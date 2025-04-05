#include "node/namenode.h"

#include <brpc/closure_guard.h>

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

#include "common/agg_inode.h"
#include "common/inode.h"
#include "node/raft_nn.h"

namespace spkdfs {

  using namespace std;
  using namespace braft;
  using json = nlohmann::json;

  void fail_response(Response* response, const std::exception& e) {
    LOG(ERROR) << e.what();
    google::FlushLogFiles(google::INFO);
    response->set_success(false);
    ErrorMessage errMsg;
    errMsg.set_code(COMMON_EXCEPTION);
    errMsg.set_message(e.what());
    *(response->mutable_fail_info()) = errMsg;
  }

  void fail_response(Response* response, const MessageException& e) {
    LOG(ERROR) << e.what();
    google::FlushLogFiles(google::INFO);
    response->set_success(false);
    *(response->mutable_fail_info()) = e.errorMessage();
  }

  void NamenodeServiceImpl::check_leader(Response* response) {
    Node leader = nn_raft_ptr->leader();
    if (!leader.valid()) {
      throw runtime_error("leader not ready");
    }
    if (leader.ip != my_ip) {
      throw MessageException(REDIRECT_TO_MASTER, leader.ip);
    }
  }

  void NamenodeServiceImpl::ls(::google::protobuf::RpcController* controller,
                               const CommonPathRequest* request, CommonPathResponse* response,
                               ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    try {
      LOG(INFO) << "rpc: ls";
      Inode inode;
      inode.set_fullpath(request->path().empty() ? "/" : request->path());
      nn_raft_ptr->ls(inode);
      response->mutable_common()->set_success(true);
      *(response->mutable_data()) = inode.value();
    } catch (const MessageException& e) {
      fail_response(response->mutable_common(), e);
    } catch (const std::exception& e) {
      fail_response(response->mutable_common(), e);
    }
  }

  void NamenodeServiceImpl::mkdir(::google::protobuf::RpcController* controller,
                                  const CommonPathRequest* request, CommonResponse* response,
                                  ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    try {
      LOG(INFO) << "rpc: mkdir";
      check_leader(response->mutable_common());
      if (request->path().empty()) {
        throw runtime_error("parameter path required");
      }
      string path = request->path();
      if (path[0] != '/') {
        path = "/" + path;
      }

      Inode inode = Inode::get_default_dir(path);
      // check in rocksdb
      // if(inode.get_fullpath() == "/") throw MessageException("cannot mkdir /")
      nn_raft_ptr->prepare_mkdir(inode);
      // task.data

      // buf.append(inode.value());
      LOG(INFO) << "going to propose inode: " << inode.value();
      Task task;
      butil::IOBuf buf;
      buf.push_back(OpType::OP_MKDIR);
      buf.append(inode.value());
      task.data = &buf;  // task.data cannot be NULL
      task.done = new RaftCommonClosure(response->mutable_common(), done_guard.release());
      nn_raft_ptr->apply(task);
    } catch (const MessageException& e) {
      fail_response(response->mutable_common(), e);
    } catch (const std::exception& e) {
      fail_response(response->mutable_common(), e);
    }
  }

  void NamenodeServiceImpl::rm(::google::protobuf::RpcController* controller,
                               const CommonPathRequest* request, CommonResponse* response,
                               ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    try {
      LOG(INFO) << "rpc: rm";
      check_leader(response->mutable_common());

      if (request->path().empty()) {
        throw runtime_error("parameter path required");
      }

      Inode inode;
      inode.set_fullpath(request->path());

      nn_raft_ptr->prepare_rm(inode);  // 先将inode.valid = false; 再在父目录的dentry中删除
      // task.data

      // buf.append(inode.value());
      LOG(INFO) << "going to propose inode: " << inode.value();
      Task task;
      butil::IOBuf buf;
      buf.push_back(OpType::OP_RM);
      buf.append(inode.value());
      task.data = &buf;  // task.data cannot be NULL
      task.done = new RaftCommonClosure(response->mutable_common(), done_guard.release());
      nn_raft_ptr->apply(task);
    } catch (const MessageException& e) {
      fail_response(response->mutable_common(), e);
    } catch (const std::exception& e) {
      fail_response(response->mutable_common(), e);
    }
  }

  void NamenodeServiceImpl::put(::google::protobuf::RpcController* controller,
                                const NNPutRequest* request, NNPutResponse* response,
                                ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    try {
      LOG(INFO) << "rpc: put";
      check_leader(response->mutable_common());
      if (request->path().empty()) {
        throw runtime_error("parameter path required");
      }
      Inode inode;
      inode.set_fullpath(request->path());
      nn_raft_ptr->prepare_put(inode);  // 本地db查询，是否已经存在该文件
      inode.storage_type_ptr = StorageType::from_string(request->storage_type());
      LOG(INFO) << "prepare put inode's prepare_put" << inode.value();

      Task task;
      butil::IOBuf buf;
      buf.push_back(OpType::OP_PUT);
      buf.append(inode.value());
      task.data = &buf;
      task.done = new RaftCommonClosure(response->mutable_common(), done_guard.release());
      nn_raft_ptr->apply(task);  // raft 同步给其他节点
    } catch (const MessageException& e) {
      fail_response(response->mutable_common(), e);
    } catch (const std::exception& e) {
      fail_response(response->mutable_common(), e);
    }
  }

  void NamenodeServiceImpl::put_batch(::google::protobuf::RpcController* controller,
                                      const ::spkdfs::NNPutRequestBatch* request,
                                      ::spkdfs::NNPutResponseBatch* response,
                                      ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    try {
      LOG(INFO) << "rpc: put agg";
      check_leader(response->mutable_common());
      vector<Inode> inodes;
      for (int i = 0; i < request->reqs_size(); i++) {
        const ::spkdfs::NNPutRequest& req = request->reqs(i);
        if (req.path().empty()) {
          throw runtime_error("parameter path required");
        }
        Inode inode;
        inode.set_fullpath(req.path());
        nn_raft_ptr->prepare_put(inode);
        inode.storage_type_ptr = StorageType::from_string(req.storage_type());
        LOG(INFO) << "prepare put inode's prepare_put" << inode.value();
        inodes.push_back(inode);
      }
      nlohmann::json j = inodes;
      Task task;
      butil::IOBuf buf;
      buf.push_back(OpType::OP_PUTBATCH);
      buf.append(j.dump());
      task.data = &buf;
      task.done = new RaftCommonClosure(response->mutable_common(), done_guard.release());
      nn_raft_ptr->apply(task);  // raft 同步给其他节点
    } catch (const MessageException& e) {
      fail_response(response->mutable_common(), e);
    } catch (const std::exception& e) {
      fail_response(response->mutable_common(), e);
    }
  }

  void NamenodeServiceImpl::put_ok(::google::protobuf::RpcController* controller,
                                   const NNPutOKRequest* request, CommonResponse* response,
                                   ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);

    LOG(INFO) << "rpc: put_ok";
    try {
      check_leader(response->mutable_common());

      if (request->path().empty()) {
        throw runtime_error("parameter path required");
      }
      Inode inode;
      inode.set_fullpath(request->path());
      nn_raft_ptr->prepare_put_ok(inode);
      inode.sub = vector<string>(request->sub().begin(),
                                 request->sub().end());  // inode中存储文件分块的每个分片的sha256
      inode.filesize = request->filesize();
      LOG(INFO) << "inode's prepare_put_ok" << inode.value();

      Task task;
      butil::IOBuf buf;
      buf.push_back(OpType::OP_PUTOK);
      buf.append(inode.value());
      task.data = &buf;
      task.done = new RaftCommonClosure(response->mutable_common(), done_guard.release());
      nn_raft_ptr->apply(task);
    } catch (const MessageException& e) {
      fail_response(response->mutable_common(), e);
    } catch (const std::exception& e) {
      fail_response(response->mutable_common(), e);
    }
  }
  static std::pair<int, int> getLastPair(const std::string& input) {
    std::stringstream ss(input);
    std::string pair;
    int last_first = 0, last_second = 0;
    // cout << "getLastPair input: " << input << endl;

    // 逐个获取逗号分隔的子字符串
    std::vector<std::string> pairs;
    while (std::getline(ss, pair, ',')) {
      pairs.push_back(pair);  // 将每个pair放入一个临时vector中
    }

    // 确保至少有一个pair，且最后一个pair符合 begin|end 格式
    if (pairs.empty()) {
      cerr << "Error: Empty input string." << endl;
      throw std::runtime_error("Empty input string.");
    }

    // 获取最后一个pair
    const std::string& last_pair = pairs.back();
    size_t pos = last_pair.find('|');
    if (pos != std::string::npos) {
      try {
        // 提取并转换 begin 和 end
        last_first = std::stoi(last_pair.substr(0, pos));    // begin
        last_second = std::stoi(last_pair.substr(pos + 1));  // end
      } catch (const std::invalid_argument& e) {
        cerr << "Invalid number format in pair: " << last_pair << endl;
        throw std::runtime_error("Invalid pair format");
      } catch (const std::out_of_range& e) {
        cerr << "Number out of range in pair: " << last_pair << endl;
        throw std::runtime_error("Number out of range in pair");
      }
    } else {
      cerr << "Invalid format, missing '|' in last pair: " << last_pair << endl;
      throw std::runtime_error("Invalid pair format, missing '|' delimiter in last pair");
    }

    return std::make_pair(last_first, last_second);
  }
  void NamenodeServiceImpl::put_ok_batch(::google::protobuf::RpcController* controller,
                                         const ::spkdfs::NNPutOKRequestBatch* request,
                                         ::spkdfs::CommonResponse* response,
                                         ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);

    LOG(INFO) << "rpc: put_ok_batch";
    try {
      check_leader(response->mutable_common());
      int req_len = request->reqs_size();
      vector<bool> valid(req_len, true);
      vector<FileInfo> files;
      uint64_t aggfile_size = 0;
      // 生成聚合文件的inode,进行同步
      SnowflakeIDGenerator generator(std::stoi(my_ip.substr(my_ip.rfind('.') + 1)));  // 传入机器 Ip
      std::string id = generator.generateID();
      // 多个小文件的inode put_ok. 将多个inode的信息一起提交，序列化到json数组中
      vector<Inode> inodes;
      for (int i = 0; i < req_len; i++) {
        const ::spkdfs::NNPutOKRequest& req = request->reqs(i);
        if (req.path().empty()) {
          throw runtime_error("parameter path required");
        }
        Inode inode;
        inode.set_fullpath(req.path());
        nn_raft_ptr->prepare_put_ok(inode);
        inode.sub = vector<string>(req.sub().begin(), req.sub().end());
        inode.filesize = req.filesize();
        inode.agg_inode_id = id;
        aggfile_size += req.filesize();
        // 解析sub得到begin，end
        auto pos = getLastPair(*req.sub().begin());
        files.emplace_back(req.path(), inode.filesize, pos);
        inodes.push_back(inode);
        LOG(INFO) << "inode's prepare_put_ok_batch" << inode.value();
      }
      // 同步多个小文件的inode信息
      nlohmann::json j = inodes;
      std::string json_str = j.dump();
      Task task;
      butil::IOBuf buf;
      buf.push_back(OpType::OP_PUTOKBATCH);
      buf.append(json_str);
      task.data = &buf;
      task.done = new RaftCommonClosure(response->mutable_common(), done_guard.release());
      nn_raft_ptr->apply(task);
      // 生成聚合文件的inode,进行同步
      buf.clear();
      AggInode aggInode(id, files, aggfile_size, valid, req_len);
      buf.push_back(OpType::OP_PUTAGG);
      buf.append(aggInode.value());
      task.data = &buf;
      task.done = new RaftCommonClosure(response->mutable_common(), done_guard.release());
      nn_raft_ptr->apply(task);
    } catch (const MessageException& e) {
      fail_response(response->mutable_common(), e);
    } catch (const std::exception& e) {
      fail_response(response->mutable_common(), e);
    }
  }
  void NamenodeServiceImpl::get_master(::google::protobuf::RpcController* controller,
                                       const Request* request, NNGetMasterResponse* response,
                                       ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    try {
      LOG(INFO) << "rpc: get_master";

      Node leader = nn_raft_ptr->leader();
      if (!leader.valid()) {
        throw runtime_error("leader not ready");
      }
      *(response->mutable_node()) = to_string(leader);
      response->mutable_common()->set_success(true);
    } catch (const MessageException& e) {
      fail_response(response->mutable_common(), e);
    } catch (const std::exception& e) {
      fail_response(response->mutable_common(), e);
    }
  }
  void NamenodeServiceImpl::update_lock(::google::protobuf::RpcController* controller,
                                        const NNLockRequest* request, CommonResponse* response,
                                        ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    LOG(INFO) << "rpc update_lock";
    try {
      check_leader(response->mutable_common());

      if (request->paths().empty()) {
        throw runtime_error("parameter paths required");
      }
      for (auto& s : request->paths()) {
        Inode inode;
        inode.set_fullpath(s);

        nn_raft_ptr->ls(inode);
        inode.update_ddl_lock();

        Task task;
        butil::IOBuf buf;
        buf.push_back(OpType::OP_PUT);
        buf.append(inode.value());
        task.data = &buf;
        task.done = new RaftCommonClosure(response->mutable_common(), done_guard.release());
        nn_raft_ptr->apply(task);
      }
    } catch (const MessageException& e) {
      fail_response(response->mutable_common(), e);
    } catch (const std::exception& e) {
      fail_response(response->mutable_common(), e);
    }
    // request;
  }
}  // namespace spkdfs
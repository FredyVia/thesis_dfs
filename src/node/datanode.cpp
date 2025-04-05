#include "node/datanode.h"

#include <brpc/closure_guard.h>
#include <brpc/controller.h>  // brpc::Controller
#include <sys/syslog.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "common/config.h"
#include "common/exception.h"
#include "common/utils.h"
#include "node/config.h"
namespace spkdfs {
  using namespace std;

  DatanodeServiceImpl::DatanodeServiceImpl(const std::string& my_ip, RaftDN* dn_raft_ptr)
      : my_ip(my_ip), dn_raft_ptr(dn_raft_ptr) {}

  void DatanodeServiceImpl::put(::google::protobuf::RpcController* controller,
                                const DNPutRequest* request, CommonResponse* response,
                                ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    try {
      // check_status();
      string blkid = request->blkid();
      auto file_path = FLAGS_data_dir + "/blk_" + blkid;
      brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
      StreamReceiver* stream_receiver_ptr
          = new StreamReceiver([file_path](const std::string& data) {
              ofstream file(file_path, ios::binary);
              if (!file) {
                LOG(ERROR) << "Failed to open file for writing." << file_path;
                return;
              }
              file << data;
              file.flush();
              file.close();  // auto close when deconstruct
            });

      brpc::StreamId stream_id;
      brpc::StreamOptions stream_options;
      stream_options.handler = stream_receiver_ptr;
      _threads.emplace_back([stream_receiver_ptr]() {
        while (!stream_receiver_ptr->is_finished()) {
          sleep(1);
        }
        delete stream_receiver_ptr;
      });
      if (brpc::StreamAccept(&stream_id, *cntl, &stream_options) != 0) {
        cntl->SetFailed("Fail to accept stream");
        throw runtime_error("Fail to accept stream");
      }
      response->mutable_common()->set_success(true);
    } catch (const std::exception& e) {
      LOG(ERROR) << e.what();
      response->mutable_common()->set_success(false);
      response->mutable_common()->mutable_fail_info()->set_code(COMMON_EXCEPTION);
      *(response->mutable_common()->mutable_fail_info()->mutable_message()) = e.what();
    }
  }

  void DatanodeServiceImpl::get(::google::protobuf::RpcController* controller,
                                const DNGetRequest* request, DNGetResponse* response,
                                ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    try {
      // check_status();
      auto file_path = FLAGS_data_dir + "/blk_" + request->blkid();

      StreamSender* sender = new StreamSender([file_path](const brpc::StreamId& stream_id) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
          LOG(ERROR) << "Failed to open input file: " << file_path;
          return;
        }

        // 检查文件是否为空
        file.seekg(0, std::ios::end);
        if (file.tellg() == 0) {
          LOG(ERROR) << "File is empty: " << file_path;
          return;
        }
        file.seekg(0, std::ios::beg);

        char buffer[BRPC_STREAM_CHUNK_SIZE];
        bool error_flag = false;
        int status = 0;

        brpc::StreamWriteOptions write_options;
        write_options.write_in_background = true;
        while (file.read(buffer, BRPC_STREAM_CHUNK_SIZE) || file.gcount() > 0) {
          size_t bytes_read = file.gcount();
          if (bytes_read > 0) {
            butil::IOBuf msg;
            msg.append(buffer, bytes_read);
            while (true) {  // status == 0 not while
              status = brpc::StreamWrite(stream_id, msg, &write_options);
              if (status == 0) {
                break;
              } else if (status == EAGAIN) {
                usleep(BRPC_STREAM_SLEEP_MICROSECOND);  // sleep for 50 ms
                continue;
              } else {
                LOG(ERROR) << "Failed to write stream " << stream_id << ", error: " << status;
                error_flag = true;
                break;
              }
            }
            if (error_flag) {
              break;
            }
          }
        }
      });

      brpc::StreamId stream_id;
      brpc::StreamOptions stream_options;
      stream_options.handler = sender;
      _threads.emplace_back([sender]() {
        while (!sender->is_finished()) {
          sleep(1);
        }
        delete sender;
      });
      brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
      if (brpc::StreamAccept(&stream_id, *cntl, &stream_options) != 0) {
        cntl->SetFailed("Fail to accept stream");
        return;
      }
      // 将读取的数据设置到响应中
      response->mutable_common()->set_success(true);
    } catch (const std::exception& e) {
      LOG(ERROR) << e.what();
      response->mutable_common()->set_success(false);
      response->mutable_common()->mutable_fail_info()->set_code(COMMON_EXCEPTION);
      *(response->mutable_common()->mutable_fail_info()->mutable_message()) = e.what();
    }
  }

  void DatanodeServiceImpl::get_namenodes(::google::protobuf::RpcController* controller,
                                          const Request* request, DNGetNamenodesResponse* response,
                                          ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    LOG(INFO) << "get_namenodes_call";
    auto nodes = dn_raft_ptr->get_namenodes();
    LOG(INFO) << to_string(nodes);
    for (const auto& node : nodes) {
      std::string node_str = to_string(node);
      response->add_nodes(node_str);
    }
    response->mutable_common()->set_success(true);
  }

  void DatanodeServiceImpl::get_datanodes(::google::protobuf::RpcController* controller,
                                          const Request* request, DNGetDatanodesResponse* response,
                                          ::google::protobuf::Closure* done) {
    try {
      brpc::ClosureGuard done_guard(done);
      LOG(INFO) << "get_datanodes_call";
      auto nodes = dn_raft_ptr->get_datanodes();
      LOG(INFO) << to_string(nodes);
      for (const auto& node : nodes) {
        std::string node_str = to_string(node);
        response->add_nodes(node_str);
      }
      response->mutable_common()->set_success(true);
    } catch (const std::exception& e) {
      LOG(ERROR) << e.what();
      response->mutable_common()->set_success(false);
      response->mutable_common()->mutable_fail_info()->set_code(COMMON_EXCEPTION);
      *(response->mutable_common()->mutable_fail_info()->mutable_message()) = e.what();
    }
  }
}  // namespace spkdfs
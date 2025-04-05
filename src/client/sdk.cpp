#include "client/sdk.h"

#include <fcntl.h>
#include <glog/logging.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "client/config.h"
#include "common/config.h"
#include "common/data_retrive.hpp"
#include "common/exception.h"
#include "common/stream.h"
#include "common/utils.h"

namespace spkdfs {
  using namespace std;
  using namespace brpc;

  void check_response(const Controller &cntl, const Response &response) {
    if (cntl.Failed()) {
      throw runtime_error("brpc not success: " + cntl.ErrorText());
    }
    if (!response.success()) {
      throw MessageException(response.fail_info());
    }
  }

  SDK::SDK(const std::string &datanode, const std::string &namenode)
      : dn_connecttion_pool(ConnecttionPool<DatanodeService_Stub>::get_instance()) {
    // get all namenodes
    Controller cntl;
    Request request;
    DNGetNamenodesResponse dn_getnamenodes_resp;
    vector<string> namenodes;
    RETRY_WITH_CONDITION(dn_connecttion_pool.get_stub(datanode)->get_namenodes(
                             &cntl, &request, &dn_getnamenodes_resp, NULL),
                         cntl.Failed());
    check_response(cntl, dn_getnamenodes_resp.common());
    cout << "namenodes: ";
    for (auto node : dn_getnamenodes_resp.nodes()) {
      namenodes.push_back(node);
      cout << node << ", ";
    }

    // get the master namenode
    string namenode_slave = namenode;
    if (namenode_slave == "") {
      namenode_slave = get_slave_namenode(namenodes);
    }
    cout << "namenode slave: " << namenode_slave << endl;
    if (nn_slave_channel.Init(string(namenode_slave).c_str(), &BRPC_CHANNEL_OPTIONS) != 0) {
      throw runtime_error("namenode master channel init failed");
    }
    nn_slave_stub_ptr = new NamenodeService_Stub(&nn_slave_channel);

    NNGetMasterResponse nn_getmaster_resp;
    cntl.Reset();
    RETRY_WITH_CONDITION(nn_slave_stub_ptr->get_master(&cntl, &request, &nn_getmaster_resp, NULL),
                         cntl.Failed());
    check_response(cntl, nn_getmaster_resp.common());

    string namenode_master = nn_getmaster_resp.node();
    cout << "namenode master: " << namenode_master << endl;

    // set the master namenode service
    if (nn_master_channel.Init(string(namenode_master).c_str(), &BRPC_CHANNEL_OPTIONS) != 0) {
      throw runtime_error("namenode_master channel init failed");
    }
    nn_master_stub_ptr = new NamenodeService_Stub(&nn_master_channel);

    // set the slave namenode service
    if (namenode_master == namenode_slave) {
      namenodes.erase(remove(namenodes.begin(), namenodes.end(), namenode_slave), namenodes.end());
      namenode_slave = get_slave_namenode(namenodes);
      // cout << "namenode slave: " << namenode_slave << endl;
      if (nn_slave_channel.Init(string(namenode_slave).c_str(), &BRPC_CHANNEL_OPTIONS) != 0) {
        throw runtime_error("namenode_slave channel init failed");
      }
      delete nn_slave_stub_ptr;
      nn_slave_stub_ptr = new NamenodeService_Stub(&nn_slave_channel);
      // cout << endl;
    }

    timer = make_shared<IntervalTimer>(
        max(LOCK_REFRESH_INTERVAL >> 2, 5),
        [this]() {
          if (remoteLocks.empty()) {
            return;
          }
          int retryCount = 0;  // 重试计数器
          while (true) {
            try {
              vector<string> paths;
              {
                std::shared_lock<std::shared_mutex> lock(mutex_remoteLocks);
                paths = vector<string>(remoteLocks.begin(), remoteLocks.end());
              }
              _lock_remote(paths);

              break;
            } catch (const exception &e) {
              if (retryCount >= 3) {  // 允许最多重试3次
                LOG(ERROR) << "retry error" << e.what();
                throw IntervalTimer::TimeoutException("timeout", e.what());
              }
              retryCount++;  // 增加重试计数器
              continue;      // 跳过当前迭代的其余部分
            }
          }
        },
        [this](const void *args) {
          throw runtime_error(string("update lock fail ") + (const char *)args);
        });
  }

  string SDK::get_slave_namenode(const vector<string> &namenodes) {
    srand(time(0));
    int index = rand() % namenodes.size();
    return namenodes[index];
  }

  SDK::~SDK() {
    if (nn_master_stub_ptr != nullptr) delete nn_master_stub_ptr;
    if (nn_slave_stub_ptr != nullptr) delete nn_slave_stub_ptr;
  }

  Inode SDK::get_inode(const std::string &dst) {
    if (local_inode_cache.find(dst) == local_inode_cache.end()) {
      throw runtime_error("cannot find inode in local_inode_cache");
    }
    return local_inode_cache[dst];
  }

  Inode SDK::get_remote_inode(const std::string &dst) {
    // if(inodeCache.find(dst) != inodeCache.end()){
    //   return inodeCache[dst];
    // }
    Controller cntl;
    CommonPathRequest request;
    CommonPathResponse response;
    *(request.mutable_path()) = dst;
    RETRY_WITH_CONDITION(nn_slave_stub_ptr->ls(&cntl, &request, &response, NULL), cntl.Failed());
    check_response(cntl, response.common());

    auto _json = nlohmann::json::parse(response.data());
    Inode inode = _json.get<Inode>();
    VLOG(2) << inode.value() << endl;
    return inode;
  }

  Inode SDK::ls(const std::string &path) { return get_remote_inode(path); }

  void SDK::mkdir(const string &dst) {
    Controller cntl;
    CommonPathRequest request;
    CommonResponse response;
    *(request.mutable_path()) = dst;
    RETRY_WITH_CONDITION(nn_master_stub_ptr->mkdir(&cntl, &request, &response, NULL),
                         cntl.Failed());
    check_response(cntl, response.common());
  }

  inline std::string SDK::guess_storage_type() const { return "RS<7,5,16>"; }

  std::vector<std::string> SDK::get_datanodes() {
    vector<string> res;
    brpc::Controller cntl;
    Request request;
    DNGetDatanodesResponse response;
    RETRY_WITH_CONDITION(
        dn_connecttion_pool.get_stub()->get_datanodes(&cntl, &request, &response, NULL),
        cntl.Failed());
    check_response(cntl, response.common());
    for (const auto &node_str : response.nodes()) {
      res.push_back(node_str);
    }
    return res;
  }

  std::vector<std::string> SDK::get_online_datanodes() {
    vector<Node> vec;
    for (const auto &node_str : get_datanodes()) {
      Node node;
      from_string(node_str, node);
      vec.push_back(node);
    }
    node_discovery(vec);
    vec.erase(
        std::remove_if(vec.begin(), vec.end(),
                       [](const Node &node) { return node.nodeStatus != NodeStatus::ONLINE; }),
        vec.end());
    vector<string> res;
    for (const auto &node : vec) {
      res.push_back(to_string(node));
    }
    return res;
  }

  void SDK::update_inode(const std::string &path) {
    std::shared_lock<std::shared_mutex> local_read_lock(mutex_local_inode_cache);
    Inode local = local_inode_cache[path];
    local_read_lock.unlock();

    Inode remote = get_remote_inode(path);
    for (int i = 0; i < min(local.sub.size(), remote.sub.size()); i++) {
      if (local.sub[i] != remote.sub[i]) {
        fs::remove_all(get_tmp_index_path(path, i));
      }
    }
    for (int i = remote.sub.size(); i < local.sub.size(); i++) {
      fs::remove_all(get_tmp_index_path(path, i));
    }
    std::unique_lock<std::shared_mutex> local_write_lock(mutex_local_inode_cache);
    local_inode_cache[path] = remote;
  }

  void SDK::read_lock(const std::string &dst) { pathlocks.read_lock(get_tmp_path(dst)); }

  void SDK::write_lock(const std::string &dst) {
    pathlocks.write_lock(get_tmp_path(dst));
    lock_remote(dst);
  }

  void SDK::_unlock_remote(const std::string &dst) {
    std::unique_lock<std::shared_mutex> lock(mutex_remoteLocks);
    remoteLocks.erase(dst);
  }

  void SDK::unlock(const std::string &dst) {
    // read also unlock remote(no effection)
    _unlock_remote(dst);
    pathlocks.unlock(get_tmp_path(dst));
  }

  void SDK::open(const std::string &path, int flags) {
    cout << "open flags: " << flags << endl;

    if (flags & O_CREAT) {
      // ref mknod & create in libfuse
      _create(path);
    }

    string local_path = get_tmp_path(path);
    mkdir_f(local_path);
    if ((flags & O_ACCMODE) != O_RDONLY) {
      string write_path = get_tmp_write_path(path);
      write_lock(path);
      mkdir_f(write_path);
      clear_dir(write_path);
    } else {
      read_lock(path);
    }
    update_inode(path);

    if (flags & O_TRUNC) {
      LOG(WARNING) << "open with flag: O_TRUNC";
      _truncate(path, 0);
    }
  }

  void SDK::lock_remote(const std::string &path) {
    _lock_remote(path);
    std::unique_lock<std::shared_mutex> lock(mutex_remoteLocks);
    remoteLocks.insert(path);
  }

  void SDK::_lock_remote(const std::string &path) {
    vector<string> paths = {path};
    _lock_remote(paths);
  }

  void SDK::_lock_remote(const std::vector<std::string> &paths) {
    brpc::Controller cntl;
    NNLockRequest request;
    CommonResponse response;
    for (auto &s : paths) {
      request.add_paths(s);
    }
    RETRY_WITH_CONDITION(nn_master_stub_ptr->update_lock(&cntl, &request, &response, NULL),
                         cntl.Failed());
    check_response(cntl, response.common());
  }

  void SDK::close(const std::string &dst) {
    fsync(dst);
    std::unique_lock<std::shared_mutex> local_write_lock(mutex_local_inode_cache);
    local_inode_cache.erase(dst);
    local_write_lock.unlock();
    unlock(dst);
  }

  void SDK::put(std::shared_ptr<UploadTask> task_ptr) {
    DataRetrive<StorageType> data_retrive(task_ptr->local_path, ClientFileType::LOCAL,
                                          task_ptr->storage_type, *this);
    // 1. name_node put
    data_retrive.namenode_put(task_ptr->remote_path);
    // 2. 编码
    auto encoded_res = data_retrive.encode_content();
    // 3. lock remote
    _lock_remote(task_ptr->remote_path);
    /* UploadTask 状态改变 WAITING -> DATA_UPLOADING */
    if (task_ptr)
      task_ptr->belong_que->change_status(task_ptr->remote_path, UploadStatus::DATA_UPLOADING);
    // 4. data_node put
    auto sub_res = data_retrive.datanode_put(encoded_res);
    /* UploadTask 状态改变 DATA_UPLOADING -> META_UPLOADING */
    if (task_ptr)
      task_ptr->belong_que->change_status(task_ptr->remote_path, UploadStatus::META_UPLOADING);
    // 5. unlock + name_node put_ok
    data_retrive.namenode_putok(sub_res);
    /* UploadTask 状态改变 META_UPLOADING -> FINISHED */
    if (task_ptr)
      task_ptr->belong_que->change_status(task_ptr->remote_path, UploadStatus::FINISHED);

    return;
  }

  void SDK::putAggFile(AggInode &aggInode, string &data, const std::string &storage_type) {
    shared_ptr<StorageType> storage_type_ptr = StorageType::from_string(storage_type);
    // 创建每个小文件inode信息
    brpc::Controller cntl;
    NNPutRequestBatch nn_put_req_batch;
    NNPutResponseBatch nn_put_resp_batch;
    for (int i = 0; i < aggInode.files.size(); i++) {
      auto file = aggInode.getFile(i);
      if (file.task_ptr) {
        std::lock_guard<std::mutex> lock(file.task_ptr->_mtx);
        // 取消该小文件聚合上传, 当前无需对远程有任何撤回动作
        if (file.task_ptr->uploadStatus == UploadStatus::CANCELED) continue;
      }
      string dstFilePath = aggInode.getFilePath(i);
      auto filesize = aggInode.getFileSize(i);
      NNPutRequest nn_put_req;
      nn_put_req.set_path(dstFilePath);
      nn_put_req.set_storage_type(storage_type);
      *nn_put_req_batch.add_reqs() = nn_put_req;
    }
    // 请求namenode_master服务，批量提交多个元数据
    nn_master_stub_ptr->put_batch(&cntl, &nn_put_req_batch, &nn_put_resp_batch, NULL);
    // 检查response
    check_response(cntl, nn_put_resp_batch.common());

    //  元数据锁，lock多个inode
    for (auto &file : aggInode.getFiles()) {
      _lock_remote(file.path);
    }

    uint64_t blockSize = storage_type_ptr->get_block_size();
    cout << "using block size: " << blockSize << endl;
    // 编码数据上传至datanode,data是聚合文件数据
    string res = encode_one_block(storage_type_ptr, data);
    NNPutOKRequestBatch nnPutokReqBatch;
    for (auto &file : aggInode.getFiles()) {
      if (file.task_ptr) {
        { // 检查本小文件是否取消上传
          std::lock_guard<std::mutex> lock(file.task_ptr->_mtx);
          if (file.task_ptr->uploadStatus == UploadStatus::CANCELED) {
            // 取消 put_batch 中对该单个小文件的 prepare_put 操作
            rm(file.path);
            continue;
          }
        }
        /* 每个小文件 UploadStatus 状态: WAITING -> DATA_UPLOADING */
        file.task_ptr->belong_que->change_status(file.path, UploadStatus::DATA_UPLOADING);
      }
      string sub = res;
      sub.push_back(',');
      auto [begin, end] = file.pos;
      // 小文件的sub和其他两种不同，需要特殊处理，要包括小文件所在的聚合文件的起始位置和结束位置
      sub += (std::to_string(begin) + "|" + std::to_string(end));
      NNPutOKRequest nnPutokReq;
      nnPutokReq.set_path(file.path);
      nnPutokReq.set_filesize(file.size);
      nnPutokReq.add_sub(sub);
      *nnPutokReqBatch.add_reqs() = nnPutokReq;
    }
    for (auto &file : aggInode.getFiles()) {
      /* 每个小文件 UploadStatus 状态: DATA_UPLOADING -> META_UPLOADING */
      if (file.task_ptr)
        // META_UPLOADING 状态下 上传无法终止 等待上传完成
        file.task_ptr->belong_que->change_status(file.path, UploadStatus::META_UPLOADING);
      _unlock_remote(file.path);
    }
    CommonResponse response;
    cntl.Reset();
    // 提交所有小文件的inode信息
    nn_master_stub_ptr->put_ok_batch(&cntl, &nnPutokReqBatch, &response, NULL);
    /* 每个小文件 UploadStatus 状态: META_UPLOADING -> FINISHED */
    for (auto &file : aggInode.getFiles()) {
      if (file.task_ptr)
        file.task_ptr->belong_que->change_status(file.path, UploadStatus::FINISHED);
    }
    check_response(cntl, response.common());
    cout << "put agg ok" << endl;
  }

  std::string SDK::get_from_datanode(const std::string &datanode, const std::string &blkid) {
    DNGetRequest dnget_req;
    DNGetResponse dnget_resp;
    *(dnget_req.mutable_blkid()) = blkid;
    brpc::Controller cntl;
    brpc::StreamId stream_id;
    string res;
    StreamReceiver receiver([&res](const string &data) { res = data; });
    brpc::StreamOptions stream_options;
    stream_options.handler = &receiver;
    if (brpc::StreamCreate(&stream_id, cntl, &stream_options) != 0) {
      LOG(ERROR) << "Fail to create stream1";
      throw runtime_error("Fail to create stream");
    }
    ConnecttionPool<DatanodeService_Stub>::get_instance().get_stub(datanode)->get(
        &cntl, &dnget_req, &dnget_resp, NULL);
    check_response(cntl, dnget_resp.common());
    butil::IOBuf msg;
    msg.append("start");
    if (brpc::StreamWrite(stream_id, msg) != 0) {
      LOG(ERROR) << "Failed to write stream";
      throw runtime_error("stream start write failed");
    }
    while (receiver.is_finished() == false) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    stream_options.handler = nullptr;
    if (cal_sha256sum(res) != blkid) {
      LOG(ERROR) << "sha256sum check failed";
      throw runtime_error("sha256sum check fail");
    }
    cout << datanode << " | " << blkid << " success" << endl;

    return res;
  }

  std::string SDK::put_to_datanode(const string &datanode, const std::string &block) {
    string sha256sum = cal_sha256sum(block);
    cout << "sha256sum:" << sha256sum << endl;
    // 假设每个分块的大小是block.size() / k
    Controller cntl;
    DNPutRequest dn_req;
    CommonResponse dn_resp;
    *(dn_req.mutable_blkid()) = sha256sum;
    brpc::StreamId stream_id;
    if (brpc::StreamCreate(&stream_id, cntl, nullptr) != 0) {
      LOG(ERROR) << "Fail to create stream1";
      throw runtime_error("Fail to create stream");
    }
    LOG(INFO) << "Created Stream=" << stream_id;
    dn_connecttion_pool.get_stub(datanode)->put(&cntl, &dn_req, &dn_resp, NULL);

    check_response(cntl, dn_resp.common());

    size_t offset = 0;
    size_t block_size = block.size();
    bool error_flag = false;
    int status = 0;
    brpc::StreamWriteOptions write_options;
    write_options.write_in_background = true;
    while (status == 0 && offset < block_size) {
      // 计算当前块的大小
      size_t current_chunk_size = std::min(BRPC_STREAM_CHUNK_SIZE, block_size - offset);

      // 创建当前块的 IOBuf
      butil::IOBuf msg;
      msg.append(block.data() + offset, current_chunk_size);
      while (true) {  // status == 0 not while
        status = brpc::StreamWrite(stream_id, msg, &write_options);
        if (status == 0) {
          offset += current_chunk_size;

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

    if (brpc::StreamClose(stream_id) != 0) {
      throw runtime_error("stream close fail");
    }
    return sha256sum;
  }

  std::string SDK::encode_one_block(std::shared_ptr<StorageType> storage_type_ptr,
                                    const std::string &block) {
    auto vec = storage_type_ptr->encode(block);  // rs encode，编码为k+m个分片
    vector<string> nodes = get_online_datanodes();
    std::vector<pair<std::string, std::string>> nodes_hashs;
    int node_index = 0, succ = 0;
    string sha256sum;
    int fail_count = 0;  // avoid i-- becomes endless loop
    // 上传每个编码后的分片到datanode
    for (int i = 0; i < vec.size(); i++) {
      cout << "node[" << node_index << "]"
           << ":" << nodes[node_index] << endl;
      try {
        sha256sum = put_to_datanode(nodes[node_index], vec[i]);
        // 每个分片的sha256sum和datanode的ip存入nodes_hashs
        nodes_hashs.push_back(make_pair(nodes[node_index], sha256sum));
        succ++;
        cout << nodes[node_index] << " success" << endl;
      } catch (const exception &e) {
        LOG(ERROR) << e.what();
        LOG(ERROR) << nodes[node_index] << " failed";
        i--;
        fail_count++;
      }
      node_index = (node_index + 1) % nodes.size();
      if (node_index == 0 && storage_type_ptr->check(succ) == false) {
        LOG(WARNING) << "may not be secure";
        if (fail_count == nodes.size()) {
          LOG(ERROR) << "all failed";
          break;
        }
        fail_count = 0;
      }
    }
    return encode_one_sub(nodes_hashs);
  }

  std::string SDK::decode_one_block(std::shared_ptr<StorageType> storage_type_ptr,
                                    const std::string &s) {
    int succ = 0;
    vector<string> datas;
    datas.resize(storage_type_ptr->getDecodeBlocks());
    std::vector<std::pair<std::string, std::string>> res;
    res = decode_one_sub(s);  // 解析分片信息(datnode_ip|blkid分片sha256sum)
    string node;
    string blkid;
    // 从datanode获取分片信息
    for (auto &p : res) {
      node = p.first;
      blkid = p.second;
      try {
        datas[succ++] = get_from_datanode(node, blkid);
        if (succ == storage_type_ptr->getDecodeBlocks()) {
          // k个分片都获取到后，进行解码
          // 副本类型则时获取到一个分片就返回
          return storage_type_ptr->decode(datas);
        }
      } catch (const exception &e) {
        LOG(ERROR) << e.what();
        LOG(ERROR) << node << " | " << blkid << " failed";
      }
    }
    return "";
  }

  void SDK::get(const std::string &src, const std::string &dst) {
    Inode inode = get_remote_inode(src);  // 获取元数据inode

    cout << "using storage type: " << inode.storage_type_ptr->to_string() << endl;
    ofstream dstFile(dst, std::ios::binary);
    if (!dstFile.is_open()) {
      throw runtime_error("Failed to open file" + dst);
    }
    // 增加聚合小文件的sub处理
    int begin = 0, end = 0;
    if (!inode.agg_inode_id.empty()) {
      if (inode.sub.empty()) {
        throw runtime_error("agg file sub is empty");
      }
      string sub = inode.sub[0];
      size_t pos = sub.rfind(",");

      if (pos != std::string::npos) {
        std::string last_part = sub.substr(pos + 1);
        // 使用字符串流解析 last_part 中的 begin 和 end
        std::istringstream ss(last_part);
        char delimiter;
        if (ss >> begin >> delimiter >> end) {
          // 解析成功，移除 ",begin|end"
          sub = sub.substr(0, pos);
        } else {
          throw std::runtime_error("Invalid format: Unable to parse begin|end");
        }
      }
      // 读取聚合文件的数据,截取begin到end的数据
      std::string decoded_block = decode_one_block(inode.storage_type_ptr, sub);
      if (begin < 0 || end > decoded_block.size() || begin >= end) {
        throw std::runtime_error("Invalid range: begin or end out of bounds");
      }
      // 提取 [begin, end] 区间的数据
      std::string substring_to_write = decoded_block.substr(begin, end - begin + 1);
      // 写入文件
      dstFile << substring_to_write;
    } else {
      for (const auto &s : inode.sub) {  // sub是一个文件分块的分片信息
        dstFile << decode_one_block(inode.storage_type_ptr, s);  // 得到解码后的文件分块
      }
    }
    dstFile.flush();
    dstFile.close();
    fs::resize_file(dst, inode.filesize);
  }

  void SDK::rm(const std::string &dst) {
    Controller cntl;
    CommonPathRequest request;
    CommonResponse response;
    *(request.mutable_path()) = dst;
    RETRY_WITH_CONDITION(nn_master_stub_ptr->rm(&cntl, &request, &response, NULL), cntl.Failed());
    check_response(cntl, response.common());
  }

  // void SDK::local_truncate(const Inode& inode, size_t size) {
  //   inode.get_block_size();
  //   inode.filesize;
  //   for (const auto &entry : fs::directory_iterator(write_dst)) {
  //     const auto index_str = entry.path().filename().string();
  //     if (entry.is_directory()) {
  //     }
  //     res.push_back(stoi(index_str));
  //   }
  //   inode.get_block_size();
  // }
  void SDK::truncate(const std::string &dst, size_t size) {
    open(dst, O_WRONLY | O_TRUNC);
    unlock(dst);
  }
  void SDK::_truncate(const std::string &dst, size_t size) {
    Inode inode = get_inode(dst);
    if (inode.filesize == size) {
      return;
    }
    NNPutOKRequest nnPutokReq;
    *(nnPutokReq.mutable_path()) = dst;
    nnPutokReq.set_filesize(size);
    for (int i = 0; i < align_up(size, inode.get_block_size()); i++) {
      nnPutokReq.add_sub(inode.sub[i]);
    }
    Controller cntl;
    CommonResponse response;
    RETRY_WITH_CONDITION(nn_master_stub_ptr->put_ok(&cntl, &nnPutokReq, &response, NULL),
                         cntl.Failed());
    check_response(cntl, response.common());
    unlock(dst);
  }

  inline pair<int, int> SDK::get_indexs(const Inode &inode, uint64_t offset, size_t size) const {
    return make_pair(align_index_down(offset, inode.get_block_size()),
                     align_index_up(offset + size, inode.get_block_size()));
  }

  void SDK::ln_path_index(const std::string &path, uint32_t index) const {
    string hard_ln_path = get_tmp_write_path(path) + "/" + std::to_string(index);
    cout << "creating hardlink: " << hard_ln_path << " >>>>> " << get_tmp_index_path(path, index)
         << endl;
    fs::remove(hard_ln_path);
    std::filesystem::create_hard_link(get_tmp_index_path(path, index), hard_ln_path);
  }

  inline std::string SDK::get_tmp_write_path(const string &path) const {
    return get_tmp_path(path) + "/write";
  }

  inline std::string SDK::get_tmp_path(const string &path) const {
    return FLAGS_data_dir + "/fuse/" + cal_md5sum(path);
  }

  std::string SDK::get_tmp_index_path(const string &path, uint32_t index) const {
    string dst_path = get_tmp_path(path);
    return dst_path + "/" + std::to_string(index);
  }

  std::string SDK::read_data(const Inode &inode, std::pair<int, int> indexs) {
    string tmp_path;
    cout << "using storage type: " << inode.storage_type_ptr->to_string() << endl;
    auto iter = inode.sub.begin() + indexs.first;
    std::ostringstream oss;
    string block;
    for (int index = indexs.first; index < indexs.second; index++) {
      if (iter >= inode.sub.end()) {
        oss << string((indexs.second - index) * inode.get_block_size(), '\0');
        break;
      }
      string blks = *iter;
      tmp_path = get_tmp_index_path(inode.get_fullpath(), index);
      if (fs::exists(tmp_path) && fs::file_size(tmp_path) > 0) {
        block = read_file(tmp_path);
      } else {
        pathlocks.write_lock(tmp_path);
        if (fs::exists(tmp_path) && fs::file_size(tmp_path) > 0) {
          block = read_file(tmp_path);
        } else {
          ofstream ofile(tmp_path, std::ios::binary);
          if (!ofile) {
            LOG(ERROR) << "Failed to open file for reading." << tmp_path;
            throw std::runtime_error("openfile error:" + tmp_path);
          }
          block = decode_one_block(inode.storage_type_ptr, blks);
          ofile << block;
          ofile.close();
        }
        pathlocks.unlock(tmp_path);
      }
      oss << block;
      iter++;
    }
    return oss.str();
  }

  std::string SDK::read_data(const string &path, uint64_t offset, size_t size) {
    string res;
    Inode inode = get_inode(path);
    if (inode.filesize < offset) {
      cout << "out of range: filesize: " << inode.filesize << ", offset: " << offset
           << ", size: " << size << endl;
      return "";
    }
    if (inode.filesize < offset + size) {
      size = inode.filesize - offset;
    }
    pair<int, int> indexs = get_indexs(inode, offset, size);
    auto s = read_data(inode, indexs);
    return string(s.begin() + offset - indexs.first * inode.get_block_size(),
                  s.begin() + offset - indexs.first * inode.get_block_size() + size);
  }

  void SDK::write_data(const string &path, uint64_t offset, const std::string &s) {
    Inode inode = get_inode(path);

    size_t size = s.size();
    auto iter = s.begin();

    pair<int, int> indexs = get_indexs(inode, offset, size);
    if (offset < inode.filesize && offset % inode.get_block_size() != 0) {
      read_data(inode, make_pair(indexs.first, indexs.first + 1));
    }
    if (offset + size < inode.filesize && indexs.first != indexs.second - 1
        && (offset + size) % inode.get_block_size() != 0) {
      read_data(inode, make_pair(indexs.second - 1, indexs.second));
    }

    for (int index = indexs.first; index < indexs.second; index++) {
      size_t left = index == indexs.first ? offset % inode.get_block_size() : 0;
      size_t right = index == indexs.second - 1 ? (offset + size) % inode.get_block_size()
                                                : inode.get_block_size();
      size_t localSize = right - left;
      // iter += localSize;

      string dst = get_tmp_index_path(path, index);

      // using ios::binary only will clear the file
      ofstream dstFile(dst, ios::binary | ios::app);
      if (!dstFile) {
        LOG(ERROR) << "failed to write data to " << dst << std::endl;
        throw runtime_error("failed to write data to " + dst);
      }
      cout << "seek begin: " << left << ", seek end: " << right << endl;
      dstFile.seekp(left);
      dstFile << string(iter, iter + localSize);
      dstFile.close();
      iter += localSize;
      ln_path_index(path, index);
    }
  }
  void SDK::create(const string &path) { open(path, O_WRONLY | O_CREAT); }
  // stateless
  void SDK::_create(const string &path) {
    string dst_path = get_tmp_index_path("", 0);
    std::ofstream ofile(dst_path);
    ofile.close();
    std::shared_ptr<UploadTask> task_ptr = std::make_shared<UploadTask>(
        dst_path, path, fs::file_size(dst_path), guess_storage_type());
    put(task_ptr);
    // put(dst_path, path, guess_storage_type());
  }

  void SDK::fsync(const std::string &dst) {
    string write_dst = get_tmp_write_path(dst);
    if (!fs::exists(write_dst)) {
      return;
    }
    if (!fs::is_directory(write_dst)) {
      throw runtime_error(write_dst + " is not directory");
    }
    vector<int> res;
    for (auto &file : list_dir(write_dst)) {
      res.push_back(stoi(file));
    };
    if (res.empty()) return;
    sort(res.begin(), res.end());
    Inode inode = get_inode(dst);
    if (inode.sub.size() <= res.back()) {
      inode.sub.resize(res.back() + 1);
    }
    int index_filesize, last_index_filesize = 0;
    string index_block;
    for (auto &i : res) {
      string index_file_path = write_dst + "/" + std::to_string(i);
      pathlocks.read_lock(index_file_path);
      index_block = read_file(index_file_path);
      last_index_filesize = index_block.size();
      inode.sub[i] = encode_one_block(inode.storage_type_ptr, index_block);
      fs::remove(index_file_path);
      pathlocks.unlock(index_file_path);
    }
    int filesize = std::max(inode.filesize,
                            (uint64_t)res.back() * inode.get_block_size() + last_index_filesize);
    Controller cntl;
    NNPutOKRequest nnPutokReq;
    CommonResponse response;
    *(nnPutokReq.mutable_path()) = dst;
    nnPutokReq.set_filesize(filesize);
    for (auto &s : inode.sub) {
      nnPutokReq.add_sub(s);
    }
    RETRY_WITH_CONDITION(nn_master_stub_ptr->put_ok(&cntl, &nnPutokReq, &response, NULL),
                         cntl.Failed());
    check_response(cntl, response.common());
  }
}  // namespace spkdfs
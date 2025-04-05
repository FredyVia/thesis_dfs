#ifndef DATA_RETRIVE_HPP
#define DATA_RETRIVE_HPP

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <fcntl.h>
#include <glog/logging.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

#include "client/sdk.h"
#include "common/common.pb.h"
#include "common/connecttion_pool.hpp"
#include "common/datanode.pb.h"
#include "common/exception.h"
#include "common/inode.h"
#include "common/namenode.pb.h"
#include "common/utils.h"

namespace spkdfs {
  enum class ClientFileType { LOCAL, REMOTE };

  class AnonymousMMap {
  private:
    size_t _size;
    void *_addr;

  public:
    AnonymousMMap() : _size(0), _addr(nullptr) {}
    // AnonymousMMap(size_t _sz) : _size(_sz), _addr(nullptr) {
    //   _addr = mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    //   if (_addr == MAP_FAILED) {
    //     throw std::runtime_error("Failed to create anonymous mmap");
    //   }
    // }
    AnonymousMMap(const AnonymousMMap &m) = delete;
    AnonymousMMap &operator=(const AnonymousMMap &m) = delete;

    ~AnonymousMMap() {
      if (_addr && _addr != MAP_FAILED) {
        munmap(_addr, _size);
      }
    }

  public:
    inline void *data() { return _addr; }
    inline size_t size() { return _size; }

    void save_to_file(const std::string &path) {
      std::ofstream file(path, std::ios::binary);
      if (!file.is_open()) {
        throw std::runtime_error("open file error");
      }

      file.write(static_cast<char *>(_addr), _size);
      file.flush();
      file.close();
    }

    void open_from_file(const std::string &path) {
      std::ifstream file(path, std::ios::binary);
      if (!file.is_open()) {
        throw std::runtime_error("open file error");
      }

      if (_addr && _addr != MAP_FAILED) {
        munmap(_addr, _size);
      }

      auto filesize = std::filesystem::file_size(path);
      _size = filesize;
      _addr = mmap(nullptr, filesize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (_addr == MAP_FAILED) {
        throw std::runtime_error("Failed to create anonymous mmap");
      }

      file.read(static_cast<char *>(_addr), filesize);
      file.close();
    }

    // 切割 和 sdk切割方式一样
    std::vector<std::string> split_all_block(size_t blockSize) {
      char *raw_data = static_cast<char *>(_addr);
      size_t block_nums = _size / blockSize;
      size_t bytes_remained = _size % blockSize;
      std::vector<std::string> vec;
      vec.reserve(block_nums + 1);

      std::string tmp(blockSize, 0);
      for (size_t i = 0; i < block_nums; i++) {
        std::memcpy(tmp.data(), raw_data + i * blockSize, blockSize);
        vec.push_back(tmp);
        std::cout << "<block i>: " << cal_sha256sum(tmp) << std::endl;  // 检查块切割效果
      }

      if (bytes_remained > 0) {
        std::memcpy(tmp.data(), raw_data + block_nums * blockSize, bytes_remained);
        vec.push_back(tmp);
        std::cout << "<block i>: " << cal_sha256sum(tmp) << std::endl;  // 检查块切割效果
      }
      return vec;
    }
  };  // class AnonymousMMap end

  template <typename ST> class DataRetrive {
  private:
    std::shared_ptr<ST> storage_type_ptr;

    // 依赖SDK实例
    SDK &sdk;
    // 数据获取器实现
    AnonymousMMap _content;
    size_t _size;
    size_t _offset;

    std::string local_path;
    std::string remote_path;
    ClientFileType cur_file_type;  // 当前数据获取器持有文件的类型

    // reserved
    Inode inode;

  public:
    DataRetrive(const std::string &path, ClientFileType filetype, const std::string &st, SDK &sdk);
    ~DataRetrive() { std::cout << "~DataRetrive" << std::endl; };

    // 编码
    std::vector<std::string> encode_one_block(const std::string &block);
    std::vector<std::vector<std::string>> encode_content();
    std::string encode_one_sub(const std::vector<std::pair<std::string, std::string>> &nodes_hashs);
    // [!解码]

  public:
    void namenode_put(const std::string &path);  // 未替换SDK中代码
    std::vector<std::string> datanode_put(std::vector<std::vector<std::string>> &encoded_data);
    void namenode_putok(const std::vector<std::string> &sub);
  };  // class DataRetrive end

  // 通过 storage_type_ptr 对单个block进行编码
  template <typename ST>
  std::vector<std::string> DataRetrive<ST>::encode_one_block(const std::string &block) {
    // base_ptr -> derived_ptr
    return storage_type_ptr->encode(
        block);  // auto encoded_blocks = storage_type_ptr->encode(block);
  }

  // 对当前 _content 的所有block进行编码
  template <typename ST> std::vector<std::vector<std::string>> DataRetrive<ST>::encode_content() {
    size_t blockSize = storage_type_ptr->get_block_size();
    // std::cout << "blockSize = " << blockSize << std::endl;

    std::vector<std::string> block_vec = _content.split_all_block(blockSize);
    std::vector<std::vector<std::string>> encode_result;
    int index = 0;
    for (auto &item : block_vec) {
      encode_result.emplace_back(std::move(encode_one_block(item)));
    }

    return encode_result;
  }

  template <typename ST> void DataRetrive<ST>::namenode_put(const std::string &path) {
    try {
      brpc::Controller cntl;
      NNPutRequest nn_put_req;
      NNPutResponse nn_put_resp;
      this->remote_path = path;  // 记录 dstFilePath
      *(nn_put_req.mutable_path()) = this->remote_path;
      *(nn_put_req.mutable_storage_type()) = storage_type_ptr->to_string();
      RETRY_WITH_CONDITION(sdk.nn_master_stub_ptr->put(&cntl, &nn_put_req, &nn_put_resp, NULL),
                           cntl.Failed());
      check_response(cntl, nn_put_resp.common());
    } catch (const std::runtime_error &e) {
      std::cerr << e.what() << std::endl;
    } catch (const std::exception &e) {
      std::cerr << e.what() << std::endl;
    }
    return;
  }

  // 原sdk.encode_one_block
  template <typename ST> std::vector<std::string> DataRetrive<ST>::datanode_put(
      std::vector<std::vector<std::string>> &encoded_data) {
    std::vector<std::string> nodes = sdk.get_online_datanodes();
    std::vector<std::string> sub;
    int node_index = 0, succ = 0, fail_count = 0;

    for (size_t file_block = 0; file_block < encoded_data.size(); file_block++) {
      std::string sha256sum;
      std::vector<std::string> &vec = encoded_data[file_block];
      std::vector<std::pair<std::string, std::string>> nodes_hashs;
      for (size_t i = 0; i < vec.size(); i++) {
        std::cout << "node[" << node_index << "]:" << nodes[node_index] << std::endl;
        // 暂时用sdk.put_to_datanode()接口
        try {
          sha256sum = sdk.put_to_datanode(nodes[node_index], vec[i]);
          nodes_hashs.push_back(std::make_pair(nodes[node_index], sha256sum));
          succ++;
          std::cout << nodes[node_index] << " success" << std::endl;
        } catch (std::exception &e) {
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
      // 暂时先用 encode_one_sub 后面改到 storage_type
      std::string one_sub_hash = this->encode_one_sub(nodes_hashs);
      sub.push_back(one_sub_hash);
    }
    return sub;
  }

  template <typename ST> void DataRetrive<ST>::namenode_putok(const std::vector<std::string> &sub) {
    try {
      brpc::Controller cntl;
      NNPutOKRequest nn_putok_req;
      CommonResponse nn_putok_resp;

      *(nn_putok_req.mutable_path()) = this->remote_path;
      nn_putok_req.set_filesize(this->_size);
      for (size_t i = 0; i < sub.size(); i++) {
        nn_putok_req.add_sub(sub[i]);
        std::cout << "namenode_putok() <sub>:" << sub[i] << std::endl;
      }

      // unlock remote
      sdk._unlock_remote(this->remote_path);

      RETRY_WITH_CONDITION(
          sdk.nn_master_stub_ptr->put_ok(&cntl, &nn_putok_req, &nn_putok_resp, NULL),
          cntl.Failed());
      check_response(cntl, nn_putok_resp.common());
    } catch (std::exception &e) {
      std::cerr << e.what() << std::endl;
    }
  }

  template <typename ST>
  DataRetrive<ST>::DataRetrive(const std::string &path, ClientFileType filetype,
                               const std::string &_storage_type, SDK &_sdk)
      : storage_type_ptr(StorageType::from_string(_storage_type)), sdk(_sdk) {
    std::cout << "DataRetrive<ST> = " << _storage_type << std::endl;
    if (storage_type_ptr == nullptr) {
      throw std::runtime_error("resource initial error");
    }

    this->_offset = 0;
    this->cur_file_type = filetype;
    if (filetype == ClientFileType::LOCAL) {  // 文件在客户端
      this->local_path = path;

      _content.open_from_file(path);
      _size = _content.size();

    } else {  // 文件在node
      this->remote_path = path;
    }

    std::cout << "DataRetrive()" << std::endl;
  }

  template <typename ST> std::string DataRetrive<ST>::encode_one_sub(
      const std::vector<std::pair<std::string, std::string>> &nodes_hashs) {
    std::string res;
    for (int i = 0; i < nodes_hashs.size(); i++) {
      res += to_string(nodes_hashs[i].first) + "|" + nodes_hashs[i].second + ",";
    }
    res.pop_back();
    return res;
  }
}  // namespace spkdfs
#endif
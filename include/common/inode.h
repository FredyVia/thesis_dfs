#ifndef INODE_H
#define INODE_H

#include <glog/logging.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include "common/storage_type.h"
#include "common/utils.h"

namespace spkdfs {
  std::vector<std::pair<std::string, std::string>> decode_one_sub(const std::string& str);
  std::string encode_one_sub(const std::vector<std::pair<std::string, std::string>>& sub);
  class Inode {
    std::string fullpath;

  public:
    static Inode get_default_dir(const std::string& path);
    inline void set_fullpath(const std::string& fullpath) {
      this->fullpath = simplify_path(fullpath);
    }
    inline std::string get_fullpath() const { return fullpath; }

    bool is_directory = false;
    uint64_t filesize = 0;
    std::shared_ptr<StorageType> storage_type_ptr;  // 存储类型：副本，纠删码, 聚合文件
    std::vector<std::string> sub;                   // 目录sub为子目录，文件sub为数据块
    bool valid;
    uint64_t ddl_lock = 0;
    // 聚合的小文件需要维护大文件的Inode信息，大文件Inode信息中保存聚合操作的一些信息
    std::string agg_inode_id;
    // int modification_time;
    void lock();
    void unlock();
    void update_ddl_lock();
    std::string filename() const;
    std::string parent_path() const;
    std::string value() const;
    inline int get_block_size() const {
      return storage_type_ptr == nullptr ? 0 : storage_type_ptr->get_block_size();
    }
  };
  bool operator==(const Inode& lhs, const Inode& rhs) noexcept;

  void to_json(nlohmann::json& j, const Inode& inode);

  void from_json(const nlohmann::json& j, Inode& inode);
}  // namespace spkdfs
#endif
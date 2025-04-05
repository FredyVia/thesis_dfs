#include "common/inode.h"

#include <glog/logging.h>

#include <string>

#include "common/config.h"
#include "common/exception.h"
#include "erasurecode.h"

namespace spkdfs {
  using json = nlohmann::json;
  using namespace std;

  std::string Inode::value() const {
    nlohmann::json j;
    to_json(j, *this);
    return j.dump();
  }

  void Inode::lock() {
    uint64_t now = get_time();
    if (ddl_lock >= now) {
      throw MessageException(EXPECTED_LOCK_CONFLICT,
                             fullpath + " file is being edit! ddl_lock: " + std::to_string(ddl_lock)
                                 + ", now: " + std::to_string(now));
    }
    ddl_lock = now + LOCK_REFRESH_INTERVAL;
  }

  void Inode::unlock() { ddl_lock = 0; }

  void Inode::update_ddl_lock() { ddl_lock = get_time() + LOCK_REFRESH_INTERVAL; }

  std::string Inode::filename() const {
    std::string s = std::filesystem::path(fullpath).filename().string();
    if (is_directory) {
      s += "/";
    }
    return s;
  }

  std::string Inode::parent_path() const { return get_parent_path(fullpath); }

  inline std::string to_string(std::unique_ptr<StorageType> storageType_ptr) {
    return storageType_ptr->to_string();
  }

  Inode Inode::get_default_dir(const string& path) {
    Inode inode;
    inode.set_fullpath(path);
    inode.is_directory = true;
    inode.filesize = 0;
    inode.storage_type_ptr = nullptr;
    inode.sub = {};
    inode.valid = true;
    inode.ddl_lock = 0;
    return inode;
  }

  std::string encode_one_sub(const std::vector<std::pair<std::string, std::string>>& nodes_hashs) {
    string res;
    for (int i = 0; i < nodes_hashs.size(); i++) {
      res += to_string(nodes_hashs[i].first) + "|" + nodes_hashs[i].second + ",";
    }
    res.pop_back();
    // LOG(INFO) << res;
    return res;
  }

  std::vector<std::pair<std::string, std::string>> decode_one_sub(const std::string& one_sub) {
    std::vector<std::pair<std::string, std::string>> res;
    std::map<int, std::pair<std::string, std::string>> tmp;
    stringstream ss(one_sub);
    string str;
    while (getline(ss, str, ',')) {
      stringstream ss(str);
      string node, blkid;
      getline(ss, node, '|');   // 提取第一个部分
      getline(ss, blkid, '|');  // 提取第二个部分
      VLOG(2) << node << "|" << blkid << endl;
      res.push_back(make_pair(node, blkid));
    }
    return res;
  }

  void to_json(nlohmann::json& j, const Inode& inode) {
    j = nlohmann::json{{"fullpath", inode.get_fullpath()},
                       {"is_directory", inode.is_directory},
                       {"filesize", inode.filesize},
                       {"sub", inode.sub},
                       {"valid", inode.valid},
                       {"ddl_lock", inode.ddl_lock}};
    //  ,{"modification_time", inode.modification_time}
    if (inode.storage_type_ptr != nullptr) {
      nlohmann::json storage_json;
      inode.storage_type_ptr->to_json(storage_json);  // 假设 to_json 返回一个 JSON 对象
      j["storage_type"] = storage_json.dump();  // 转换为字符串并添加到 JSON 对象中
    }
    if (!inode.agg_inode_id.empty()) {
      j["agg_inode_id"] = inode.agg_inode_id;
    }
  }

  void from_json(const nlohmann::json& j, Inode& inode) {
    inode.set_fullpath(j.at("fullpath").get<std::string>());
    inode.is_directory = j.at("is_directory").get<bool>();
    inode.filesize = j.at("filesize").get<uint64_t>();
    inode.sub = j.at("sub").get<std::vector<std::string>>();
    inode.valid = j.at("valid").get<bool>();
    inode.ddl_lock = j.at("ddl_lock").get<uint64_t>();
    inode.storage_type_ptr = nullptr;
    if (j.find("storage_type") != j.end()) {
      // LOG(INFO) << j.at("storage_type");
      auto storage_type_json = nlohmann::json::parse(j.at("storage_type").get<std::string>());
      from_json(storage_type_json, inode.storage_type_ptr);
    }
    if (j.find("agg_inode_id") != j.end()) {
      inode.agg_inode_id = j.at("agg_inode_id").get<std::string>();
    }

    // inode.modification_time = j.at("modification_time").get<int>();
  }

  bool operator==(const Inode& linode, const Inode& rinode) noexcept {
    return linode.sub == rinode.sub && linode.filesize == rinode.filesize;
  }
}  // namespace spkdfs
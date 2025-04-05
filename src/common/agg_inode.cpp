#include "common/agg_inode.h"

namespace spkdfs {

  // 序列化 FileInfo
  void to_json(nlohmann::json& j, const FileInfo& file) {
    j = nlohmann::json{
        {"path", file.path}, {"size", file.size}, {"pos", {file.pos.first, file.pos.second}}};
  }

  // 反序列化 FileInfo
  void from_json(const nlohmann::json& j, FileInfo& file) {
    file.path = j.at("path").get<std::string>();
    file.size = j.at("size").get<uint64_t>();
    file.pos = {j.at("pos")[0].get<int>(), j.at("pos")[1].get<int>()};  // 反序列化 std::pair
  }

  // 序列化 AggInode
  void to_json(nlohmann::json& j, const AggInode& inode) {
    j = nlohmann::json{{"id", inode.id},
                       {"files", inode.files},
                       {"aggregate_size", inode.aggregate_size},
                       {"valid", inode.valid},
                       {"valid_count", inode.valid_count}};
  }

  // 反序列化 AggInode
  void from_json(const nlohmann::json& j, AggInode& inode) {
    inode.id = j.at("id").get<std::string>();
    inode.files = j.at("files").get<std::vector<FileInfo>>();
    inode.aggregate_size = j.at("aggregate_size").get<uint64_t>();
    inode.valid = j.at("valid").get<std::vector<bool>>();
    inode.valid_count = j.at("valid_count").get<int>();
  }

}  // namespace spkdfs

#ifndef NODE_H
#define NODE_H
#include <braft/raft.h>

#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace spkdfs {
  enum class NodeStatus { ONLINE, OFFLINE, UNKNOWN };
  class Node {
  public:
    std::string ip;
    int port = 0;
    NodeStatus nodeStatus;
    Node(std::string ip = "", int port = 0, NodeStatus nodeStatus = NodeStatus::UNKNOWN);
    void scan();
    // 重载 == 运算符
    bool operator==(const Node& other) const;
    bool operator!=(const Node& other) const;
    bool operator<(const Node& other) const;
    bool valid();
    static Node INVALID_NODE;
    braft::PeerId to_peerid() const;
    std::string to_string() const;
    void from_peerId(braft::PeerId peerid);
  };

  void from_string(const std::string& node_str, Node& node);

  std::string to_string(const std::vector<Node>& nodes);

  void node_discovery(std::vector<Node>& nodes);

  std::vector<Node> parse_nodes(const std::string& nodes_str);

  std::string to_string(const spkdfs::Node& node);

  void to_json(nlohmann::json& j, const Node& node);

  void from_json(const nlohmann::json& j, Node& node);

  std::ostream& operator<<(std::ostream& out, const Node& node);
}  // namespace spkdfs

#endif
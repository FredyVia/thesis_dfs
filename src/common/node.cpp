#include "common/node.h"

#include <arpa/inet.h>
#include <brpc/channel.h>
#include <brpc/controller.h>  // brpc::Controller
#include <brpc/server.h>      // brpc::Server
#include <glog/logging.h>

#include <exception>
#include <functional>
#include <thread>

#include "common/common.pb.h"
#include "common/config.h"
#include "common/utils.h"

namespace spkdfs {
  using namespace std;
  using namespace braft;
  Node Node::INVALID_NODE = Node("0.0.0.0", 0);

  braft::PeerId Node::to_peerid() const {
    butil::EndPoint ep;
    butil::str2endpoint(ip.c_str(), port, &ep);
    braft::PeerId peerid(ep);
    VLOG(2) << peerid;
    return peerid;
  }

  void Node::scan() {
    brpc::Channel channel;
    // 初始化 channel
    if (channel.Init(to_string().c_str(), &BRPC_CHANNEL_OPTIONS) != 0) {
      throw runtime_error("channel init failed");
    }

    brpc::Controller cntl;
    spkdfs::Request request;
    spkdfs::CommonResponse response;

    CommonService_Stub stub(&channel);
    RETRY_WITH_CONDITION(stub.echo(&cntl, &request, &response, NULL), cntl.Failed());

    VLOG(2) << (*this);
    if (cntl.Failed()) {
      LOG(WARNING) << "RPC failed: " << cntl.ErrorText();
      LOG(WARNING) << "OFFLINE";
      nodeStatus = NodeStatus::OFFLINE;
    } else {
      nodeStatus = NodeStatus::ONLINE;
      VLOG(2) << "ONLINE";
    }
  }
  bool Node::operator==(const Node& other) const { return ip == other.ip; }
  bool Node::operator!=(const Node& other) const { return !(*this == other); }
  bool Node::operator<(const Node& other) const { return ip < other.ip; }
  std::ostream& operator<<(std::ostream& out, const Node& node) {
    out << to_string(node);
    return out;
  }
  bool Node::valid() { return *this != INVALID_NODE; }

  void Node::from_peerId(braft::PeerId peerid) {
    ip = inet_ntoa(peerid.addr.ip);
    port = peerid.addr.port;
  }

  std::string to_string(const vector<Node>& nodes) {
    std::ostringstream oss;
    for (const auto& node : nodes) {
      oss << to_string(node) << ",";
    }
    string s = oss.str();
    s.pop_back();
    return s;
  }

  vector<Node> parse_nodes(const string& nodes_str) {
    vector<Node> res;
    istringstream stream(nodes_str);
    string token;
    Node node;
    while (getline(stream, token, ',')) {
      from_string(token, node);
      res.push_back(node);
    }
    return res;
  }

  void node_discovery(std::vector<Node>& nodes) {
    std::vector<std::thread> threads;

    for (auto& node : nodes) {
      threads.emplace_back(std::bind(&Node::scan, &node));
    }

    for (auto& t : threads) {
      t.join();
    }
    for (const auto& node : nodes) {
      VLOG(2) << node
              << " status: " << (node.nodeStatus == NodeStatus::ONLINE ? "ONLINE" : "OFFLINE");
    }
  }

  Node::Node(std::string ip, int port, NodeStatus nodeStatus)
      : ip(ip), port(port), nodeStatus(nodeStatus) {
    // warning: ipv6 not supported
    from_string(ip, *this);
  }

  std::string Node::to_string() const { return spkdfs::to_string(*this); }

  void from_string(const std::string& node_str, Node& node) {
    std::istringstream iss(node_str);
    std::getline(iss, node.ip, ':');
    std::string portStr;
    std::getline(iss, portStr);
    if (!portStr.empty()) node.port = std::stoi(portStr);
  }

  std::string to_string(const spkdfs::Node& node) {
    return node.ip + ":" + std::to_string(node.port);
  }

  void to_json(nlohmann::json& j, const Node& node) {
    j = nlohmann::json{{"ip", node.ip}, {"port", node.port}};
  }

  void from_json(const nlohmann::json& j, Node& node) {
    node.ip = j.at("ip").get<std::string>();
    node.port = j.at("port").get<int>();
  }
}  // namespace spkdfs
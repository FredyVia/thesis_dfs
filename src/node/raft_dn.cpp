#include "node/raft_dn.h"

#include <brpc/closure_guard.h>
#include <butil/logging.h>
#define DBG_MACRO_NO_WARNING
#include <dbg-macro/dbg.h>

#include <algorithm>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#include "common/node.h"
#include "common/utils.h"
#include "node/config.h"
namespace spkdfs {
  using namespace std;
  using json = nlohmann::json;

  RaftDN::RaftDN(const std::string& my_ip, const std::vector<Node>& _nodes,
                 DNApplyCallbackType applyCallback)
      : my_ip(my_ip), applyCallback(applyCallback) {
    datanode_list = _nodes;
    for (auto& node : datanode_list) {
      node.port = FLAGS_dn_port;
    }
    LOG(INFO) << "datanode list" << endl;
    dbg::pretty_print(LOG(INFO), datanode_list);

    node_options.fsm = this;
    node_options.node_owns_fsm = false;
    std::string prefix = "local://" + FLAGS_data_dir + "/datanode";
    node_options.log_uri = prefix + "/log";
    node_options.raft_meta_uri = prefix + "/raft_meta";
    node_options.snapshot_uri = prefix + "/snapshot";
    node_options.disable_cli = true;
    vector<braft::PeerId> peer_ids;
    peer_ids.reserve(datanode_list.size());  // 优化：预先分配所需空间
    std::transform(datanode_list.begin(), datanode_list.end(), std::back_inserter(peer_ids),
                   [](const Node& node) { return node.to_peerid(); });
    node_options.initial_conf = braft::Configuration(peer_ids);
    raft_node = new braft::Node("RaftDN", Node(my_ip, FLAGS_dn_port).to_peerid());
  }

  RaftDN::~RaftDN() { delete raft_node; }

  void RaftDN::start() {
    // 实现细节
    if (raft_node->init(node_options) != 0) {
      google::FlushLogFiles(google::INFO);
      LOG(ERROR) << "Fail to init raft node";
      throw runtime_error("raft datanode init failed");
    }
  }

  void RaftDN::shutdown() { raft_node->shutdown(NULL); }
  void RaftDN::add_node(const Node& node) { raft_node->add_peer(node.to_peerid(), NULL); }
  void RaftDN::remove_node(const Node& node) { raft_node->remove_peer(node.to_peerid(), NULL); }
  void RaftDN::on_apply(braft::Iterator& iter) {
    LOG(INFO) << "datanode on_apply";
    // 实现细节
    butil::IOBuf data;
    for (; iter.valid(); iter.next()) {
      data = iter.data();
      braft::AsyncClosureGuard closure_guard(iter.done());
    }
    string data_str = data.to_string();
    LOG(INFO) << "got namenodes:" << data_str;
    auto _json = json::parse(data_str);
    namenode_list = _json.get<vector<Node>>();
    applyCallback(namenode_list);
    if (leader().ip != my_ip) {
      if (nn_timer != nullptr) {
        LOG(INFO) << "stop nn_timer";
        delete nn_timer;
        nn_timer = nullptr;
      }
      return;
    }
    LOG(INFO) << "start nn_timer";
    nn_timer = new IntervalTimer(
        NN_INTERVAL,
        [this]() {
          int retryCount = 0;  // 重试计数器
          for (size_t i = 0; i < namenode_list.size(); ++i) {
            namenode_list[i].scan();
            if (namenode_list[i].nodeStatus == NodeStatus::OFFLINE) {
              LOG(INFO) << "retry:" << namenode_list[i];
              if (retryCount >= 3) {  // 允许最多重试3次
                throw IntervalTimer::TimeoutException("timeout", namenode_list[i].ip.c_str());
              }
              i--;           // 减少索引以在下一次迭代中重新扫描同一节点
              retryCount++;  // 增加重试计数器
              continue;      // 跳过当前迭代的其余部分
            }
            retryCount = 0;  // 重置重试计数器
          }
        },
        [this](const void* args) {
          string lossip = string(static_cast<const char*>(args));
          LOG(INFO) << "lossIP: " << lossip;
          on_nodes_loss({Node(lossip)});
        });
  }
  // my onw callback, not override
  void RaftDN::on_nodes_loss(const vector<Node>& node) {
    LOG(INFO) << "on_nodes_loss";
    on_leader_start(0);
  }

  vector<Node> RaftDN::get_namenodes() { return namenode_list; }
  vector<Node> RaftDN::get_datanodes() { return datanode_list; }
  void RaftDN::on_leader_start(int64_t term) {
    LOG(INFO) << "Node becomes leader";
    vector<Node> nodes = get_namenodes();
    std::sort(nodes.begin(), nodes.end());

    node_discovery(nodes);

    nodes.erase(
        std::remove_if(nodes.begin(), nodes.end(),
                       [](const Node& node) { return node.nodeStatus != NodeStatus::ONLINE; }),
        nodes.end());
    LOG(INFO) << "online nodes";
    dbg::pretty_print(LOG(INFO), nodes);
    vector<Node> allnodes = datanode_list;

    LOG(INFO) << "allnodes: ";
    dbg::pretty_print(LOG(INFO), allnodes);

    // 从 allnodes 中删除在 nodes 中的节点
    allnodes.erase(std::remove_if(allnodes.begin(), allnodes.end(),
                                  [&nodes](const Node& node) {
                                    return std::binary_search(nodes.begin(), nodes.end(), node);
                                  }),
                   allnodes.end());

    // 遍历 allnodes
    for (size_t i = 0; i < allnodes.size() && nodes.size() < FLAGS_expected_nn;
         i += FLAGS_expected_nn) {
      size_t end = std::min(i + FLAGS_expected_nn, allnodes.size());
      std::vector<Node> sub(allnodes.begin() + i, allnodes.begin() + end);

      node_discovery(sub);

      for (auto& node : sub) {
        if (node.nodeStatus == NodeStatus::ONLINE) {
          node.port = FLAGS_nn_port;
          nodes.push_back(node);
          if (nodes.size() >= FLAGS_expected_nn) {
            break;
          }
        }
      }
    }
    LOG(INFO) << "chosen namenodes:";
    dbg::pretty_print(LOG(INFO), nodes);
    if (nodes.empty()) {
      LOG(ERROR) << "no node ready";
      throw runtime_error("no node ready");
    }

    json j = json::array();
    for (const auto& node : nodes) {
      j.push_back(node);  // 这里会自动调用 to_json 函数
    }
    butil::IOBuf buf;
    buf.append(j.dump());
    // butil::IOBuf buf;
    braft::Task task;
    task.data = &buf;
    task.done = nullptr;
    raft_node->apply(task);
  }

  Node RaftDN::leader() {
    Node node;
    braft::PeerId leader = raft_node->leader_id();
    if (leader.is_empty()) {
      LOG(INFO) << "I'm leader";
      node.ip = my_ip;
      node.port = FLAGS_nn_port;
    } else {
      node.from_peerId(leader);
    }
    LOG(INFO) << "leader: " << node;
    return node;
  }
  // void RaftDN::on_leader_stop(const butil::Status& status) {
  //   LOG(INFO) << "Node stepped down : " << status;
  // }
  void RaftDN::on_snapshot_save(braft::SnapshotWriter* writer, braft::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    string file_path = writer->get_path() + "/namenodes.json";
    ofstream file(file_path, ios::binary);
    if (!file) {
      LOG(ERROR) << "Failed to open file for writing." << file_path;
      done->status().set_error(EIO, "Fail to save " + file_path);
    }
    json j = json::array();
    for (const auto& node : namenode_list) {
      j.push_back(node);  // 这里会自动调用 to_json 函数
    }
    file << j.dump();
    file.close();
    writer->add_file(file_path);
  }

  int RaftDN::on_snapshot_load(braft::SnapshotReader* reader) {
    string file_path = reader->get_path() + "/namenodes.json";
    string str;
    str = read_file(file_path);
    if (!str.empty()) {
      auto _json = json::parse(str);
      namenode_list = _json.get<vector<Node>>();
    }
    LOG(INFO) << "propose namenodes:";
    dbg::pretty_print(LOG(INFO), namenode_list);
    return 0;
  }

  // void RaftDN::on_shutdown() { LOG(INFO) << "This node is down" ; }
  // void RaftDN::on_error(const ::braft::Error& e) { LOG(ERROR) << "Met raft error " << e ;
  // } void RaftDN::on_configuration_committed(const ::braft::Configuration& conf) {
  //   LOG(INFO) << "Configuration of this group is " << conf;
  // }
  // void RaftDN::on_stop_following(const ::braft::LeaderChangeContext& ctx) {
  //   LOG(INFO) << "Node stops following " << ctx;
  // }
  // void RaftDN::on_start_following(const ::braft::LeaderChangeContext& ctx) {
  //   LOG(INFO) << "Node start following " << ctx;
  // }
}  // namespace spkdfs
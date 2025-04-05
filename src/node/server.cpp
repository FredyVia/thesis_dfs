#include "node/server.h"

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <iterator>
#include <memory>
#include <vector>

#include "common/node.h"
#include "common/service.h"
#include "common/utils.h"

namespace spkdfs {
  using namespace std;
  using namespace braft;

  Server::Server(const std::vector<Node>& nodes) {
    my_ip = get_my_ip(nodes);
    LOG(INFO) << "my_ip: " << my_ip;
    dn_raft_ptr = new RaftDN(my_ip, nodes,
                             std::bind(&Server::on_namenodes_change, this, std::placeholders::_1));
    CommonServiceImpl* dn_common_service_ptr = new CommonServiceImpl();
    DatanodeServiceImpl* dn_service_ptr = new DatanodeServiceImpl(my_ip, dn_raft_ptr);
    if (dn_server.AddService(dn_common_service_ptr, brpc::SERVER_OWNS_SERVICE) != 0) {
      throw runtime_error("server failed to add dn common service");
    }
    if (dn_server.AddService(dn_service_ptr, brpc::SERVER_OWNS_SERVICE) != 0) {
      throw runtime_error("server failed to add dn service");
    }
    if (add_service(&dn_server, FLAGS_dn_port) != 0) {
      throw runtime_error("server failed to add dn service");
    }
  }

  void Server::on_namenodes_change(const std::vector<Node>& namenodes) {
    bool found = any_of(namenodes.begin(), namenodes.end(),
                        [this](const Node& node) { return node.ip == my_ip; });
    if (found) {
      LOG(INFO) << "I'm in namenodes list";
      if (nn_raft_ptr != nullptr) {
        change_namenodes_list(namenodes);
        return;
      }
      LOG(INFO) << "start new namenode";
      nn_raft_ptr = new RaftNN(my_ip, namenodes);
      CommonServiceImpl* nn_common_service_ptr = new CommonServiceImpl();
      NamenodeServiceImpl* nn_service_ptr = new NamenodeServiceImpl(my_ip, nn_raft_ptr);

      if (nn_server.AddService(nn_common_service_ptr, brpc::SERVER_OWNS_SERVICE) != 0) {
        throw runtime_error("server failed to add common nn service");
      }
      if (nn_server.AddService(nn_service_ptr, brpc::SERVER_OWNS_SERVICE) != 0) {
        throw runtime_error("server failed to add nn service");
      }
      if (add_service(&nn_server, FLAGS_nn_port) != 0) {
        throw runtime_error("server failed to add nn service");
      }
      if (nn_server.Start(FLAGS_nn_port, NULL) != 0) {
        throw runtime_error("nn server failed to start service");
      }
      nn_raft_ptr->start();
      return;
    }
    if (nn_raft_ptr != nullptr) {
      LOG(INFO) << "I'm going to stop namenode";
      nn_raft_ptr->shutdown();
      nn_server.Stop(0);
      nn_server.Join();
      delete nn_raft_ptr;
      nn_raft_ptr = nullptr;
      nn_server.ClearServices();
    }
  }

  void Server::change_namenodes_list(const std::vector<Node>& new_namenodes) {
    vector<Node> old_namenodes{nn_raft_ptr->list_peers()};
    int alivenodes_count{0};
    for (const auto& node : old_namenodes) {
      if (find(begin(new_namenodes), end(new_namenodes), node) != new_namenodes.end()) {
        ++alivenodes_count;
      }
    }
    LOG(INFO) << "old nodes: " << to_string(old_namenodes);
    LOG(INFO) << "new nodes: " << to_string(new_namenodes);
    LOG(INFO) << "alive nodes count: " << alivenodes_count;
    if (alivenodes_count > new_namenodes.size() / 2) {
      nn_raft_ptr->change_peers(new_namenodes);
    } else {
      nn_raft_ptr->reset_peers(new_namenodes);
    }
  }

  void Server::start() {
    if (dn_server.Start(FLAGS_dn_port, NULL) != 0) {
      LOG(ERROR) << "Fail to start Server";
      throw runtime_error("start service failed");
    }
    dn_raft_ptr->start();
    int count = 0;
    while (!brpc::IsAskedToQuit()) {
      sleep(10);
      count++;
      if (count == 5) {
        count = 0;
        LOG(INFO) << "Running";
      }
    }
  }
  Node Server::leader() {
    if (nn_raft_ptr == nullptr) {
      throw runtime_error("not running namenode");
    }
    return nn_raft_ptr->leader();
  }
  Server::~Server() {
    nn_server.Stop(0);
    nn_server.Join();

    dn_server.Stop(0);
    dn_server.Join();
    if (nn_raft_ptr != nullptr) {
      nn_raft_ptr->shutdown();
      delete nn_raft_ptr;
      nn_raft_ptr = nullptr;
    }
    if (dn_raft_ptr != nullptr) {
      dn_raft_ptr->shutdown();
      delete dn_raft_ptr;
      dn_raft_ptr = nullptr;
    }
  }
}  // namespace spkdfs
#ifndef SERVER_H
#define SERVER_H
#include <brpc/server.h>  // brpc::Server

#include <set>
#include <string>
#include <vector>

#include "common/node.h"
#include "node/config.h"
#include "node/datanode.h"
#include "node/namenode.h"
#include "node/raft_dn.h"
#include "node/raft_nn.h"
#include "node/rocksdb.h"

namespace spkdfs {

  class Server {
  public:
    Server(const std::vector<Node>& nodes);
    ~Server();
    void start();

  private:
    std::string my_ip;

    brpc::Server nn_server;
    brpc::Server dn_server;

    RaftDN* dn_raft_ptr = nullptr;
    RaftNN* nn_raft_ptr = nullptr;
    void on_namenode_master_change(const Node& node);
    Node leader();
    void on_namenodes_change(const std::vector<Node>& namenodes);
    void change_namenodes_list(const std::vector<Node>& new_namenodes);
  };
}  // namespace spkdfs
#endif
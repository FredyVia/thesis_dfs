#include "node/config.h"

#include <exception>
#include <sstream>
#include <string>
#include <vector>
namespace spkdfs {
  using namespace std;
  DEFINE_string(data_dir, "/spkdfs/data", "data storage");
  DEFINE_uint32(nn_port, 10001, "port of namenode");
  DEFINE_uint32(dn_port, 10002, "port of datanode");
  // DEFINE_uint32(dn_rpc_port, 10002, "port of datanode rpc");
  // DEFINE_uint32(dn_raft_port, 10003, "port of datanode raft");
  DEFINE_uint32(expected_nn, 6, "count of namenodes");
  DEFINE_string(nodes, "", "list of all nodes");
  DEFINE_string(coredumps_dir, "/spkdfs/coredumps", "coredumps_dir");
  // void Config::parse_cmdline(int argc, char* argv[]) {
  //   gflags::ParseCommandLineFlags(&argc, &argv, true);
  //   auto ptr = getInstance();

  // }
  // shared_ptr<Config> Config::getInstance() {
  //   lock_guard<mutex> lock(mutex_);
  //   if (!instance_) {
  //     instance_ = shared_ptr<Config>(new Config);
  //   }
  //   return instance_;
  // }
}  // namespace spkdfs
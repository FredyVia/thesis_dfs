#include <gflags/gflags.h>

#include <iostream>
#include <string>
#include <vector>

#include "client/config.h"
#include "client/daemon.h"
#include "client/daemon_service.h"
DEFINE_string(daemon_ip_port, "0.0.0.0:8003", "daemon_server ip:port");
DEFINE_uint64(filesize_boundary, 4 * 1024 * 1024, "boundary of file size division (default 4MB)");
using namespace spkdfs;
int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  // 将日志信息输出到标准输出
  FLAGS_logtostderr = true;

  LOG(INFO) << "daemon main";

  spkdfs::Daemon daemon_server(FLAGS_daemon_ip_port, FLAGS_filesize_boundary, FLAGS_datanode,
                               FLAGS_namenode);
  daemon_server.start();

  return 0;
}
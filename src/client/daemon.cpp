#include "client/daemon.h"

#include <brpc/server.h>

#include <string>

#include "client/daemon_service.h"
#include "common/common.pb.h"
#include "common/daemon.pb.h"
namespace spkdfs {
  /* daemon 构造 (注册rpc实现/服务)*/
  Daemon::Daemon(std::string& ip, size_t filesize_boundary, std::string _datanode,
                 std::string _namenode)
      : my_ip(ip),
        sdk(_datanode, _namenode),
        upload(filesize_boundary, sdk),
        download(upload, sdk) {
    daemon_ser_impl_ptr = new DaemonServiceImpl(upload, download);

    if (0 != daemon_server.AddService(daemon_ser_impl_ptr, brpc::SERVER_OWNS_SERVICE)) {
      LOG(FATAL) << "Failed to add daemon_ser_impl_ptr";
      throw std::runtime_error("server failed to add daemon server");
    }

    LOG(INFO) << "Daemon(std::string &ip); ip = " << ip;
  }

  /* daemon 析构 */
  Daemon::~Daemon() {
    daemon_server.Stop(0);
    daemon_server.Join();

    delete daemon_ser_impl_ptr;
  }

  /* daemon 服务启动 */
  void Daemon::start() {
    LOG(INFO) << "Daemon start!";

    if (0 != daemon_server.Start(port(), NULL)) {
      LOG(ERROR) << "Fail to start Daemon_Server";
      throw std::runtime_error("start service failed");
    }

    daemon_server.RunUntilAskedToQuit();
  }

  /* 工具 */
  uint32_t Daemon::port() const {  // 得到ip中的端口
    size_t pos = my_ip.find(':');
    if (pos != std::string::npos) {
      std::string port = my_ip.substr(pos + 1);
      return std::stoul(port);
    }
    return 8003;
  };
}  // namespace spkdfs
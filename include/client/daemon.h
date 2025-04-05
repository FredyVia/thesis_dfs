#ifndef _DAEMON_H_
#define _DAEMON_H_
#include <brpc/server.h>

#include "client/daemon_download.h"
#include "client/daemon_service.h"
#include "client/daemon_upload.h"
#include "common/common.pb.h"
#include "common/daemon.pb.h"

namespace spkdfs {

  class Daemon {
    brpc::Server daemon_server;
    DaemonServiceImpl *daemon_ser_impl_ptr;  // 生产任务者
    DaemonUpload upload;                     // 消费任务者（实现上传）
    DaemonDownload download;                 // (下载)

    std::string my_ip;  // 127.0.0.1:8003
    std::mutex _mutex;
    std::condition_variable _cv;
    SDK sdk;

  public:
    Daemon(std::string &ip, size_t filesize_boundary, std::string _datanode,
           std::string _namenode = "");
    ~Daemon();
    void start();

    uint32_t port() const;
  };
}  // namespace spkdfs
#endif

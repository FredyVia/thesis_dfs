#ifndef SPKDFS_DAEMON_SERVICE_H
#define SPKDFS_DAEMON_SERVICE_H
#include <brpc/server.h>

#include <condition_variable>
#include <mutex>

#include "client/daemon_download.h"
#include "client/daemon_upload.h"
#include "common/daemon.pb.h"
#include "node/server.h"

namespace spkdfs {
  class DaemonServiceImpl : public DaemonService {
    DaemonUpload &_upload;
    DaemonDownload &_download;
    void put(::google::protobuf::RpcController *controller,
             const ::spkdfs::DaemonPutRequest *request, ::spkdfs::DaemonPutResponse *response,
             ::google::protobuf::Closure *done) override;
    void get(::google::protobuf::RpcController *controller,
             const ::spkdfs::DaemonGetRequest *request, ::spkdfs::DaemonGetResponse *response,
             ::google::protobuf::Closure *done) override;
    void sync(::google::protobuf::RpcController *controller,
              const ::spkdfs::DaemonSyncRequest *request, ::spkdfs::DaemonSyncResponse *response,
              ::google::protobuf::Closure *done) override;
    void ls(::google::protobuf::RpcController *controller,
            const ::spkdfs::CommonPathRequest *request, ::spkdfs::CommonPathResponse *response,
            ::google::protobuf::Closure *done) override;
    void rm(::google::protobuf::RpcController *controller,
            const ::spkdfs::CommonPathRequest *request, ::spkdfs::CommonPathResponse *response,
            ::google::protobuf::Closure *done) override;
    void debug(::google::protobuf::RpcController *controller,
               const ::spkdfs::DaemonSyncRequest *request, ::spkdfs::DaemonSyncResponse *response,
               ::google::protobuf::Closure *done) override;

  public:
    DaemonServiceImpl(DaemonUpload &upload, DaemonDownload &download);
  };
}  // namespace spkdfs
#endif
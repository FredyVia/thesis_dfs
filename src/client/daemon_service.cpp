#include "client/daemon_service.h"

#include "client/config.h"
#include "client/daemon_upload.h"

// #include <grpcpp/grpcpp.h>

#include <fstream>
#include <iostream>

namespace spkdfs {
  std::string GET_FILE_NAME(std::string& path) {
    return std::filesystem::path(path).filename().string();
  }
  DaemonServiceImpl::DaemonServiceImpl(DaemonUpload& upload, DaemonDownload& download)
      : _upload(upload), _download(download) {}

  /* brpc服务端 rpc实现 */
  void DaemonServiceImpl::put(::google::protobuf::RpcController* controller,
                              const DaemonPutRequest* request, DaemonPutResponse* response,
                              google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);

    try {
      LOG(INFO) << "DaemonServiceImpl::put(): " << request->local_path() << " | "
                << request->remote_path() << " | " << request->storage_type();

      /* 在 DaemonUpload 中: 根据文件大小区分上传队列 */
      _upload.upload(request->local_path(), request->remote_path(), request->storage_type());
      response->mutable_common()->set_success(true);
    } catch (const std::exception& e) {
      response->mutable_common()->set_success(false);
      response->mutable_common()->mutable_fail_info()->set_code(COMMON_EXCEPTION);
      response->mutable_common()->mutable_fail_info()->set_message(e.what());
    }
  }

  void DaemonServiceImpl::get(::google::protobuf::RpcController* controller,
                              const DaemonGetRequest* request, DaemonGetResponse* response,
                              google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);

    try {
      LOG(INFO) << "GET: remote_path=" << request->remote_path()
                << " | local_path=" << request->local_path();
      _download.download(request->remote_path(), request->local_path());
      response->mutable_common()->set_success(true);
    } catch (std::exception& e) {
      response->mutable_common()->set_success(false);
      response->mutable_common()->mutable_fail_info()->set_code(COMMON_EXCEPTION);
      response->mutable_common()->mutable_fail_info()->set_message(e.what());
    }
  }

  void DaemonServiceImpl::sync(::google::protobuf::RpcController* controller,
                               const DaemonSyncRequest* request, DaemonSyncResponse* response,
                               google::protobuf::Closure* done) {
    brpc::ClosureGuard done_gaurd(done);

    try {
      LOG(INFO) << "DaemonServiceImpl::sync(): local_path = " << request->local_path()
                << " | remote_path = " << request->remote_path();

      bool res = _upload.fsync(request->local_path(), request->remote_path());
      response->mutable_common()->set_success(true);
      response->set_finished(res);
      // response->set_success(true);
    } catch (std::exception& e) {
      response->mutable_common()->set_success(false);
      response->mutable_common()->mutable_fail_info()->set_code(COMMON_EXCEPTION);
      response->mutable_common()->mutable_fail_info()->set_message(e.what());
    }
    return;
  }

  void DaemonServiceImpl::ls(::google::protobuf::RpcController* controller,
                             const ::spkdfs::CommonPathRequest* request,
                             ::spkdfs::CommonPathResponse* response,
                             ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);

    try {
      LOG(INFO) << "DaemonServiceImpl::ls(): path = " << request->path();

      response->set_data(_upload.ls(request->path()));

      response->mutable_common()->set_success(true);
    } catch (std::exception& e) {
      response->mutable_common()->set_success(false);
      response->mutable_common()->mutable_fail_info()->set_code(COMMON_EXCEPTION);
      response->mutable_common()->mutable_fail_info()->set_message(e.what());
    }
    return;
  }

  void DaemonServiceImpl::rm(::google::protobuf::RpcController* controller,
                             const ::spkdfs::CommonPathRequest* request,
                             ::spkdfs::CommonPathResponse* response,
                             ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);

    try {
      LOG(INFO) << "DaemonServiceImpl::rm(): path = " << request->path();

      bool res = _upload.rm(request->path());

      response->mutable_common()->set_success(res);
    } catch (std::exception& e) {
      response->mutable_common()->set_success(false);
      response->mutable_common()->mutable_fail_info()->set_code(COMMON_EXCEPTION);
      response->mutable_common()->mutable_fail_info()->set_message(e.what());
    }
    return;
  }

  void DaemonServiceImpl::debug(::google::protobuf::RpcController* controller,
                                const ::spkdfs::DaemonSyncRequest* request,
                                ::spkdfs::DaemonSyncResponse* response,
                                ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_gaurd(done);
    LOG(INFO) << "DaemonServiceImpl::debug() ";
    _upload.debug();
    return;
  }

}  // namespace spkdfs
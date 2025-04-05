#ifndef SPKDFS_DAEMON_UPLOAD_TASK_H
#define SPKDFS_DAEMON_UPLOAD_TASK_H
#include <brpc/server.h>

#include <mutex>
#include <string>

#include "common/daemon.pb.h"
#include "node/server.h"
namespace spkdfs {
  // 等待中， 数据上传中， 元数据上传中， 取消， 完成
  enum UploadStatus { WAITING, DATA_UPLOADING, META_UPLOADING, CANCELED, FINISHED };

  class UploadQueue;

  class UploadTask {
  public:
    std::mutex _mtx;
    UploadQueue *belong_que;
    UploadStatus uploadStatus;
    std::string storage_type;  // 对接SDK

    /* bigfile task */
    std::string local_path;
    std::string remote_path;
    size_t size;

    /* smallfile task */
    AggInode *aggInode;
    std::string *aggData;

    UploadTask(std::string local_path, std::string remote_path, size_t size = 0,
               std::string storage_type = "", UploadQueue *que = nullptr)
        : size(size),
          local_path(local_path),
          remote_path(remote_path),
          storage_type(storage_type),
          belong_que(que) {}
  };
}  // namespace spkdfs
#endif
#ifndef DAEMON_DOWNLOAD_H
#define DAEMON_DOWNLOAD_H
#include <string>

#include "client/daemon_upload.h"
#include "client/sdk.h"
namespace spkdfs {

  class DaemonDownload {
    friend class DaemonUpload;

  private:
    DaemonUpload& upload;
    SDK& sdk;

    std::atomic<bool> _stop;
    std::unique_ptr<std::thread> local_file_download_thread_ptr;
    std::unique_ptr<std::thread> remote_file_download_thread_ptr;

    /* <local_src, local_dst> */
    std::queue<std::pair<std::string, std::string>> local_que;
    std::queue<std::pair<std::string, std::string>> remote_que;

    std::mutex _mtx_local;
    std::condition_variable _cv_local;

    std::mutex _mtx_remote;
    std::condition_variable _cv_remote;

  public:
    DaemonDownload(DaemonUpload& _upload, SDK& _sdk);
    ~DaemonDownload();
    void stop();

    void download(const std::string& src, const std::string& dst);

    /* 从本地DaemonUpload下载 线程处理函数 */
    void thread_local_file_download();
    /* 从remote下载 线程处理函数 */
    void thread_remote_file_download();
  };
}  // namespace spkdfs

#endif
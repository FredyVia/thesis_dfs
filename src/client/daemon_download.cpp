#include "client/daemon_download.h"

#include <glog/logging.h>

#include <filesystem>
#include <memory>

#include "client/config.h"
using namespace std;

namespace spkdfs {
  DaemonDownload::DaemonDownload(DaemonUpload& _upload, SDK& sdk) : upload(_upload), sdk(sdk) {
    LOG(INFO) << "DaemonDownload::DaemonDownload()";

    _stop.store(false);
    local_file_download_thread_ptr
        = std::make_unique<std::thread>([this]() { thread_local_file_download(); });
    remote_file_download_thread_ptr
        = std::make_unique<std::thread>([this]() { thread_remote_file_download(); });
  }

  DaemonDownload::~DaemonDownload() {
    LOG(INFO) << "DaemonDownload::~DaemonDownload()";
    stop();
  }

  void DaemonDownload::stop() {
    _stop.store(true);
    local_file_download_thread_ptr->join();
    remote_file_download_thread_ptr->join();
  }

  /* src 远程路径, dst 本地路径 */
  void DaemonDownload::download(const std::string& src, const std::string& dst) {
    /* 1. 文件在Daemon中存在还未上传(_que中存在 || _map中存在)
          查询到src对应的本地文件路径, 拷贝文件给dst路径文件 */
    auto bigf_taskptr = upload.big_file_que.ls(src);
    if (bigf_taskptr) {  // 在大文件队列中找到, 利用 task 查询本地路径文件
      std::string local_src = bigf_taskptr.value()->local_path;
      {
        std::lock_guard<std::mutex> lock(_mtx_local);
        local_que.push(std::make_pair(local_src, dst));
      }
      _cv_local.notify_one();
      return;
    }

    auto smallf_taskptr = upload.small_file_que.ls(src);
    if (smallf_taskptr) {  // 在小文件队列中找到, 利用 task 查询本地路径文件
      std::string local_src = smallf_taskptr.value()->local_path;
      {
        std::lock_guard<std::mutex> lock(_mtx_local);
        local_que.push(std::make_pair(local_src, dst));
      }
      _cv_local.notify_one();
      return;
    }

    /* 2. 文件不在Daemon中,(既不在_que中也不在_map中) 通过SDK远程下载 [线程下载] */
    {
      std::lock_guard<std::mutex> lock(_mtx_remote);
      remote_que.push(std::make_pair(src, dst));
    }
    _cv_remote.notify_one();
    return;
  }

  /* 无论是小文件还是大文件都在本地 */
  void DaemonDownload::thread_local_file_download() {
    while (!_stop.load()) {
      std::unique_lock<std::mutex> lock(_mtx_local);
      _cv_local.wait(lock, [this] { return !local_que.empty() || _stop.load(); });
      try {
        if (!local_que.empty()) {  // 本地文件copy
          std::pair<std::string, std::string> task = local_que.front();
          local_que.pop();
          std::filesystem::copy(task.first, task.second,
                                std::filesystem::copy_options::overwrite_existing);
        }
      } catch (std::filesystem::filesystem_error& e) {
        LOG(ERROR) << "Error copying file: " << e.what();
      }

      if (_stop.load() && local_que.empty()) break;
    }
  }

  /* SDK.get() */
  void DaemonDownload::thread_remote_file_download() {
    while (!_stop.load()) {
      std::unique_lock<std::mutex> lock(_mtx_remote);
      _cv_remote.wait(lock, [this] { return !remote_que.empty() || _stop.load(); });
      try {
        if (!remote_que.empty()) {
          std::pair<std::string, std::string> task = remote_que.front();
          remote_que.pop();

          sdk.get(task.first, task.second);
        }
      } catch (std::exception& e) {
        LOG(ERROR) << "Error thread_remote_file_download SDK::get";
      }

      if (_stop.load() && remote_que.empty()) break;
    }
  }
}  // namespace spkdfs

#ifndef DAEMON_UPLOAD_H
#define DAEMON_UPLOAD_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "client/config.h"
#include "client/daemon_upload_queue.h"
#include "client/sdk.h"

namespace spkdfs {

  class DaemonUpload {
    friend class DaemonDownload;

  private:
    std::atomic<bool> _stop;
    size_t filesize_boundary;

    std::mutex _mtx_time;
    std::chrono::system_clock::time_point last_small_file_upload;  // 上次主动聚合上传时间点

    std::unique_ptr<std::thread> small_file_upload_thread_ptr;
    std::unique_ptr<std::thread> big_file_upload_thread_ptr;
    std::unique_ptr<IntervalTimer> aggragate_timer;

    /* 小文件: */
    UploadQueue small_file_que;
    /* 大文件: 采用条件变量,当rpc.put大文件时,加入队列 并通知 线程执行 */
    UploadQueue big_file_que;

    std::mutex _mtx_small_file;
    std::condition_variable _cv_small_file;

    std::mutex _mtx_big_file;
    std::condition_variable _cv_big_file;
    SDK& sdk;

  public:
    DaemonUpload(size_t filesize_boundary, SDK& sdk);
    ~DaemonUpload();
    void stop();

    /* 判断大小文件，加入各自队列处理线程 */
    void upload(std::string local_path, std::string remote_path,
                std::string storage_type = DEFAULT_STORAGE_TYPE);
    /* fsync 刷新小文件直接上传 */
    bool fsync(const std::string& local_path, const std::string& remote_path);
    /* ls 直接查询远端 */
    std::string ls(const std::string &path);
    /* rm 取消小文件上传 */
    bool rm(const std::string &path);
    /* debug */
    void debug();
    /* 大文件线程处理函数 */
    void thread_big_file_upload();
    /* 小文件线程处理函数 */
    void thread_small_file_upload();
    /* 小文件聚合逻辑 */
    void small_file_aggregate_upload(std::vector<std::shared_ptr<UploadTask>> &agg_batch_queue);
    /* 定时聚合线程 */
    void thread_timed_aggragate_upload();
  };
}  // namespace spkdfs
#endif
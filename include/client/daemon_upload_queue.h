#ifndef SPKDFS_DAEMON_UPLOAD_QUEUE_H
#define SPKDFS_DAEMON_UPLOAD_QUEUE_H
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "client/daemon_upload_task.h"
namespace spkdfs {

  class UploadQueue {
  public:
    std::mutex _mtx_map;
    std::mutex _mtx_que;

    typedef std::function<bool(std::shared_ptr<UploadTask> task_ptr)> check_func;
    UploadQueue(size_t capacity = -1);
    std::shared_ptr<UploadTask> pop_front();
    void push_back(std::shared_ptr<UploadTask> task);
    std::vector<std::shared_ptr<UploadTask>> get_until_size(size_t size);
    size_t current_all_smallfile_size();
    bool empty();
    void rm(const std::string& path);
    std::shared_ptr<UploadTask> find_in_map(const std::string &path);
    std::vector<std::shared_ptr<UploadTask>> find_dir_in_map(const std::string &path);
    std::optional<std::shared_ptr<UploadTask>> exist_in_que(const std::string& local_path,
                                                            const std::string& remote_path);
    void change_status(const std::string& path, UploadStatus status);
    void rm_multi(std::vector<std::shared_ptr<UploadTask>>& removed_task);
    std::optional<std::shared_ptr<UploadTask>> ls(const std::string& path);
    friend std::ostream& operator<<(std::ostream& os, UploadQueue& uploadQueue);

  private:
    size_t _capacity;                              // 队列的最大容量
    std::deque<std::shared_ptr<UploadTask>> _que;  // 存储键值对的队列
    std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<UploadTask>>>
        _map;  // 键到队列元素迭代器的映射
  };

}  // namespace spkdfs
#endif
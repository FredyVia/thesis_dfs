#include "client/daemon_upload_queue.h"

#include <atomic>

#include "client/daemon_upload_task.h"
namespace spkdfs {

  UploadQueue::UploadQueue(size_t capacity) : _capacity(capacity) {
    if (capacity == -1) {
      _capacity = std::numeric_limits<size_t>::max();
    }
  }

  // 获取队列第一个键值对
  std::shared_ptr<UploadTask> UploadQueue::pop_front() {
    std::lock_guard<std::mutex> lock(_mtx_que);

    if (_que.empty()) {
      return nullptr;
    }
    auto ptr = _que.front();
    _que.pop_front();
    return ptr;
  }

  // 插入键值对到队列的最后
  void UploadQueue::push_back(std::shared_ptr<UploadTask> task) {
    std::scoped_lock lock(_mtx_que, _mtx_map);

    if (_que.size() >= _capacity) {
      throw std::overflow_error("full");
    }
    _que.push_back(task);
    fs::path p(task->remote_path);
    std::string parent = p.parent_path().string();
    std::string name = p.filename().string();
    _map[parent][name] = task;
    // 更新task状态 -> WAITING
    task->uploadStatus = WAITING;
    task->belong_que = this; // 因为fsync的时候, 会更换队列, 所以要修改所属队列
  }

  std::vector<std::shared_ptr<UploadTask>> UploadQueue::get_until_size(size_t size) {
    std::lock_guard<std::mutex> lock(_mtx_que);
    std::vector<std::shared_ptr<UploadTask>> ret;
    size_t sum = 0;
    while (!_que.empty() && sum + _que.front()->size < size) {
      sum += _que.front()->size;
      ret.push_back(_que.front());
      _que.pop_front();
    }
    return ret;
  }

  size_t UploadQueue::current_all_smallfile_size() {
    std::lock_guard<std::mutex> lock(_mtx_que);
    size_t allsize = 0;
    std::for_each(_que.begin(), _que.end(), [&allsize](const std::shared_ptr<UploadTask> task_ptr) {
      allsize += task_ptr->size;
    });
    return allsize;
  }

  /* 查询当前_map或者_que中 是否存在path路径文件 */
  std::optional<std::shared_ptr<UploadTask>> UploadQueue::ls(const std::string& path) {
    fs::path p(path);
    std::string parent = p.parent_path().string();
    std::string name = p.filename().string();
    {
      std::lock_guard<std::mutex> lock(_mtx_que);
      for (auto it = _que.begin(); it != _que.end(); it++) {
        if ((*it)->remote_path == path) return *it;
      }
    }

    // _que中没有找到 _map中可能存在
    std::lock_guard<std::mutex> lock(_mtx_map);
    auto it = _map.find(parent);
    if (it == _map.end()) {
      return std::nullopt;
    }
    auto it2 = it->second.find(name);
    if (it2 == it->second.end()) {
      return std::nullopt;
    }
    {
      /* Cancel说明远端也不存在 那么get都从Daemon拿到 */
      std::lock_guard<std::mutex> lock(it2->second->_mtx);
      if (it2->second->uploadStatus == UploadStatus::CANCELED) return std::nullopt;
      return std::make_optional(it2->second);
    }
  }

  bool UploadQueue::empty() {
    std::lock_guard<std::mutex> lock(_mtx_que);
    return _que.empty();
  }

  std::shared_ptr<UploadTask> UploadQueue::find_in_map(const std::string& path) {
    fs::path p(path);
    std::string parent = p.parent_path().string();
    std::string name = p.filename().string();
    std::lock_guard<std::mutex> lock(_mtx_map);
    auto it = _map.find(parent);
    if (it == _map.end()) {
      return nullptr;
    }
    auto it2 = it->second.find(name);
    if (it2 == it->second.end()) {
      return nullptr;
    }
    return it2->second;
  }

  std::vector<std::shared_ptr<UploadTask>> UploadQueue::find_dir_in_map(const std::string &path) {
    std::vector<std::shared_ptr<UploadTask>> entry;
    std::lock_guard<std::mutex> lock(_mtx_map);
    auto it = _map.find(path);
    if (it == _map.end()) {
      return entry;
    }
    // 遍历_map生成entry
    for (const auto& [filename, task] : it->second) {
      std::lock_guard<std::mutex> tlock(task->_mtx);
      entry.push_back(task);
    }

    return entry;
  }

  /* 判断 <local_path, remote_path> 任务是否存在于_que队列中, 若存在就弹出队列 */
  std::optional<std::shared_ptr<UploadTask>> UploadQueue::exist_in_que(
      const std::string& local_path, const std::string& remote_path) {
    std::lock_guard<std::mutex> lock(_mtx_que);
    for (auto it = _que.begin(); it != _que.end(); it++) {
      if ((*it)->local_path == local_path && (*it)->remote_path == remote_path) {
        std::shared_ptr<UploadTask> tmp = *it;
        _que.erase(it); // 将该任务从小文件队列中删除
        return tmp;
      }
    }
    return std::nullopt;
  }

  /* 更新_map中path文件状态为 status */
  void UploadQueue::change_status(const std::string& path, UploadStatus status) {
    fs::path p(path);
    std::string parent = p.parent_path().string();
    std::string name = p.filename().string();
    std::lock_guard<std::mutex> lock(_mtx_map);
    auto it = _map.find(parent);
    if (it == _map.end()) {
      return;
    }
    auto it2 = it->second.find(name);
    if (it2 == it->second.end()) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(it2->second->_mtx);
      it2->second->uploadStatus = status;
    }
  }

  void UploadQueue::rm(const std::string& path) {
    fs::path p(path);
    std::string parent = p.parent_path().string();
    std::string name = p.filename().string();
    std::lock_guard<std::mutex> lock(_mtx_map);
    auto it = _map.find(parent);
    if (it == _map.end()) {
      return;
    }
    auto it2 = it->second.find(name);
    if (it2 == it->second.end()) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(it2->second->_mtx);
      // 如果是META_UPLOADING, 不取消
      if (it2->second->uploadStatus == UploadStatus::WAITING
          || it2->second->uploadStatus == UploadStatus::DATA_UPLOADING) {
        it2->second->uploadStatus = UploadStatus::CANCELED;
      }
    }
  }

  /* 从_map中删除多个task */
  void UploadQueue::rm_multi(std::vector<std::shared_ptr<UploadTask>>& removed_task) { return; }

  // 为了支持直接通过<<输出KVQueue，重载<<运算符
  std::ostream& operator<<(std::ostream& os, UploadQueue& uploadQueue) {
    std::lock_guard<std::mutex> lock(uploadQueue._mtx_que);
    if (uploadQueue._que.empty()) {
      os << "[]" << std::endl;
    } else {
      os << "[" << std::endl;
      for (const auto& item : uploadQueue._que) {
        os << "\t<local_path=" << item->local_path << ", remote_path=" << item->remote_path
          << ", size=" << item->size << ">" << std::endl;
      }
      os << "]" << std::endl;
    }
    return os;
  }
}  // namespace spkdfs
#include "client/daemon_upload.h"

#include <glog/logging.h>

#include <filesystem>
#include <memory>

#include "client/config.h"
#include "client/sdk.h"
#include "common/agg_inode.h"
using namespace std;

namespace spkdfs {
  DaemonUpload::DaemonUpload(size_t fs_b, SDK& sdk) : filesize_boundary(fs_b), sdk(sdk) {
    LOG(INFO) << "DaemonUpload::DaemonUpload() ::time_point = "
              << last_small_file_upload.time_since_epoch().count()
              << " | FLAGS_agg_size_boundary = " << FLAGS_agg_size_boundary;

    _stop.store(false);
    small_file_upload_thread_ptr = make_unique<std::thread>([this]() { thread_big_file_upload(); });
    big_file_upload_thread_ptr = make_unique<std::thread>([this]() { thread_small_file_upload(); });
    /* IntervalTimer 析构函数回收线程 */
    aggragate_timer = std::make_unique<IntervalTimer>(
        FLAGS_agg_timer_interval, [this]() { thread_timed_aggragate_upload(); },
        [](const void* args) {
          throw std::runtime_error(std::string("DaemonUpload::thread_timed_aggragate_upload() END")
                                   + (const char*)(args));
        });
  }

  void DaemonUpload::stop() {
    _stop.store(true);
    small_file_upload_thread_ptr->join();
    big_file_upload_thread_ptr->join();
  }

  void DaemonUpload::upload(std::string local_path, std::string remote_path,
                            std::string storage_type) {
    if (_stop.load()) {
      throw runtime_error("daemon is going to quit");
    }
    size_t size = fs::file_size(local_path);
    std::shared_ptr<UploadTask> upload_task_ptr
        = std::make_shared<UploadTask>(local_path, remote_path, size, storage_type,
                                       size <= filesize_boundary ? &small_file_que : &big_file_que);

    if (size <= filesize_boundary) {
      LOG(INFO) << "small_file: " << local_path << " | " << size;
      {
        std::lock_guard<std::mutex> lock(_mtx_small_file);
        small_file_que.push_back(upload_task_ptr);
      }
      _cv_small_file.notify_one();
    } else {
      LOG(INFO) << "big_file: " << local_path << " | " << size;
      {
        std::lock_guard<std::mutex> lock(_mtx_big_file);
        big_file_que.push_back(upload_task_ptr);
      }
      _cv_big_file.notify_one();
    }
  }

  /* 提交单个 smallfile 队列中上传任务 */
  bool DaemonUpload::fsync(const std::string& local_path, const std::string& remote_path) {
    std::function<void(UploadQueue&)> func = [](UploadQueue& que) {
      std::cout << que;
      return;
    };
    LOG(INFO) << "BEFORE SYNC";
    func(this->small_file_que);
    // 1. 查找 <local_path, remote_path> 是否存在
    std::optional<std::shared_ptr<UploadTask>> task_ret
        = small_file_que.exist_in_que(local_path, remote_path);
    if (task_ret) {
      LOG(INFO) << "EXIST IN file_queue!!!";
      // 2. 删除任务 等价于 加入大文件队列
      {
        std::lock_guard<std::mutex> lock(_mtx_big_file);
        task_ret.value()->belong_que = &big_file_que;
        big_file_que.push_back(task_ret.value());
      }
      _cv_big_file.notify_one();

      LOG(INFO) << "AFTER SYNC";
      func(this->small_file_que);
      return true;
    } else {  // 不存在
      LOG(INFO) << "NOT EXIST IN file_queue!!!";
      func(this->small_file_que);
      return false;
    }
  }

  std::string DaemonUpload::ls(const std::string &path) {
    Inode inode = sdk.ls(path);
    // 合并Daemon中的上传任务: 对比上传任务的父目录路径和path是否相同
    std::vector<std::shared_ptr<UploadTask>> cur_upload_entry = this->small_file_que.find_dir_in_map(path);
    for (auto &task: cur_upload_entry) {
      std::lock_guard<std::mutex> lock(task->_mtx);
      if (task->uploadStatus == UploadStatus::FINISHED
          || task->uploadStatus == UploadStatus::CANCELED)
          continue;
      fs::path p(task->remote_path);
      inode.sub.push_back(p.filename().string());
    }

    return inode.value();
  }

  /* rm 经过Daemon的目的就是判断是否可以取消任务的上传
      1. 对于大文件, 因为大文件在队列中一直不断上传,
          那么rm直接等上传完毕, 直接通过sdk.rm()删除远端
      2. 对于小文件, 因为在队列中等待，需要判断任务的状态,
         根据任务的状态先在, 本地Daemon中进行rm: 直接取消上传
  */
  bool DaemonUpload::rm(const std::string &path) {
    LOG(INFO) << "DaemonUpload::rm";

    std::shared_ptr<UploadTask> task_ptr;
    /* 在大文件队列中存在 */
    if (task_ptr = big_file_que.find_in_map(path)) {
      LOG(INFO) << "rm in big_file_que";

      while (true) {
        std::lock_guard<std::mutex> lock(task_ptr->_mtx);
        if (task_ptr->uploadStatus == FINISHED) break;
      }

      sdk.rm(path);
      
      return true;
    }
    /* 在小文件队列中存在 */
    if (task_ptr = small_file_que.find_in_map(path)) {
      LOG(INFO) << "task_ptr small_file_que";

      small_file_que.rm(path);

      return true;
    }

    /* 两个队列中都不存在, 那么去远端删除 */
    sdk.rm(path);

    return true;
  }

  void DaemonUpload::debug() {
    std::cout << small_file_que;
    return;
  }

  void DaemonUpload::thread_big_file_upload() {
    while (!_stop.load()) {
      std::unique_lock<std::mutex> lock(_mtx_big_file);  // 锁住互斥量
      _cv_big_file.wait(
          lock, [this] { return !big_file_que.empty() || _stop.load(); });  // 等待条件变量的通知
      if (!big_file_que.empty()) {
        std::shared_ptr<UploadTask> task_ptr = big_file_que.pop_front();

        LOG(INFO) << "task.uploadStatus: " << task_ptr->uploadStatus << " |" << task_ptr->local_path
                  << " |" << task_ptr->remote_path << " |" << task_ptr->storage_type;

        sdk.put(task_ptr);
        // shared_ptr, pop_front()后自动删除
      }

      if (_stop.load() && big_file_que.empty()) {
        break;  // 生产者完成且队列为空时，消费者退出
      }
    }
  }

  void DaemonUpload::thread_small_file_upload() {
    while (!_stop.load()) {
      std::unique_lock<std::mutex> lock(_mtx_small_file);
      _cv_small_file.wait(lock, [this] { return !small_file_que.empty() || _stop.load(); });
      if (!small_file_que.empty()) {
        /* 当前队列中文件总大小达到聚合标准 */
        if (small_file_que.current_all_smallfile_size() >= FLAGS_agg_size_boundary) {
          std::vector<std::shared_ptr<UploadTask>> agg_batch_queue
              = small_file_que.get_until_size(FLAGS_agg_size_boundary);
          small_file_aggregate_upload(agg_batch_queue);
        }
      }

      if (_stop.load() && small_file_que.empty()) {
        break;
      }
    }
  }

  void DaemonUpload::small_file_aggregate_upload(std::vector<std::shared_ptr<UploadTask>> &agg_batch_queue) {
    // (批量)聚合上传逻辑
    AggInode aggInode;
    std::string aggData;
    size_t currentPos = 0;

    for (const auto& task_ptr : agg_batch_queue) {
      std::ifstream ifs(task_ptr->local_path, std::ios::binary | std::ios::ate);
      if (!ifs) throw std::runtime_error("Failed to open file: " + task_ptr->local_path);

      size_t fileSize = ifs.tellg();
      ifs.seekg(0);

      std::string fileData(fileSize, '\0');
      ifs.read(&fileData[0], fileSize);
      ifs.close();

      // 组合本地多个小文件数据到std::string中
      aggData.append(fileData);

      aggInode.files.emplace_back(task_ptr->remote_path, task_ptr->size,
                                  std::make_pair<int, int>(currentPos, currentPos + fileSize));
      currentPos += fileSize;

      // 添加每个小文件 task 中的信息
      task_ptr->aggInode = &aggInode;
      task_ptr->aggData = &aggData;
    }
    // EQ(currentPos, aggData.size()) 应该是相等的
    aggInode.aggregate_size = aggData.size();

    sdk.putAggFile(aggInode, aggData, FLAGS_agg_type);

    // 刷新更新时间点
    {
      std::lock_guard<std::mutex> timelock(_mtx_time);
      last_small_file_upload = std::chrono::system_clock::now();
    }
    return;
  }

  /* 定时聚合所有文件上传 */
  void DaemonUpload::thread_timed_aggragate_upload() {
    std::chrono::system_clock::time_point last_timepoint;
    {
      std::lock_guard<std::mutex> lock(_mtx_time);
      last_timepoint = last_small_file_upload;
    }
    auto now_timepoint = std::chrono::system_clock::now();
    uint dura
        = std::chrono::duration_cast<std::chrono::seconds>(now_timepoint - last_timepoint).count();
    // 本次定时器不聚合上传原因：距离上次聚合的时间小于定时器间隔 FLAGS_agg_timer_interval
    if (dura < FLAGS_agg_timer_interval) {
      LOG(INFO)
          << "DaemonUpload::thread_timed_aggragate_upload():: don't need this timed_upload. DURA = "
          << dura;
      return;
    }
    LOG(INFO) << "DaemonUpload::thread_timed_aggragate_upload():: do this timed_upload. dura = " << dura;
    // 对小文件队列中文件进行一次总大小 FLAGS_agg_size_boundary 的聚合
    std::vector<std::shared_ptr<UploadTask>> agg_batch_queue
        = small_file_que.get_until_size(FLAGS_agg_size_boundary);
    if (agg_batch_queue.size() > 0) small_file_aggregate_upload(agg_batch_queue);
    return;
  }

  DaemonUpload::~DaemonUpload() {
    LOG(INFO) << "DaemonUpload::~DaemonUpload()";
    stop();
  }
}  // namespace spkdfs
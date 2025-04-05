#ifndef AGG_INODE_H
#define AGG_INODE_H

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
namespace spkdfs {
  class SnowflakeIDGenerator {
  public:
    SnowflakeIDGenerator(int machineId) : machineId_(machineId) {
      // 时间戳起始基准值（自定义起始时间）
      epoch_ = 1609459200000;  // 2021-01-01 00:00:00 UTC
      sequence_ = 0;
      lastTimestamp_ = -1;
    }

    std::string generateID() {
      long timestamp = currentMillis();
      if (timestamp == lastTimestamp_) {
        sequence_ = (sequence_ + 1) & sequenceMask_;
        if (sequence_ == 0) {
          timestamp = waitForNextMillis(lastTimestamp_);
        }
      } else {
        sequence_ = 0;
      }

      lastTimestamp_ = timestamp;

      long id
          = ((timestamp - epoch_) << timestampShift_) | (machineId_ << machineIdShift_) | sequence_;

      std::ostringstream oss;
      oss << std::hex << id;  // 返回十六进制字符串
      return oss.str();
    }

  private:
    long currentMillis() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now().time_since_epoch())
          .count();
    }

    long waitForNextMillis(long lastTimestamp) {
      long timestamp = currentMillis();
      while (timestamp <= lastTimestamp) {
        timestamp = currentMillis();
      }
      return timestamp;
    }

    long epoch_;                    // 起始时间
    int machineId_;                 // 机器ID
    const int sequenceBits_ = 12;   // 序列号的位数
    const int machineIdBits_ = 10;  // 机器ID的位数
    const int timestampBits_ = 41;  // 时间戳的位数

    const long sequenceMask_ = (1 << sequenceBits_) - 1;
    const long machineIdShift_ = sequenceBits_;
    const long timestampShift_ = sequenceBits_ + machineIdBits_;

    std::atomic<long> sequence_;
    long lastTimestamp_;
  };

  class UploadTask;

  struct FileInfo {
    FileInfo() = default;
    FileInfo(const std::string& path, uint64_t size, const std::pair<int, int>& pos,
             std::shared_ptr<UploadTask> task = nullptr)
        : path(path), size(size), pos(pos), task_ptr(task) {}
    std::string path;         // 小文件路径,dst
    uint64_t size;            // 小文件大小
    std::pair<int, int> pos;  // 小文件在聚合文件中的起始位置和结束位置
    std::shared_ptr<UploadTask> task_ptr;

    // 序列化
    friend void to_json(nlohmann::json& j, const FileInfo& file);

    // 反序列化
    friend void from_json(const nlohmann::json& j, FileInfo& file);
  };

  class AggInode {
  public:
    friend void to_json(nlohmann::json& j, const AggInode& inode);
    friend void from_json(const nlohmann::json& j, AggInode& inode);
    AggInode() = default;
    // Constructor with all parameters
    AggInode(const std::string& newId, const std::vector<FileInfo>& newFiles, uint64_t newSize,
             const std::vector<bool> newValid, int newValidCount)
        : id(newId),
          files(newFiles),
          aggregate_size(newSize),
          valid(newValid),
          valid_count(newValidCount) {}

    std::string getId() const { return id; }

    void setId(const std::string& newId) { id = newId; }

    std::vector<FileInfo> getFiles() const { return files; }

    FileInfo getFile(int index) const { return files[index]; }

    void setFiles(const std::vector<FileInfo>& newFiles) { files = newFiles; }

    uint64_t getAggregateSize() const { return aggregate_size; }

    void setAggregateSize(uint64_t newSize) { aggregate_size = newSize; }

    std::string getFilePath(int index) const {
      if (index >= 0 && index < files.size()) {
        return files[index].path;
      }
      return "";
    }

    int getFileSize(int index) const {
      if (index >= 0 && index < files.size()) {
        return files[index].size;
      }
      return -1;
    }
    std::pair<int, int> getFilePos(int index) {
      if (index >= 0 && index < files.size()) {
        return files[index].pos;
      }
      return {-1, -1};
    }

    void setFileUnValid(int index) {
      if (index >= 0 && index < valid.size()) {
        valid[index] = false;
        valid_count--;
      }
    }
    std::string value() const {
      nlohmann::json j;
      to_json(j, *this);
      return j.dump();
    }

  public:
    std::string id;               // 唯一标识符
    std::vector<FileInfo> files;  // 小文件列表
    uint64_t aggregate_size;      // 聚合后大文件的总大小
    std::vector<bool> valid;      // 小文件是否有效
    int valid_count;              // 有效小文件数量
  };

}  // namespace spkdfs

#endif  // !AGG_INODE_H

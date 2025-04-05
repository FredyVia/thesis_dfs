#ifndef PATHLOCK_H
#define PATHLOCK_H

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
namespace spkdfs {
  class PathLocks {
  private:
    std::unordered_map<std::string, std::pair<bool, std::unique_ptr<std::shared_mutex>>> locks;
    std::shared_mutex locks_mutex;  // 用于保护锁映射的互斥量

  public:
    void read_lock(const std::string& path);
    void write_lock(const std::string& path);
    void unlock(const std::string& path);
  };
}  // namespace spkdfs
#endif
#include "common/pathlocks.h"

#include <glog/logging.h>

namespace spkdfs {
  void PathLocks::read_lock(const std::string& key) {
    std::unique_lock<std::shared_mutex> locallock(locks_mutex);
    if (locks.find(key) == locks.end()) {
      locks[key] = make_pair(false, std::make_unique<std::shared_mutex>());
    }
    locallock.unlock();

    locks[key].second->lock_shared();
  }

  void PathLocks::write_lock(const std::string& key) {
    std::unique_lock<std::shared_mutex> locallock(locks_mutex);
    if (locks.find(key) == locks.end()) {
      locks[key] = make_pair(true, std::make_unique<std::shared_mutex>());
    }
    locallock.unlock();
    locks[key].second->lock();
  }

  void PathLocks::unlock(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(locks_mutex);
    auto it = locks.find(key);
    if (it != locks.end()) {
      if (it->second.first)
        it->second.second->unlock();
      else {
        it->second.second->unlock_shared();
      }
    }
  }
}  // namespace spkdfs
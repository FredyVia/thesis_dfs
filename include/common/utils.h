#ifndef UTILS_H
#define UTILS_H
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/node.h"
namespace fs = std::filesystem;

// 删除默认拷贝和operator=
#define NON_COPYABLE(class_name)          \
  class_name(const class_name&) = delete; \
  class_name operator=(const class_name&) = delete

#define NON_MOVEABLE(class_name)     \
  class_name(class_name&&) = delete; \
  class_name operator=(class_name&&) = delete

#define NON_MOVE_AND_COPYABLE(class_name) \
  NON_COPYABLE(class_name);               \
  NON_MOVEABLE(class_name)
namespace spkdfs {

  using RunFuncType = std::function<void()>;
  using TimeoutFuncType = std::function<void(const void*)>;
  class IntervalTimer {
  private:
    uint interval = 3;
    std::mutex mtx;
    std::condition_variable cv;

    bool running = true;
    std::shared_ptr<std::thread> t = nullptr;
    RunFuncType runFunc;
    TimeoutFuncType timeoutFunc;

  public:
    void stop();
    IntervalTimer(uint interval, const RunFuncType& runFunc,
                  const TimeoutFuncType& timeoutFunc = nullptr);
    ~IntervalTimer();
    class TimeoutException : std::exception {
    private:
      const std::string& errorinfo;
      const void* data;

    public:
      TimeoutException(const std::string& errorinfo, const void* data)
          : errorinfo(errorinfo), data(data) {}
      inline virtual const char* what(void) const noexcept override { return errorinfo.c_str(); }
      inline const void* get_data() const { return data; }
    };
  };

  std::string get_my_ip(const std::vector<Node>& vec);
  inline int align_index_up(int n, int alignment) { return n / alignment + 1; };
  inline int align_index_down(int n, int alignment) { return n / alignment; };
  inline int align_up(int n, int alignment) { return (n + alignment - 1) / alignment; };
  inline int align_down(int n, int alignment) { return align_index_down(n, alignment); };
  void mkdir_f(const std::string& dir);
  std::string cal_sha256sum(const std::string&);
  std::string cal_md5sum(const std::string&);
  std::string simplify_path(const std::string&);
  std::string read_file(const std::string& path);
  std::string get_parent_path(const std::string& path);
  std::vector<std::string> list_dir(const std::string& s);
  void clear_dir(const std::string& path);

  extern int64_t time_shifting;
  inline uint64_t _get_time() { return time(NULL); }
  inline uint64_t get_time() { return time_shifting + _get_time(); }
  inline void set_time(uint64_t expected) { time_shifting = (int64_t)expected - _get_time(); }

#define RETRY_WITH_CONDITION_TIMES(statement, fail_condition, retry_times) \
  do {                                                                     \
    int retries = 0;                                                       \
    while (retries < (retry_times)) {                                      \
      statement;                                                           \
      ++retries;                                                           \
      if ((fail_condition)) {                                              \
        LOG(ERROR) << "Retry " << retries << " failed.";                   \
      } else {                                                             \
        break;                                                             \
      }                                                                    \
    }                                                                      \
    if (retries == (retry_times)) {                                        \
      LOG(ERROR) << "All " << (retry_times) << " retries failed.";         \
    }                                                                      \
  } while (0)

#define RETRY_WITH_CONDITION(statement, fail_condition) \
  RETRY_WITH_CONDITION_TIMES(statement, fail_condition, 3)

  std::string parent_dir_path(const std::string& path);
}  // namespace spkdfs
#endif
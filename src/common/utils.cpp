#include "common/utils.h"

#include <arpa/inet.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <cryptopp/md5.h>
#include <cryptopp/sha.h>
#define DBG_MACRO_NO_WARNING
#include <dbg-macro/dbg.h>
#include <glog/logging.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <time.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

namespace spkdfs {
  using namespace std;
  int64_t time_shifting = 0;

  IntervalTimer::IntervalTimer(uint interval, const RunFuncType &runFunc,
                               const TimeoutFuncType &timeoutFunc)
      : interval(interval), runFunc(runFunc), timeoutFunc(timeoutFunc) {
    t = make_shared<std::thread>([this]() {
      try {
        std::unique_lock<std::mutex> lock(this->mtx);
        while (running) {
          // wait_for(lock, interval, pred);
          // wait when pred is false, go through when pred is true
          this->cv.wait_for(lock, std::chrono::seconds(this->interval));
          if (running) this->runFunc();
        }
      } catch (const TimeoutException &e) {
        if (this->timeoutFunc != nullptr) this->timeoutFunc(e.get_data());
      }
    });
  }

  void IntervalTimer::stop() {
    {
      std::lock_guard<std::mutex> lock(mtx);
      running = false;
    }
    cv.notify_one();
    if (t->joinable()) t->join();
  }

  IntervalTimer::~IntervalTimer() { stop(); }

  std::set<std::string> get_all_ips() {
    std::set<std::string> res;
    struct ifaddrs *interfaces = nullptr;
    struct ifaddrs *addr = nullptr;
    void *tmpAddrPtr = nullptr;

    if (getifaddrs(&interfaces) == -1) {
      throw runtime_error("get_all_ips getifaddrs failed");
    }

    LOG(INFO) << "get_all_ips:";
    for (addr = interfaces; addr != nullptr; addr = addr->ifa_next) {
      if (addr->ifa_addr == nullptr) {
        continue;
      }
      if (addr->ifa_addr->sa_family == AF_INET) {  // check it is IP4
        // is a valid IP4 Address
        tmpAddrPtr = &((struct sockaddr_in *)addr->ifa_addr)->sin_addr;
        char addressBuffer[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
        LOG(INFO) << addr->ifa_name << " IP Address: " << addressBuffer;
        res.insert(string(addressBuffer));
        // } else if (addr->ifa_addr->sa_family == AF_INET6) {  // not support IP6 curr
        //   // is a valid IP6 Address
        //   tmpAddrPtr = &((struct sockaddr_in6 *)addr->ifa_addr)->sin6_addr;
        //   char addressBuffer[INET6_ADDRSTRLEN];
        //   inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
        //   std::cout << addr->ifa_name << " IP Address: " << addressBuffer;
      }
    }

    if (interfaces) {
      freeifaddrs(interfaces);
    }
    return res;
  }

  std::string get_my_ip(const std::vector<Node> &vec) {
    set<string> nodes;
    for (const auto &node : vec) {
      nodes.insert(node.ip);
    }
    set<string> local = get_all_ips();
    std::set<string> intersection;
    // 使用 std::set_intersection 查找交集
    std::set_intersection(nodes.begin(), nodes.end(), local.begin(), local.end(),
                          std::inserter(intersection, intersection.begin()));
    LOG(INFO) << "using ip";
    for (auto &str : intersection) {
      LOG(INFO) << str << ", ";
    }
    if (intersection.size() == 0) {
      LOG(ERROR) << "cluster ips: ";
      dbg::pretty_print(LOG(ERROR), nodes);
      LOG(ERROR) << "local ips: ";
      dbg::pretty_print(LOG(ERROR), local);
      throw runtime_error("cannot find equal ips");
    }
    return *(intersection.begin());
  }

  void mkdir_f(const std::string &dir) {
    filesystem::path dirPath{dir};
    error_code ec;  // 用于接收错误码

    // 创建目录，包括所有必要的父目录
    if (!filesystem::create_directories(dirPath, ec)) {
      if (ec) {
        LOG(ERROR) << "Error: Unable to create directory " << dir << ". " << ec.message();
        throw runtime_error("mkdir error: " + dir + ec.message());
      }
    }
  }

  std::string cal_sha256sum(const std::string &data) {
    string sha256sum;
    CryptoPP::SHA256 sha256;
    // no need to delete these new object(CryptoPP manage these memory)
    CryptoPP::StringSource ss(
        data, true,
        new CryptoPP::HashFilter(sha256,
                                 new CryptoPP::HexEncoder(new CryptoPP::StringSink(sha256sum))));
    return sha256sum;
  }

  std::string cal_md5sum(const std::string &data) {
    string md5sum;
    CryptoPP::Weak1::MD5 md5;
    // no need to delete these new object(CryptoPP manage these memory)
    CryptoPP::StringSource ss(
        data, true,
        new CryptoPP::HashFilter(md5, new CryptoPP::HexEncoder(new CryptoPP::StringSink(md5sum))));
    return md5sum;
  }

  std::string simplify_path(const std::string &path) {
    // 假设这是你的原始路径
    // 使用filesystem库规范化路径
    fs::path fs_path(path);
    string res = fs_path.lexically_normal();
    if (res.size() > 1 && res.back() == '/') {
      res.pop_back();
    }
    return res;
  }

  std::string read_file(const std::string &path) {
    int filesize = fs::file_size(path);
    string block;
    block.resize(filesize);
    ifstream ifile(path, std::ios::binary);
    if (!ifile) {
      LOG(FATAL) << "Failed to open file for reading." << path << endl;
      throw std::runtime_error("openfile error:" + path);
    }
    if (!ifile.read(&block[0], filesize)) {
      LOG(FATAL) << "Failed to read file content." << endl;
      throw std::runtime_error("readfile error:" + path);
    }
    ifile.close();
    return block;
  }

  std::string get_parent_path(const std::string &path) {
    return fs::path(path).parent_path().string();
  }

  vector<std::string> list_dir(const std::string &dst) {
    vector<std::string> res;
    for (const auto &entry : fs::directory_iterator(dst)) {
      const auto name = entry.path().filename().string();
      res.push_back(name);
    }
    return res;
  }

  void clear_dir(const string &path) {
    try {
      if (fs::exists(path) && fs::is_directory(path)) {
        for (const auto &entry : fs::directory_iterator(path)) {
          fs::remove_all(entry.path());
        }
      }
    } catch (const fs::filesystem_error &e) {
      std::cerr << "Error clearing directory: " << e.what() << '\n';
    }
  }

  std::string parent_dir_path(const std::string &path) {
    return fs::path(path).parent_path().string();
  }
}  // namespace spkdfs
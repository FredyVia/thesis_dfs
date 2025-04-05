#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <brpc/channel.h>

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "common/config.h"

namespace spkdfs {
  template <typename Stub> class ConnecttionPool {
  public:
    // 获取单例实例
    static ConnecttionPool& get_instance();
    // 获取或创建指定节点的 Stub
    std::shared_ptr<Stub> get_stub(const std::string& node);
    std::shared_ptr<Stub> get_stub();

  private:
    mutable std::shared_mutex mutex_stubs;  // 用于保护共享数据
    std::unordered_map<std::string, std::pair<brpc::Channel, std::shared_ptr<Stub>>> stubs;
    // 禁用拷贝构造函数和赋值运算符
    ConnecttionPool(const ConnecttionPool&) = delete;
    ConnecttionPool& operator=(const ConnecttionPool&) = delete;

    // 私有构造函数和析构函数
    ConnecttionPool() = default;
    ~ConnecttionPool() = default;
  };

  // 获取单例实例
  template <typename Stub> ConnecttionPool<Stub>& ConnecttionPool<Stub>::get_instance() {
    static ConnecttionPool<Stub> instance;  // 局部静态变量实现线程安全的单例
    return instance;
  }

  template <typename Stub> std::shared_ptr<Stub> ConnecttionPool<Stub>::get_stub() {
    if (stubs.empty()) return nullptr;
    return stubs.begin()->second.second;
  }

  template <typename Stub>
  std::shared_ptr<Stub> ConnecttionPool<Stub>::get_stub(const std::string& node) {
    {  // 尝试使用读锁快速检查
      std::shared_lock<std::shared_mutex> read_lock(mutex_stubs);
      auto it = stubs.find(node);
      if (it != stubs.end() && it->second.second) {
        return it->second.second;
      }
    }

    // 如果节点不存在或 Stub 尚未初始化，则升级为写锁
    std::unique_lock<std::shared_mutex> write_lock(mutex_stubs);
    auto& [channel, stub_ptr] = stubs[node];  // 获取引用，避免多次查找

    if (!stub_ptr) {
      // 初始化连接
      channel.Init(node.c_str(), &BRPC_CHANNEL_OPTIONS);
      try {
        stub_ptr = std::make_unique<Stub>(&channel);
      } catch (const std::exception& e) {
        stub_ptr.reset();  // 确保智能指针为空
        throw std::runtime_error("node not online: " + std::string(e.what()));
      }
    }
    return stub_ptr;
  }

}  // namespace spkdfs
#endif  // CONNECTION_POOL_H
#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H
#include <brpc/channel.h>
namespace spkdfs {

#define CLIENT_ASYNC_TIMEOUT_MS (7200000UL)    // 1000ms * 60 * 60 * 2 = 2 Hour 过期
#define LOCK_REFRESH_INTERVAL (60)
#define BRPC_STREAM_SLEEP_MICROSECOND (50000)
#define BRPC_STREAM_CHUNK_SIZE ((size_t)16 * 1024)
  // 定义一个静态常量 ChannelOptions 配置
  static const brpc::ChannelOptions BRPC_CHANNEL_OPTIONS = [] {
    brpc::ChannelOptions options;
    options.connect_timeout_ms = 500;  // 连接超时
    options.timeout_ms = 2000;         // 请求总超时时间
    options.max_retry = 3;             // 最大重试次数
    return options;
  }();
}  // namespace spkdfs
#endif
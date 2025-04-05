#ifndef STREAM_H
#define STREAM_H
#include <brpc/stream.h>

#include <functional>
#include <sstream>
#include <string>
namespace spkdfs {

  using StreamReceiverFunc = std::function<void(std::string data)>;
  class StreamReceiver : public brpc::StreamInputHandler {
    StreamReceiverFunc _lambda;
    std::atomic<bool> finished;

    std::stringstream _ss;

  public:
    StreamReceiver(const StreamReceiverFunc& lambda);
    ~StreamReceiver();
    inline bool is_finished() { return finished.load(); }

    virtual int on_received_messages(brpc::StreamId id, butil::IOBuf* const messages[],
                                     size_t size) override;
    virtual void on_idle_timeout(brpc::StreamId id) override;
    virtual void on_closed(brpc::StreamId id) override;
  };

  using StreamSenderFunc = std::function<void(brpc::StreamId stream_id)>;
  class StreamSender : public brpc::StreamInputHandler {
    StreamSenderFunc _lambda;
    std::atomic<bool> finished;

  public:
    StreamSender(const StreamSenderFunc& lambda);
    ~StreamSender();
    inline bool is_finished() { return finished.load(); }

    virtual int on_received_messages(brpc::StreamId id, butil::IOBuf* const messages[],
                                     size_t size) override;
    virtual void on_idle_timeout(brpc::StreamId id) override;
    virtual void on_closed(brpc::StreamId id) override;
  };
}  // namespace spkdfs
#endif
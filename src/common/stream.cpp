#include "common/stream.h"

#include <glog/logging.h>
namespace spkdfs {
  StreamReceiver::StreamReceiver(const StreamReceiverFunc& lambda) : _lambda(lambda) {}

  StreamReceiver::~StreamReceiver() { LOG(INFO) << "deconstructing StreamReceiver"; }

  int StreamReceiver::on_received_messages(brpc::StreamId id, butil::IOBuf* const messages[],
                                           size_t size) {
    for (size_t i = 0; i < size; ++i) {
      _ss << *messages[i];
    }
    return 0;
  }

  void StreamReceiver::on_idle_timeout(brpc::StreamId id) {
    LOG(ERROR) << "Stream=" << id << " has no data transmission for a while";
  }

  void StreamReceiver::on_closed(brpc::StreamId id) {
    LOG(INFO) << "Stream=" << id << " is closed";
    _lambda(_ss.str());
    finished.store(true);
  }

  StreamSender::StreamSender(const StreamSenderFunc& lambda) : _lambda(lambda) {}

  StreamSender::~StreamSender() { LOG(INFO) << "deconstructing StreamSender"; }

  int StreamSender::on_received_messages(brpc::StreamId id, butil::IOBuf* const messages[],
                                         size_t size) {
    LOG(INFO) << "on_received_messages";
    for (size_t i = 0; i < size; ++i) {
      LOG(INFO) << *messages[i];
    }
    _lambda(id);
    if (int status = brpc::StreamClose(id)) {
      LOG(ERROR) << "Stream Close failed, errorno: " << status;
    }

    return 0;
  }

  void StreamSender::on_idle_timeout(brpc::StreamId id) {
    LOG(ERROR) << "Stream=" << id << " has no data transmission for a while";
  }

  void StreamSender::on_closed(brpc::StreamId id) {
    LOG(INFO) << "Stream=" << id << " is closed";
    finished.store(true);
  }
}  // namespace spkdfs
#ifndef SERVICE_H
#define SERVICE_H
#include <string>

#include "common/common.pb.h"

namespace spkdfs {

  class CommonServiceImpl : public CommonService {
    void echo(::google::protobuf::RpcController* controller, const Request* request,
              CommonResponse* response, ::google::protobuf::Closure* done) override;
  };
  std::string to_string(const ErrorMessage& e);
}  // namespace spkdfs
#endif
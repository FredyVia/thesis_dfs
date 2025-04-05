#include "common/service.h"

#include <brpc/closure_guard.h>
#define DBG_MACRO_NO_WARNING
#include <dbg-macro/dbg.h>

#include <sstream>
#include <string>
namespace spkdfs {
  void CommonServiceImpl::echo(::google::protobuf::RpcController* controller,
                               const Request* request, CommonResponse* response,
                               ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    response->mutable_common()->set_success(true);
  }
  std::string to_string(const ErrorMessage& e) {
    std::ostringstream oss;
    dbg::pretty_print(oss, e.code());
    oss << "|" << e.message();
    return oss.str();
  }
}  // namespace spkdfs
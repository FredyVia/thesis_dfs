#ifndef EXCEPTION_H
#define EXCEPTION_H
#include <exception>
#include <memory>
#include <sstream>
#include <string>

#include "common/service.h"

namespace spkdfs {
  class MessageException : public std::exception {
    ErrorMessage errMsg;
    std::string s;

  public:
    MessageException(const ErrorMessage& e) : errMsg(e) { s = to_string(e); }
    MessageException(enum ERROR_CODE code, const std::string& info) {
      errMsg.set_code(code);
      errMsg.set_message(info);
      s = spkdfs::to_string(errMsg);
    }
    inline ErrorMessage errorMessage() const { return errMsg; }
    inline const char* what() const noexcept override { return s.c_str(); }
  };
}  // namespace spkdfs
#endif
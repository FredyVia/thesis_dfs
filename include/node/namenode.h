#ifndef NAMENODE_H
#define NAMENODE_H
#include <brpc/closure_guard.h>

#include <functional>
#include <memory>
#include <string>

#include "common/exception.h"
#include "common/namenode.pb.h"
#include "common/node.h"
#include "node/raft_nn.h"
#include "node/rocksdb.h"

namespace spkdfs {
  void fail_response(Response* response, const std::exception& e);
  void fail_response(Response* response, const MessageException& e);
  class RaftCommonClosure : public braft::Closure {
    google::protobuf::Closure* _done;

  public:
    Response* response;
    RaftCommonClosure(Response* response, google::protobuf::Closure* done)
        : response(response), _done(done) {}
    void Run() override {
      std::unique_ptr<RaftCommonClosure> self_guard(this);
      brpc::ClosureGuard done_guard(_done);
    }
    inline void succ_response() { response->set_success(true); }
    inline void fail_response(const MessageException& e) { spkdfs::fail_response(response, e); }
    inline void fail_response(const std::exception& e) { spkdfs::fail_response(response, e); }
  };

  class NamenodeServiceImpl : public NamenodeService {
    void check_leader(Response* response);

  public:
    NamenodeServiceImpl(const std::string& my_ip, RaftNN* nn_raft_ptr)
        : my_ip(my_ip), nn_raft_ptr(nn_raft_ptr) {}
    void ls(::google::protobuf::RpcController* controller, const CommonPathRequest* request,
            CommonPathResponse* response, ::google::protobuf::Closure* done) override;
    void mkdir(::google::protobuf::RpcController* controller, const CommonPathRequest* request,
               CommonResponse* response, ::google::protobuf::Closure* done) override;
    void rm(::google::protobuf::RpcController* controller, const CommonPathRequest* request,
            CommonResponse* response, ::google::protobuf::Closure* done) override;

    /**
     * @brief 客户端请求创建元数据服务
     * @param request: 请求包含文件路径和存储类型
     * @param response: 响应中包含成功与否信息和datanode
     */
    void put(::google::protobuf::RpcController* controller, const NNPutRequest* request,
             NNPutResponse* response, ::google::protobuf::Closure* done) override;
    void put_ok(::google::protobuf::RpcController* controller, const NNPutOKRequest* request,
                CommonResponse* response, ::google::protobuf::Closure* done) override;
    void get_master(::google::protobuf::RpcController* controller, const Request* request,
                    NNGetMasterResponse* response, ::google::protobuf::Closure* done) override;
    void update_lock(::google::protobuf::RpcController* controller, const NNLockRequest* request,
                     CommonResponse* response, ::google::protobuf::Closure* done) override;
    // TODO: 实现
    /**
     * @brief 客户端请求创建元数据服务，批量提交一组小文件
     * @param request: 请求包含文件路径和存储类型
     * @param response: 响应中包含成功与否信息和datanode
     */
    void put_batch(::google::protobuf::RpcController* controller,
                   const ::spkdfs::NNPutRequestBatch* request,
                   ::spkdfs::NNPutResponseBatch* response,
                   ::google::protobuf::Closure* done) override;

    /**
     * @brief 客户端请求创建元数据服务，批量提交一组小文件
     * @param request: 请求包含文件路径和存储类型
     * @param response: 响应中包含成功与否信息和datanode
     */
    void put_ok_batch(::google::protobuf::RpcController* controller,
                      const ::spkdfs::NNPutOKRequestBatch* request,
                      ::spkdfs::CommonResponse* response,
                      ::google::protobuf::Closure* done) override;

  private:
    std::string my_ip;
    // IsLeaderCallbackType isLeaderCallback;
    RaftNN* nn_raft_ptr;
  };
}  // namespace spkdfs
#endif
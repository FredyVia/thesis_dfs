#ifndef DATANODE_H
#define DATANODE_H
#include <brpc/channel.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/datanode.pb.h"
#include "common/node.h"
#include "common/stream.h"
#include "node/raft_dn.h"
#include "node/rocksdb.h"
namespace spkdfs {
  class DatanodeServiceImpl : public DatanodeService {
  public:
    DatanodeServiceImpl(const std::string& my_ip, RaftDN* dn_raft_ptr);
    // void set_namenode_master(Node node);
    void put(::google::protobuf::RpcController* controller, const DNPutRequest* request,
             CommonResponse* response, ::google::protobuf::Closure* done) override;
    void get(::google::protobuf::RpcController* controller, const DNGetRequest* request,
             DNGetResponse* response, ::google::protobuf::Closure* done) override;
    void get_namenodes(::google::protobuf::RpcController* controller, const Request* request,
                       DNGetNamenodesResponse* response,
                       ::google::protobuf::Closure* done) override;
    void get_datanodes(::google::protobuf::RpcController* controller, const Request* request,
                       DNGetDatanodesResponse* response,
                       ::google::protobuf::Closure* done) override;

  private:
    std::string my_ip;

    RaftDN* dn_raft_ptr;

    std::vector<std::thread> _threads;
  };

}  // namespace spkdfs
#endif
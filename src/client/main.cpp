#include <iostream>
#include <string>
#include <vector>

#include "client/config.h"
#include "client/sdk.h"
#include "common/daemon.pb.h"

using namespace std;
using namespace spkdfs;

// DEFINE_string(command, "ls", "command type: ls mkdir put get");
// DEFINE_string(datanode, "192.168.88.112:8001", "datanode addr:port");
// DEFINE_string(storage_type, "RS<3,2,64>",
//               "rs or replication, example: rs<3,2,64> re<5,64>, k=3,m=2,blocksize=64MB ");
// DEFINE_string(namenode, "", "set the default namenode to read, addr:port");

std::mutex mtx;
std::condition_variable cv;
bool done = false;

static void put_callback(spkdfs::DaemonPutResponse *resp, brpc::Controller *cntl) {
  std::unique_ptr<DaemonPutResponse> resp_guard(resp);
  std::unique_ptr<brpc::Controller> cntl_guard(cntl);
  if (cntl->Failed()) {
    LOG(INFO) << "callback_put = Failed" << cntl->ErrorText();
  } else {
    LOG(INFO) << "callback_put = Success";
  }

  std::lock_guard<std::mutex> lock(mtx);
  done = true;
  cv.notify_one();
}

static void get_callback(spkdfs::DaemonGetResponse *resp, brpc::Controller *cntl) {
  std::unique_ptr<DaemonGetResponse> resp_guard(resp);
  std::unique_ptr<brpc::Controller> cntl_guard(cntl);
  if (cntl->Failed()) {
    LOG(INFO) << "callback_get = Failed" << cntl->ErrorText();
  } else {
    LOG(INFO) << "callback_get = Success";
  }

  std::lock_guard<std::mutex> lock(mtx);
  done = true;
  cv.notify_one();
}

void replace_daemon_put_async(const std::string &src, const std::string &dst,
                              const std::string &storage_type) {
  brpc::Channel channel;
  brpc::ChannelOptions options;
  if (0 != channel.Init("127.0.0.1:8003", &options)) {
    throw std::runtime_error("channel init failed");
  }

  brpc::Controller *cntl = new brpc::Controller();
  spkdfs::DaemonPutRequest req;
  spkdfs::DaemonPutResponse *resp = new spkdfs::DaemonPutResponse();
  req.set_local_path(src);
  req.set_remote_path(dst);
  req.set_storage_type(storage_type);
  DaemonService_Stub stub(&channel);
  cntl->set_timeout_ms(CLIENT_ASYNC_TIMEOUT_MS);  // !!! 异步调用一定要设置超时时间
  stub.put(cntl, &req, resp, brpc::NewCallback(put_callback, resp, cntl));

  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [&]() { return done; });
  return;
}

void replace_daemon_get_async(const std::string &src, const std::string &dst) {
  brpc::Channel channel;
  brpc::ChannelOptions options;
  if (0 != channel.Init("127.0.0.1:8003", &options)) {
    throw std::runtime_error("channel init failed");
  }

  brpc::Controller *cntl = new brpc::Controller();
  spkdfs::DaemonGetRequest req;
  spkdfs::DaemonGetResponse *resp = new spkdfs::DaemonGetResponse();
  req.set_remote_path(src);
  req.set_local_path(dst);
  DaemonService_Stub stub(&channel);
  cntl->set_timeout_ms(CLIENT_ASYNC_TIMEOUT_MS);
  stub.get(cntl, &req, resp, brpc::NewCallback(get_callback, resp, cntl));

  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [&]() { return done; });
  return;
}

void daemon_fsync(const std::string &local_path, const std::string &remote_path) {
  brpc::Channel channel;
  brpc::ChannelOptions options;
  if (0 != channel.Init("127.0.0.1:8003", &options)) {
    throw std::runtime_error("channel init failed");
  }

  brpc::Controller *cntl = new brpc::Controller();
  spkdfs::DaemonSyncRequest req;
  spkdfs::DaemonSyncResponse *resp = new spkdfs::DaemonSyncResponse();
  req.set_local_path(local_path);
  req.set_remote_path(remote_path);
  DaemonService_Stub stub(&channel);
  stub.sync(cntl, &req, resp, nullptr);

  if (cntl->Failed()) {
    LOG(INFO) << "cntl error text: " << cntl->ErrorText();
  } else {
    LOG(INFO) << "cntl success";
  }
}

Inode daemon_ls(const std::string &path) {
  brpc::Channel channel;
  brpc::ChannelOptions options;
  if (0 != channel.Init("127.0.0.1:8003", &options)) {
    throw std::runtime_error("channel init failed");
  }

  brpc::Controller *cntl = new brpc::Controller();
  spkdfs::CommonPathRequest req;
  spkdfs::CommonPathResponse *resp = new spkdfs::CommonPathResponse();
  req.set_path(path);
  DaemonService_Stub stub(&channel);
  stub.ls(cntl, &req, resp, nullptr);

  if (cntl->Failed()) {
    LOG(INFO) << "cntl error text: " << cntl->ErrorText();
  } else {
    LOG(INFO) << "cntl success";
  }

  string inode_data = resp->data();
  auto __json = nlohmann::json::parse(inode_data);
  Inode inode = __json.get<Inode>();
  return inode;
}

void daemon_rm(const std::string &path) {
  brpc::Channel channel;
  brpc::ChannelOptions options;
  if (0 != channel.Init("127.0.0.1:8003", &options)) {
    throw std::runtime_error("channel init failed");
  }

  brpc::Controller *cntl = new brpc::Controller();
  spkdfs::CommonPathRequest req;
  spkdfs::CommonPathResponse *resp = new spkdfs::CommonPathResponse();
  req.set_path(path);
  DaemonService_Stub stub(&channel);
  stub.rm(cntl, &req, resp, nullptr);

  if (cntl->Failed()) {
    LOG(INFO) << "cntl error text: " << cntl->ErrorText();
  } else {
    LOG(INFO) << "cntl success";
  }
}

void daemon_debug_rpc() {
  brpc::Channel channel;
  brpc::ChannelOptions options;
  if (0 != channel.Init("127.0.0.1:8003", &options)) {
    throw std::runtime_error("channel init failed");
  }

  brpc::Controller *cntl = new brpc::Controller();
  spkdfs::DaemonSyncRequest req;
  spkdfs::DaemonSyncResponse *resp = new spkdfs::DaemonSyncResponse();
  req.set_local_path("");
  req.set_remote_path("");
  DaemonService_Stub stub(&channel);
  stub.debug(cntl, &req, resp, nullptr);

  if (cntl->Failed()) {
    LOG(INFO) << "cntl error text: " << cntl->ErrorText();
  } else {
    LOG(INFO) << "cntl success";
  }
}

int main(int argc, char *argv[]) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  // 将日志信息输出到标准输出
  FLAGS_logtostderr = true;
  SDK sdk(FLAGS_datanode, FLAGS_namenode);

  if (FLAGS_command == "put") {
    // 小文件应该是put调用前拦截，然后调用聚合接口聚合为大文件，创建出聚合文件的AggInode
    // 然后调用putAggFile上传聚合文件
    replace_daemon_put_async(argv[1], argv[2], FLAGS_storage_type);
    cout << "success" << endl;
  } else if (FLAGS_command == "get") {
    replace_daemon_get_async(argv[1], argv[2]);
    cout << "success" << endl;
  } else if (FLAGS_command == "ls") {
    Inode inode = daemon_ls(argv[1]);
    if (inode.is_directory) {
      cout << "." << endl;
      if (argv[1] != "/") cout << ".." << endl;
      for (auto sub : inode.sub) {
        cout << sub << endl;
      }
    } else {
      cout << inode.get_fullpath() << " " << inode.filesize << " "
           << (inode.storage_type_ptr == nullptr ? "UNKNOWN" : inode.storage_type_ptr->to_string())
           << endl;
    }
  } else if (FLAGS_command == "mkdir") {
    sdk.mkdir(argv[1]);
    cout << "success" << endl;
  } else if (FLAGS_command == "rm") {
    daemon_rm(argv[1]);
    cout << "success" << endl;
  } else if (FLAGS_command == "sync") {
    daemon_fsync(argv[1], argv[2]);
    cout << "fsync " << endl;
  } else {
    // DEBUG 检查打印当前小文件队列中的任务
    daemon_debug_rpc();  
    LOG(ERROR) << "unknown command";
  }
  return 0;
}
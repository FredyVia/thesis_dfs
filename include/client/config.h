#ifndef CLIENT_CONFIG
#define CLIENT_CONFIG
#include <gflags/gflags.h>

namespace spkdfs {
  DECLARE_string(data_dir);
  DECLARE_string(command);       // , "ls", "command type: ls mkdir put get");
  DECLARE_string(datanode);      // , "192.168.88.112:8001", "datanode addr:port");
  DECLARE_string(storage_type);  //, "RS<3,2,64>",
                                 // "rs or replication, example: rs<3,2,64> re<5,64>,
                                 // k=3,m=2,blocksize=64MB ");
  DECLARE_string(namenode);      //, "", "set the default namenode to read, addr:port");
  DECLARE_uint64(agg_size_boundary);
  DECLARE_string(agg_type);  // "AGG<7,5,64>"
  DECLARE_uint64(agg_timer_interval);
  static std::string DEFAULT_STORAGE_TYPE = "RS<7,5,64>";
}  // namespace spkdfs

#endif
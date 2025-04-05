#include "client/config.h"

#include <filesystem>

namespace spkdfs {
  // new_daemon
  DEFINE_string(data_dir, "/tmp/spkdfs", "tmp storage");
  // old_client
  DEFINE_string(command, "ls", "command type: ls mkdir put get");
  DEFINE_string(datanode, "192.168.88.112:8001", "datanode addr:port");
  DEFINE_string(storage_type, "RS<3,2,64>",
                "rs or replication, example: rs<3,2,64> re<5,64>, k=3,m=2,blocksize=64MB ");
  DEFINE_string(namenode, "", "set the default namenode to read, addr:port");
  DEFINE_uint64(agg_size_boundary, 64 * 1024 * 1024, "agg all small file up_boundary");
  DEFINE_string(agg_type, "AGG<7,5,64>", "aggregation type");
  DEFINE_uint64(agg_timer_interval, 30, "30s timed aggregation upload");
}  // namespace spkdfs

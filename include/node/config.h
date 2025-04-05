#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H
#include <gflags/gflags.h>

#include <vector>

#include "common/node.h"
#define NN_INTERVAL 10
namespace spkdfs {
  DECLARE_string(data_dir);
  DECLARE_string(coredumps_dir);
  DECLARE_string(nodes);
  DECLARE_uint32(nn_port);
  DECLARE_uint32(dn_port);
  DECLARE_uint32(expected_nn);
}  // namespace spkdfs
#endif
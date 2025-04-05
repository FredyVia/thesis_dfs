/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/

/** @file
 *
 * minimal example filesystem using high-level API
 *
 * Compile with:
 *
 *     gcc -Wall hello.c `pkg-config fuse3 --cflags --libs` -o hello
 *
 * ## Source code ##
 * \include hello.c
 */

#define FUSE_USE_VERSION 31

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <exception>
#include <regex>
#include <sstream>

#include "common/service.h"
/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "client/sdk.h"
#include "common/exception.h"

using namespace spkdfs;
using namespace std;
class FUSE {
  SDK *sdk;

  vector<Node> nodes;

  shared_mutex mapMutex;

public:
  FUSE(const string &ips) {
    nodes = parse_nodes(ips);
    init();
  }

  ~FUSE() { deinit(); }

  void deinit() {
    if (sdk != nullptr) {
      delete sdk;
      sdk = nullptr;
    }
  }

  void init_node(const Node &node) { sdk = new SDK(to_string(node)); }

  void init() {
    vector<Node> sub;
    sub.reserve(3);
    for (size_t i = 0; i < nodes.size(); i++) {
      if (sub.size() == 3) {
        node_discovery(sub);
        for (const Node &node : sub) {
          if (node.nodeStatus == NodeStatus::ONLINE) {
            init_node(node);
            return;
          }
        }
        sub.resize(0);
      }
      sub.push_back(nodes[i]);
    }
    if (sub.size() == 0) {
      throw runtime_error("all nodes offline");
    }
    node_discovery(sub);
    for (const Node &node : sub) {
      if (node.nodeStatus == NodeStatus::ONLINE) {
        init_node(node);
        return;
      }
    }
    throw runtime_error("all nodes offline");
  }

  void reinit() {
    deinit();
    init();
  }

  void open(const string &dst, int flags) {
    cout << "libfuse open: " << gettid() << ", path: " << dst << ", flags: " << flags << endl;
    try {
      sdk->open(dst, flags);
    } catch (const spkdfs::MessageException &e) {
      cout << e.what() << endl;
    } catch (const exception &e) {
      cout << e.what() << endl;
      reinit();
      sdk->open(dst, flags);
    }
  }

  void mkdir(const string &dst) {
    cout << "libfuse mkdir: " << dst << endl;
    try {
      sdk->mkdir(dst);
    } catch (const spkdfs::MessageException &e) {
      cout << e.what() << endl;
    } catch (const exception &e) {
      cout << e.what() << endl;
      reinit();
      sdk->mkdir(dst);
    }
  }

  void rm(const std::string &dst) {
    cout << "libfuse rm: " << dst << endl;
    try {
      sdk->rm(dst);
    } catch (const spkdfs::MessageException &e) {
      cout << e.what() << endl;
    } catch (const exception &e) {
      cout << dst << endl;
      cout << e.what() << endl;
      reinit();
      sdk->rm(dst);
    }
  }

  void truncate(const std::string &dst, size_t size) {
    cout << "libfuse truncate: " << dst << ", size: " << size << endl;
    try {
      sdk->truncate(dst, size);
    } catch (const spkdfs::MessageException &e) {
      cout << e.what() << endl;
    } catch (const exception &e) {
      cout << dst << endl;
      cout << e.what() << endl;
      reinit();
      sdk->truncate(dst, size);
    }
  }

  Inode ls(const std::string &dst) {
    cout << "libfuse ls: " << dst << endl;
    try {
      return sdk->ls(dst);
    } catch (const spkdfs::MessageException &e) {
      cout << e.what() << endl;
      throw e;
    } catch (const exception &e) {
      cout << dst << endl;
      cout << e.what() << endl;
      reinit();
      return sdk->ls(dst);
    }
  }

  string read(const string &dst, uint64_t offset, size_t size) {
    cout << "libfuse read: " << dst << endl;
    try {
      return sdk->read_data(dst, offset, size);
    } catch (const spkdfs::MessageException &e) {
      cout << e.what() << endl;
      throw e;
    } catch (const exception &e) {
      cout << dst << endl;
      cout << e.what() << endl;
      reinit();
      return sdk->read_data(dst, offset, size);
    }
  }

  void create(const string &dst) {
    cout << "libfuse create: " << dst << endl;
    try {
      sdk->create(dst);
    } catch (const spkdfs::MessageException &e) {
      cout << e.what() << endl;
      throw e;
    } catch (const exception &e) {
      cout << dst << endl;
      cout << e.what() << endl;
      reinit();
      sdk->create(dst);
    }
  }

  void write(const std::string &dst, uint64_t offset, const std::string &s) {
    cout << "libfuse write: " << dst << ", offset: " << offset << ", size: " << s.size() << endl;
    try {
      sdk->write_data(dst, offset, s);
    } catch (const spkdfs::MessageException &e) {
      cout << e.what() << endl;
      throw e;
    } catch (const exception &e) {
      cout << dst << endl;
      cout << e.what() << endl;
      reinit();
      sdk->write_data(dst, offset, s);
    }
  }

  void close(const std::string &dst) {
    cout << "libfuse close: " << dst << endl;
    try {
      sdk->close(dst);
    } catch (const spkdfs::MessageException &e) {
      cout << e.what() << endl;
      throw e;
    } catch (const exception &e) {
      cout << dst << endl;
      cout << e.what() << endl;
      reinit();
      sdk->close(dst);
    }
  }

  void fsync(const std::string &dst) {
    cout << "libfuse fsync: " << dst << endl;
    try {
      sdk->fsync(dst);
    } catch (const spkdfs::MessageException &e) {
      cout << e.what() << endl;
      throw e;
    } catch (const exception &e) {
      cout << dst << endl;
      cout << e.what() << endl;
      reinit();
      sdk->fsync(dst);
    }
  }
};

static struct options {
  const char *ips;
  int show_help;
} options;

#define OPTION(t, p) \
  { t, offsetof(struct options, p), 1 }

static const struct fuse_opt option_spec[]
    = {OPTION("--ips=%s", ips), OPTION("-h", show_help), OPTION("--help", show_help), FUSE_OPT_END};
FUSE *fuse_ptr;

static int spkdfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
  (void)fi;
  cout << "spkdfs_getattr: " << path << endl;
  try {
    Inode inode = fuse_ptr->ls(path);
    memset(stbuf, 0, sizeof(struct stat));
    if (inode.is_directory) {
      stbuf->st_mode = S_IFDIR | 0755;
      stbuf->st_nlink = 2;
    } else {
      stbuf->st_mode = S_IFREG | 0644;
      stbuf->st_nlink = 1;
      stbuf->st_size = inode.filesize;
    }
  } catch (const MessageException &e) {
    return -ENOENT;
  }
  return 0;
}

static int spkdfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                          struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
  filler(buf, ".", NULL, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_DEFAULTS);
  filler(buf, "..", NULL, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_DEFAULTS);
  // filler(buf, options.filename, NULL, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_DEFAULTS);
  Inode inode = fuse_ptr->ls(path);
  if (!inode.is_directory) {
    cout << "not directory" << endl;
    throw runtime_error("not directory error");
  }
  for (auto str : inode.sub) {
    if (str.back() == '/') str.pop_back();
    filler(buf, str.c_str(), NULL, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_DEFAULTS);
  }
  return 0;
}

static int spkdfs_mkdir(const char *path, mode_t mode) {
  try {
    fuse_ptr->mkdir(path);
  } catch (const MessageException &e) {
    cout << e.what() << endl;
    switch (e.errorMessage().code()) {
      case PATH_EXISTS_EXCEPTION:
        return EEXIST;
      case PATH_NOT_EXISTS_EXCEPTION:  // parent path not exist
        return ENOENT;
      default:
        return EIO;
    }
  } catch (const exception &e) {
    return EIO;
  }
  return 0;
}

static int spkdfs_rm(const char *path) {
  try {
    fuse_ptr->rm(path);
  } catch (const MessageException &e) {
    cout << e.what() << endl;
    switch (e.errorMessage().code()) {
      case PATH_EXISTS_EXCEPTION:
        return EEXIST;
      case PATH_NOT_EXISTS_EXCEPTION:  // parent path not exist
        return ENOENT;
      default:
        throw e;  // go to next catch
    }
  } catch (const exception &e) {
    return EIO;
  }
  return 0;
}

static int spkdfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
  cout << "call spkdfs_truncate, size: " << size << endl;
  try {
    fuse_ptr->truncate(path, size);
  } catch (const exception &e) {
    return -ENOENT;
  }
  return 0;
}

static int spkdfs_open(const char *path, struct fuse_file_info *fi) {
  fuse_ptr->open(path, fi->flags);
  return 0;
}

static int spkdfs_read(const char *path, char *buff, size_t size, off_t offset,
                       struct fuse_file_info *fi) {
  cout << "call spkdfs_read, offset: " << offset << ", size: " << size << endl;
  try {
    string s = fuse_ptr->read(path, offset, size);
    memcpy(buff, s.data(), size);
  } catch (const exception &e) {
    return -ENOENT;
  }
  return size;
}

static int spkdfs_write(const char *path, const char *data, size_t size, off_t offset,
                        struct fuse_file_info *) {
  try {
    string s(data, size);
    fuse_ptr->write(path, offset, s);
  } catch (const exception &e) {
    return -EIO;
  }
  return size;
}

int spkdfs_close(const char *path, struct fuse_file_info *) {
  try {
    fuse_ptr->close(path);
  } catch (const exception &e) {
    return -EIO;
  }
  return 0;
}
int spkdfs_fsync(const char *path, int, struct fuse_file_info *) {
  try {
    fuse_ptr->fsync(path);
  } catch (const exception &e) {
    return -EIO;
  }
  return 0;
}

static int spkdfs_create(const char *path, mode_t, struct fuse_file_info *) {
  try {
    fuse_ptr->create(path);
  } catch (const exception &e) {
    return -EIO;
  }
  return 0;
}

static int spkdfs_utimens(const char *path, const struct timespec ts[2],
                          struct fuse_file_info *fi) {
  printf("%s call\n", __func__);
  return 0;
}

static const struct fuse_operations spkdfs_oper = {
    .getattr = spkdfs_getattr,    // 316
    .mkdir = spkdfs_mkdir,        // 342
    .unlink = spkdfs_rm,          // 345
    .rmdir = spkdfs_rm,           // 348
    .truncate = spkdfs_truncate,  // 392
    .open = spkdfs_open,          // 441
    .read = spkdfs_read,          // 452
    .write = spkdfs_write,        // 464
    .release = spkdfs_close,      // 515
    .fsync = spkdfs_fsync,        // 522
    .readdir = spkdfs_readdir,    // 561
    .create = spkdfs_create,      // 614
    .utimens = spkdfs_utimens,    // 661
};

static void show_help(const char *progname) {
  printf("usage: %s [options] <mountpoint>\n\n", progname);
  printf(
      "File-system specific options:\n"
      "    --ips=<s>          ips of the cluster\n"
      "                        (default: \"192.168.88.112:11801\")\n"
      "\n");
}

int main(int argc, char *argv[]) {
  int ret;
  // google::InitGoogleLogging(argv[0]);
  // google::AllowCommandLineReparsing();
  // google::ParseCommandLineFlags(&argc, &argv, true);
  // for (int i = 0; i < argc; i++) {
  // //   cout << "argv[" << i << "]"
  //        << ": " << argv[i] << endl;
  // }
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  /* Set defaults -- we have to use strdup so that
     fuse_opt_parse can free the defaults if other
     values are specified */
  options.ips = strdup("192.168.88.111:8001,192.168.88.112:8001\n");

  /* Parse options */
  if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1) return 1;

  /* When --help is specified, first print our own file-system
     specific help text, then signal fuse_main to show
     additional help (by adding `--help` to the options again)
     without usage: line (by setting argv[0] to the empty
     string) */
  if (options.show_help) {
    show_help(argv[0]);
    assert(fuse_opt_add_arg(&args, "--help") == 0);
    args.argv[0][0] = '\0';
    return 0;
  }
  fuse_ptr = new FUSE(options.ips);
  ret = fuse_main(args.argc, args.argv, &spkdfs_oper, NULL);
  fuse_opt_free_args(&args);
  return ret;
}

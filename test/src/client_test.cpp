#include <brpc/channel.h>
#include <brpc/controller.h>  // brpc::Controller
#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#define DBG_MACRO_NO_WARNING
#include <dbg-macro/dbg.h>

#include "client/sdk.h"
#include "common/inode.h"
#include "common/utils.h"

using namespace std;
using namespace spkdfs;
// 测试 Inode 类的 JSON 数组序列化和反序列化
TEST(InodeTest, InodeAggId) {
  Inode inode1;
  inode1.set_fullpath("/path/to/file1");
  inode1.agg_inode_id = "123";  // 设置 agg_inode_id

  // 将 Inode 对象序列化为 JSON
  nlohmann::json j1 = inode1;
  std::cout << j1.dump() << std::endl;  // 应该包含 "agg_inode_id" 字段

  Inode inode2;
  inode2.set_fullpath("/path/to/file2");
  // inode2.agg_inode_id 默认是空字符串

  // 将 Inode 对象序列化为 JSON
  nlohmann::json j2 = inode2;
  std::cout << j2.dump() << std::endl;  // 不应该包含 "agg_inode_id" 字段
}

TEST(InodeTest, SerializationAndDeserializationArray) {
  // 1. 创建多个 Inode 对象，并填充数据
  std::vector<Inode> inodes;

  Inode inode1;
  inode1.set_fullpath("/path/to/inode1");
  inode1.is_directory = true;
  inode1.filesize = 1024;
  inode1.sub = {"sub1", "sub2"};
  inode1.valid = true;
  inode1.ddl_lock = 12345;
  inode1.agg_inode_id = "agg1";

  Inode inode2;
  inode2.set_fullpath("/path/to/inode2");
  inode2.is_directory = false;
  inode2.filesize = 2048;
  inode2.sub = {"sub3", "sub4"};
  inode2.valid = false;
  inode2.ddl_lock = 67890;
  inode2.agg_inode_id = "agg2";

  inodes.push_back(inode1);
  inodes.push_back(inode2);

  // 2. 将 Inode 对象数组序列化为 JSON 字符串
  nlohmann::json j = inodes;
  std::string serializedData = j.dump();  // 转换为 JSON 字符串

  // 打印 JSON 数据（可选）
  std::cout << "Serialized JSON Array: " << serializedData << std::endl;

  // 3. 反序列化 JSON 字符串回 Inode 对象数组
  std::vector<Inode> deserializedInodes = j.get<std::vector<Inode>>();

  // 4. 验证反序列化后的数据是否正确

  // 检查 Inode 数组的大小
  EXPECT_EQ(inodes.size(), deserializedInodes.size());

  // 验证每个 Inode 对象的数据
  for (size_t i = 0; i < inodes.size(); ++i) {
    EXPECT_EQ(inodes[i].get_fullpath(), deserializedInodes[i].get_fullpath());
    EXPECT_EQ(inodes[i].is_directory, deserializedInodes[i].is_directory);
    EXPECT_EQ(inodes[i].filesize, deserializedInodes[i].filesize);
    EXPECT_EQ(inodes[i].sub, deserializedInodes[i].sub);
    EXPECT_EQ(inodes[i].valid, deserializedInodes[i].valid);
    EXPECT_EQ(inodes[i].ddl_lock, deserializedInodes[i].ddl_lock);
    EXPECT_EQ(inodes[i].agg_inode_id, deserializedInodes[i].agg_inode_id);
  }
}
// 测试AggInode的序列化和反序列化
TEST(AggInodeTest, SerializationAndDeserialization) {
  // 1. 创建一个 AggInode 对象，并填充数据
  std::vector<FileInfo> files
      = {FileInfo("/path/to/file1", 100, {0, 100}), FileInfo("/path/to/file2", 200, {100, 300}),
         FileInfo("/path/to/file3", 300, {300, 600})};

  AggInode inode("agg123", files, 600, {true, true, true}, 3);

  // 2. 将 AggInode 序列化为 JSON 字符串
  std::string serializedData = inode.value();  // 转换为 JSON 字符串

  // 打印 JSON 数据（可选）
  std::cout << "Serialized JSON: " << serializedData << std::endl;

  // 3. 反序列化 JSON 字符串回 AggInode 对象
  nlohmann::json j = nlohmann::json::parse(serializedData);
  AggInode deserializedInode = j.get<AggInode>();
  // from_json(j, deserializedInode);
  // 4. 验证反序列化后的数据是否正确

  // 检查 ID
  EXPECT_EQ(inode.getId(), deserializedInode.getId());

  // 检查文件信息
  EXPECT_EQ(inode.getFiles().size(), deserializedInode.getFiles().size());
  for (size_t i = 0; i < inode.getFiles().size(); ++i) {
    EXPECT_EQ(inode.getFiles()[i].path, deserializedInode.getFiles()[i].path);
    EXPECT_EQ(inode.getFiles()[i].size, deserializedInode.getFiles()[i].size);
    EXPECT_EQ(inode.getFiles()[i].pos, deserializedInode.getFiles()[i].pos);
  }

  // 检查聚合文件的大小
  EXPECT_EQ(inode.getAggregateSize(), deserializedInode.getAggregateSize());

  // 检查 valid 标记
  EXPECT_EQ(inode.valid.size(), deserializedInode.valid.size());
  for (size_t i = 0; i < inode.valid.size(); ++i) {
    EXPECT_EQ(inode.valid[i], deserializedInode.valid[i]);
  }

  // 检查有效文件数量
  EXPECT_EQ(inode.valid_count, deserializedInode.valid_count);
}

TEST(Client, putAgg) {
  SDK sdk("192.168.88.112:8001");
  sdk.mkdir("/tmp");
  vector<string> files = {"/tmp/file1", "/tmp/file2", "/tmp/file3", "/tmp/file4", "/tmp/file5"};

  // 创建测试用的小文件
  for (auto& file : files) {
    std::ofstream ofs(file);
    if (!ofs) throw std::runtime_error("Failed to create file: " + file);
    ofs << "Test data for " << file << std::endl;
    ofs.close();
  }

  // 聚合逻辑
  AggInode aggInode;
  std::string aggData;
  size_t currentPos = 0;

  for (const auto& file : files) {
    std::ifstream ifs(file, std::ios::binary | std::ios::ate);
    if (!ifs) throw std::runtime_error("Failed to open file: " + file);

    uint64_t fileSize = ifs.tellg();
    ifs.seekg(0);

    std::string fileData(fileSize, '\0');
    ifs.read(&fileData[0], fileSize);
    ifs.close();

    // 追加到聚合数据中
    aggData.append(fileData);

    // 添加文件信息到 AggInode, [pos_start, pos_end)
    aggInode.files.emplace_back(file, fileSize, std::make_pair(currentPos, currentPos + fileSize));
    currentPos += fileSize;
  }

  aggInode.aggregate_size = aggData.size();

  // 上传聚合文件
  sdk.putAggFile(aggInode, aggData, "AGG<7,5,64>");

  // 验证上传结果
  EXPECT_EQ(aggInode.files.size(), files.size());
  EXPECT_EQ(aggInode.aggregate_size, aggData.size());
  EXPECT_GT(aggInode.aggregate_size, 0);

  for (size_t i = 0; i < files.size(); ++i) {
    EXPECT_EQ(aggInode.files[i].path, files[i]);
    EXPECT_EQ(aggInode.files[i].pos.second - aggInode.files[i].pos.first, aggInode.files[i].size);
  }
  std::string dir = "/tmp/dst/";
  for (const auto& file : files) {
    try {
      sdk.get(file, dir + file.substr(5));
    } catch (const std::exception& e) {
      std::cout << "get " << file << " " << e.what() << std::endl;
    }
  }
}

TEST(Client, encode_decode) {}

TEST(Client, mkdir) {
  SDK sdk("192.168.88.112:8001");
  // should be sequencial
  vector<string> datas = {
      "/abc",
      "/abcv/",
      "/abc/cde",
  };
  map<string, set<string>> res;
  // remove all
  for (auto iter = datas.rbegin(); iter != datas.rend(); iter++) {
    try {
      sdk.rm(*iter);
    } catch (const exception& e) {
      cout << "rm " << *iter << e.what() << endl;
    }
  }

  // create dir
  for (auto& s : datas) {
    sdk.mkdir(s);
    Inode inode;
    inode.set_fullpath(s);
    res[inode.parent_path()].insert(inode.filename());
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  int c = 0;
  for (auto& [parent_dir, subdirs] : res) {
    cout << "parent dir: " << parent_dir << endl;
    cout << "sub dir: ";
    for (auto& s : subdirs) {
      cout << s << ", ";
    }
    cout << endl;
    Inode inode = sdk.ls(parent_dir);
    set<string> sub(inode.sub.begin(), inode.sub.end());
    dbg::pretty_print(cout, inode.value());
    for (auto& s : subdirs) {
      Inode inode;
      inode.set_fullpath(s);
      EXPECT_EQ(sub.contains(inode.filename()) || sub.contains(inode.filename() + "/"), true)
          << inode.filename() << " not found";
    }
  }
}

TEST(Client, concurrency) {
  SDK sdk("192.168.88.112:8001");
  vector<std::thread> threads;
  string data = "/tests/";
  try {
    sdk.rm(data);
  } catch (const exception& e) {
  }
  // 创建并启动多个线程
  for (int i = 0; i < 5; ++i) {
    threads.emplace_back([i, &sdk, &data]() {
      try {
        sdk.mkdir(data);
      } catch (const exception& e) {
      }
    });
  }

  // 等待所有线程完成
  for (auto& t : threads) {
    t.join();
  }

  Inode inode = sdk.ls("/");
  int c = 0;
  for (auto& s : inode.sub) {
    if (s == data) {
      c++;
    }
  }
  // EXPECT_EQ(c, 1);
}

TEST(TestCommon, alignup) {
  vector<vector<int>> checks = {{2, 5, 0, 1}, {4, 5, 0, 1}, {5, 5, 1, 2}, {6, 5, 1, 2},
                                {1, 2, 0, 1}, {2, 2, 1, 2}, {4, 2, 2, 3}, {5, 2, 2, 3}};
  for (auto& vec : checks) {
    EXPECT_EQ(align_index_down(vec[0], vec[1]), vec[2]);
    EXPECT_EQ(align_index_up(vec[0], vec[1]), vec[3]);
  }
}

TEST(SDK, read_data_edge) {}

TEST(TestCommon, hash) {
  // md5sum, sha256sum
  vector<vector<string>> vec
      = {{"", "D41D8CD98F00B204E9800998ECF8427E",
          "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"}};
  for (const auto& v : vec) {
    EXPECT_EQ(cal_md5sum(v[0]), v[1]);
    EXPECT_EQ(cal_sha256sum(v[0]), v[2]);
  }
}
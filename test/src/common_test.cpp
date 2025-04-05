#include <common/inode.h>
#include <common/node.h>
#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

using namespace std;
using namespace spkdfs;
using json = nlohmann::json;

TEST(Node, parse_nodes) {
  vector<vector<pair<string, int>>> ipports_group = {
      {{"127.0.0.1", 10086}, {"127.0.0.2", 10086}, {"127.0.0.3", 10086}}, {{{"127.0.0.1", 10086}}}};
  for (auto &ipports : ipports_group) {
    vector<string> res(4);  // test ip,ip | ip:port,ip:port | ip,ip, | ip:port,ip:port,
    for (auto &p : ipports) {
      res[0] += p.first + ",";
      res[1] += p.first + ",";
      res[2] += p.first + ":" + to_string(p.second) + ",";
      res[3] += p.first + ":" + to_string(p.second) + ",";
    }
    res[0].pop_back();
    res[2].pop_back();

    for (int i = 0; i < res.size(); i++) {
      std::vector<Node> nodes = parse_nodes(res[i]);
      EXPECT_EQ(nodes.size(), ipports.size());
      for (int j = 0; j < nodes.size(); j++) {
        EXPECT_EQ(nodes[j].ip, ipports[j].first);
        if (i < 2) {
          EXPECT_EQ(nodes[j].port, 0);
        } else {
          EXPECT_EQ(nodes[j].port, ipports[j].second);
        }
      }
    }
  }
}

TEST(Inode, RSStorageType) {
  vector<pair<string, vector<int>>> res = {
      {"RS<1,2,3>", {1, 2, 3}},
      {"RS<888,88,8>", {888, 88, 8}},
      {"RS<12,21,39>", {12, 21, 39}},
  };
  for (auto &p : res) {
    RSStorageType *rs_storage_type_ptr
        = dynamic_cast<RSStorageType *>(StorageType::from_string(p.first).get());
    EXPECT_EQ(rs_storage_type_ptr->k, p.second[0]);
    EXPECT_EQ(rs_storage_type_ptr->m, p.second[1]);
    EXPECT_EQ(rs_storage_type_ptr->get_block_size() / 1024 / 1024, p.second[2]);
  }
}

TEST(Inode, REStorageType) {
  vector<pair<string, vector<int>>> res = {
      {"RE<2,3>", {2, 3}},
      {"RE<88,8>", {88, 8}},
      {"RE<21,39>", {21, 39}},
  };
  for (auto &p : res) {
    REStorageType *re_storage_type_ptr
        = dynamic_cast<REStorageType *>(StorageType::from_string(p.first).get());
    EXPECT_EQ(re_storage_type_ptr->replications, p.second[0]);
    EXPECT_EQ(re_storage_type_ptr->get_block_size() / 1024 / 1024, p.second[1]);
  }
}

TEST(Inode, inode_parent_path) {
  vector<vector<string>> datas = {
      {"/tests", "/"},           {"/tests/", "/"}, {"/tests/abc", "/tests"},
      {"/tests/abc/", "/tests"}, {"//", "/"},      {"/", "/"},
  };
  Inode inode;
  for (auto &data : datas) {
    inode.set_fullpath(data[0]);
    EXPECT_EQ(inode.parent_path(), data[1])
        << "data[0]: " << data[0] << ", data[1]: " << data[1]
        << " ,inode.get_fullpath(): " << inode.get_fullpath() << ";";
  }
}

TEST(Client, RS_encode_decode) {
  vector<string> datas = {"12345", "qwer", "", "\0"};

  // fatal "RS<12,8,16>", "RS<12,8,64>" "RS<12,8,256>",
  vector<string> storage_types
      = {"RS<3,2,16>", "RS<3,2,64>", "RS<3,2,256>", "RS<7,5,16>", "RS<7,5,64>", "RS<7,5,256>"};
  srand(time(0));
  std::random_device rd;   // 用于获取随机数种子
  std::mt19937 gen(rd());  // 标准 mersenne_twister_engine
  auto generateBlock = [&gen](size_t length) {
    std::uniform_int_distribution<> distrib(0, 255);  // 注意修改为 0 到 255，代表合法的字符范围

    std::string block(length, ' ');  // 预先分配内存，减少内存重新分配

    for (size_t i = 0; i < length; ++i) {
      block[i] = static_cast<char>(distrib(gen));  // 直接转换为字符
    }

    return block;
  };

  for (auto &storage_type : storage_types) {
    auto rs_ptr = StorageType::from_string(storage_type);
    for (auto &data : datas) {
      try {
        string res = rs_ptr->decode(rs_ptr->encode(data));
        EXPECT_EQ(res, data);
      } catch (const exception &e) {
        FAIL() << e.what() << ", storage_type" << storage_type << endl;
      }
    }
    // max: data_size = 2.4 * blocksize
    string data = generateBlock(((rand() % 60) * 1.0 / 40) * rs_ptr->get_block_size());
    cout << "generateBlock size: " << data.size() << endl;
    auto vec = rs_ptr->encode(data);
    size_t size = vec[0].size();
    for (const auto &s : vec) {
      // check encoded file size if equal
      EXPECT_EQ(s.size(), size);
    }
    EXPECT_EQ(rs_ptr->decode(vec), data);
  }
}

TEST(Inode, full_path) {}

TEST(Inode, from_json) {}

TEST(Inode, to_json) {}
TEST(Common, stl) {
  try {
    string s;
    s.pop_back();
  } catch (const exception &e) {
    FAIL() << "We shouldn't get here.";
  }
  try {
    try {
      throw 20;
    } catch (int e) {
      throw string("abc");
    } catch (std::string const &ex) {
      FAIL() << "re throw We shouldn't get here.";
    }
  } catch (std::string const &ex) {
    SUCCEED() << "re throw go here";
  }
}
TEST(Inode, json) {
  vector<string> datas = {"/", "/abc"};
  for (auto &s : datas) {
    Inode inode = Inode::get_default_dir(s);
    auto _json = json::parse(inode.value());
    Inode tmpInode = _json.get<Inode>();
    EXPECT_EQ(tmpInode.value(), inode.value());
  }
}
TEST(Utils, timer) {
  int interval = 3, pre = 0, now = 0, test_times = 3;
  auto intervalTimer_ptr = make_shared<IntervalTimer>(
      interval,
      [&]() {
        test_times--;
        cout << "interval timer running" << endl;
        if (test_times == 0) {
          throw IntervalTimer::TimeoutException("exit", "");
        }
        now = time(NULL);
        if (pre != 0) {
          EXPECT_LE(abs(now - pre - interval), 1);
        }
        pre = now;
      },
      [this](const void *args) {});
  sleep(4 * interval);
}
#include <benchmark/benchmark.h>
#include <gflags/gflags.h>
#include <sys/sysinfo.h>

#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <thread>

#include "client/sdk.h"
#include "common/exception.h"

using namespace std;
using namespace spkdfs;

string DATANODE = "192.168.88.112:8001";
int STRLEN = 16;
int THREADS = 4;
int ITERS = 64;

class ThreadSafeStringSelector {
public:
  ThreadSafeStringSelector(const string& characters)
      : eng(rd()), dist(0, characters.size() - 1), chars(characters) {
    if (characters.size() < 32) {
      throw runtime_error("Characters string must be at least 16 characters long.");
    }
  }

  // 从chars中随机选择长度为16的字符串
  string select_one(size_t str_len = 64) { return *(select_batch(1, str_len).begin()); }

  unordered_set<string> select_batch(size_t size, size_t str_len = 64) {
    unordered_set<string> se;
    lock_guard<mutex> guard(mtx);
    while (se.size() < size) {
      string result;
      for (int j = 0; j < str_len; ++j) {
        result += chars[dist(eng)];
      }
      se.insert(result);
    }
    return se;
  }

private:
  random_device rd;
  mt19937 eng;
  uniform_int_distribution<> dist;
  string chars;
  mutex mtx;
};

static ThreadSafeStringSelector threadSafeStringSelector(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

class BenchmarkFixture : public benchmark::Fixture {
  unique_ptr<SDK> sdk_ptr = nullptr;
  unordered_set<string> datas;

public:
  void SetUp(benchmark::State& state) override {
    int size = 0;
    for (auto _ : state) {
      size++;
    }

    sdk_ptr = make_unique<SDK>(DATANODE);
    for (auto& s : threadSafeStringSelector.select_batch(size, STRLEN)) {
      datas.insert("/" + s);
    }
    for (auto& data : datas) {
      try {
        sdk_ptr->rm(data);
      } catch (const exception& e) {
      }
    }
  }

  void TearDown(benchmark::State& state) override {
    for (auto& data : datas) {
      try {
        sdk_ptr->rm(data);
      } catch (const exception& e) {
      }
    }
  }

  void mkdirBenchmark(benchmark::State& st) {
    for (auto _ : st) {
      for (auto& s : datas) {
        try {
          sdk_ptr->mkdir(s);
          // cout << "mkdir: " << s << endl;
        } catch (const exception& e) {
        }
      }
    }
  }

  void refBenchmark(benchmark::State& st) {
    for (auto _ : st) {
      for (auto& s : datas) {
        try {
          Node(DATANODE).scan();
          // cout << "mkdir: " << s << endl;
        } catch (const exception& e) {
        }
      }
    }
  }
};

BENCHMARK_DEFINE_F(BenchmarkFixture, mkdir)(benchmark::State& st) { mkdirBenchmark(st); }
BENCHMARK_DEFINE_F(BenchmarkFixture, ref)(benchmark::State& st) { refBenchmark(st); }

BENCHMARK_REGISTER_F(BenchmarkFixture, mkdir)->Threads(THREADS)->Iterations(ITERS);
BENCHMARK_REGISTER_F(BenchmarkFixture, ref)->Threads(THREADS)->Iterations(ITERS);

// ->Args({32})->Args({64})->Threads(4)->Threads(8);

int main(int argc, char* argv[]) {
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
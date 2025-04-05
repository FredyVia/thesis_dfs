#ifndef EXTENDIBLE_HASH_TABLE_H
#define EXTENDIBLE_HASH_TABLE_H

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

namespace spkdfs {

  /**
   * @tparam K key type
   * @tparam V value type
   */
  template <typename K, typename V> class ExtendibleHashTable {
  public:
    /**
     * @brief Create a new ExtendibleHashTable.
     * @param bucket_size: fixed size for each bucket
     */
    explicit ExtendibleHashTable(size_t bucket_size)
        : global_depth_(0), bucket_size_(bucket_size), num_buckets_(1) {
      // dir nums = 2^global_depth. 最初全局深度为0，只有一个dir
      dir_.push_back(std::make_shared<Bucket>(bucket_size_, 0));
    }

    int GetGlobalDepth() const {
      std::scoped_lock<std::mutex> lock(latch_);
      return GetGlobalDepthInternal();
    }

    int GetLocalDepth(int dir_index) const {
      std::scoped_lock<std::mutex> lock(latch_);
      return GetLocalDepthInternal(dir_index);
    }

    int GetNumBuckets() const {
      std::scoped_lock<std::mutex> lock(latch_);
      return GetNumBucketsInternal();
    }

    /**
     * @brief Find the value associated with the given key.
     * @param key The key to be searched.
     * @param[out] value The value associated with the key.
     * @return True if the key is found, false otherwise.
     */
    bool Find(const K &key, V &value) {
      std::scoped_lock<std::mutex> lock(latch_);
      return dir_[IndexOf(key)]->Find(key, value);
    }

    /**

     * @brief Insert the given key-value pair into the hash table.
     * @param key The key to be inserted.
     * @param value The value to be inserted.
     */
    void Insert(const K &key, const V &value) {
      std::scoped_lock<std::mutex> lock(latch_);
      while (!dir_[IndexOf(key)]->Insert(key, value)) {
        // bucket full
        // 将dir扩大一倍，满的bucket中的元素重新分到整个dir中
        auto index = IndexOf(key);
        auto bucket_ptr = dir_[index];
        auto global_extend = false;
        // 如果溢出桶的本地深度等于全局深度，那么需要执行目录扩张和桶分裂
        if (bucket_ptr->GetDepth() == global_depth_) {
          global_depth_++;
          dir_.resize(std::pow(2, global_depth_), nullptr);  // dir扩张先用nullptr填充
          global_extend = true;
        }
        // 如果局部深度小于全局深度，那么仅仅发生桶分裂。然后仅仅把局部深度增加1.这种情况是多个dir指针指向同一个bucket，创建新桶
        RedistributeBucket(bucket_ptr, index);

        // 桶扩张需要将新的指针指向同一个桶
        if (global_extend) {
          auto curr_dir_size = dir_.size();
          auto old_dir_size = curr_dir_size >> 1;
          for (auto i = old_dir_size; i < curr_dir_size; i++) {
            if (dir_[i] == nullptr) {
              dir_[i] = dir_[i - old_dir_size];
            }
          }
        }
      }
    }

    /**
     * @brief Given the key, remove the corresponding key-value pair in the hash table.
     * @param key The key to be deleted.
     * @return True if the key exists, false otherwise.
     */
    bool Remove(const K &key) {
      std::scoped_lock<std::mutex> lock(latch_);
      return dir_[IndexOf(key)]->Remove(key);
    }

    class Bucket {
    public:
      explicit Bucket(size_t array_size, int depth) : size_(array_size), depth_(depth) {}

      inline bool IsFull() const { return list_.size() == size_; }

      inline int GetDepth() const { return depth_; }

      inline void IncrementDepth() { depth_++; }

      inline std::list<std::pair<K, V>> &GetItems() { return list_; }

      /**
       * @brief Find the value associated with the given key in the bucket.
       * @param key The key to be searched.
       * @param[out] value The value associated with the key.
       * @return True if the key is found, false otherwise.
       */
      bool Find(const K &key, V &value) {
        // find_if 将*first作为参数传给谓词pred
        /*
        template<class InputIterator, class UnaryPredicate>
        InputIterator find_if (InputIterator first, InputIterator last, UnaryPredicate
        pred)
        {
          while (first!=last) {
            if (pred(*first)) return first;
            ++first;
          }
          return last;
        }
        */
        auto it = std::find_if(list_.begin(), list_.end(), [key](const std::pair<K, V> &element) {
          return element.first == key;
        });
        if (it != list_.end()) {
          value = it->second;
          return true;
        }
        return false;
      }

      /**
       * @brief Given the key, remove the corresponding key-value pair in the
       * bucket.
       * @param key The key to be deleted.
       * @return True if the key exists, false otherwise.
       */
      bool Remove(const K &key) {
        auto it = std::find_if(list_.begin(), list_.end(), [key](const std::pair<K, V> &element) {
          return element.first == key;
        });
        if (it != list_.end()) {
          list_.erase(it);
          return true;
        }
        return false;
      }

      /**
       * @brief Insert the given key-value pair into the bucket.
       *      1. If a key already exists, the value should be updated.
       *      2. If the bucket is full, do nothing and return false.
       * @param key The key to be inserted.
       * @param value The value to be inserted.
       * @return True if the key-value pair is inserted, false otherwise.
       */
      bool Insert(const K &key, const V &value) {
        auto it = std::find_if(list_.begin(), list_.end(), [key](const std::pair<K, V> &element) {
          return element.first == key;
        });
        if (it != list_.end()) {
          it->second = value;
        } else {
          if (IsFull()) {
            return false;
          }
          list_.insert(list_.end(), std::make_pair(key, value));
        }
        return true;
      }
      std::list<std::pair<K, V>> list_;

    private:
      size_t size_;
      int depth_;
    };

  private:
    int global_depth_;    // The global depth of the directory
    size_t bucket_size_;  // The size of a bucket
    int num_buckets_;     // The number of buckets in the hash table
    mutable std::mutex latch_;
    std::vector<std::shared_ptr<Bucket>> dir_;  // The directory of the hash table

    /**
     * @brief Redistribute the kv pairs in a full bucket.
     * @param bucket The bucket to be redistributed.
     */
    void RedistributeBucket(std::shared_ptr<Bucket> bucket, int64_t index) {
      auto old_depth = bucket->GetDepth();
      int64_t old_gap = std::pow(2, old_depth);
      bucket->IncrementDepth();

      // 分裂出两个新的bucket
      auto even_new_bucket = std::make_shared<Bucket>(bucket_size_, bucket->GetDepth());
      auto odd_new_bucket = std::make_shared<Bucket>(bucket_size_, bucket->GetDepth());
      num_buckets_++;

      // 目录中的指针指向新的bucket
      // index % old_gap 为指向这个bucket的第一个指针
      for (u_int64_t i = index % old_gap; i < dir_.size(); i += old_gap) {
        if (dir_[i] == nullptr || dir_[i] == bucket) {
          auto prefix_bit = (i >> old_depth) & 1;
          dir_[i] = (prefix_bit == 0) ? even_new_bucket : odd_new_bucket;
        }
      }
      for (auto it = bucket->list_.rbegin(); it != bucket->list_.rend(); it++) {
        auto item_index = IndexOf(it->first);
        dir_[item_index]->Insert(it->first, it->second);
      }
    }

    /**
     * @brief For the given key, return the entry index in the directory where the
     * key hashes to.
     * @param key The key to be hashed.
     * @return The entry index in the directory.
     */
    size_t IndexOf(const K &key) {
      int mask = (1 << global_depth_) - 1;
      return std::hash<K>()(key) & mask;
    }

    int GetGlobalDepthInternal() const { return global_depth_; }
    int GetLocalDepthInternal(int dir_index) const { return dir_[dir_index]->GetDepth(); }
    int GetNumBucketsInternal() const { return num_buckets_; }
  };

}  // namespace spkdfs
#endif  // EXTENDIBLE_HASH_TABLE_H
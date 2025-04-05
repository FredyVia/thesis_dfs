#include <glog/logging.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

namespace spkdfs {
  class StorageType {
  protected:
    // split file to b size(MB)
    int b;

  public:
    StorageType(int b) : b(b) {};
    virtual std::string to_string() const = 0;
    virtual void to_json(nlohmann::json& j) const = 0;
    virtual std::vector<std::string> encode(const std::string& data) const = 0;
    virtual std::string decode(std::vector<std::string> vec) const = 0;
    virtual bool check(int success) const = 0;
    static std::shared_ptr<StorageType> from_string(const std::string& input);
    inline virtual int get_block_size() { return b * 1024 * 1024; }
    virtual int getBlocks() = 0;
    virtual int getDecodeBlocks() = 0;
    virtual ~StorageType() {};
  };

  class RSStorageType : public StorageType {
  public:
    int k;
    int m;

    RSStorageType(int k, int m, int b) : k(k), m(m), StorageType(b) {};
    std::string to_string() const override;
    void to_json(nlohmann::json& j) const override;
    std::vector<std::string> encode(const std::string& data) const override;
    std::string decode(std::vector<std::string> vec) const override;
    bool check(int success) const override;
    // inline virtual int get_block_size() { return b * 1024 * 1024; }
    inline virtual int getBlocks() { return k + m; }
    virtual int getDecodeBlocks() { return k; }
  };

  class REStorageType : public StorageType {
  private:
  public:
    int replications;
    REStorageType(int replications, int b) : replications(replications), StorageType(b) {};
    std::string to_string() const override;
    void to_json(nlohmann::json& j) const override;
    std::vector<std::string> encode(const std::string& data) const override;
    std::string decode(std::vector<std::string> vec) const override;
    bool check(int success) const override;
    inline virtual int getBlocks() { return replications; }
    virtual int getDecodeBlocks() { return 1; }
  };

  // 小文件聚合为大文件的存储类型
  // 该类型的文件Inode的sub = {<ip,blkid>,<ip,blkid>,...<ip,blkid>,<begin,end>}
  // 一个块所有分片的信息+该文件所在整个聚合文件的起始位置和结束位置，需要特殊解析
  class AggStorageType : public StorageType {
  private:
  public:
    int k;
    int m;
    AggStorageType(int k, int m, int b) : k(k), m(m), StorageType(b) {};
    std::string to_string() const override;
    void to_json(nlohmann::json& j) const override;
    std::vector<std::string> encode(const std::string& data) const override;
    std::string decode(std::vector<std::string> vec) const override;
    bool check(int success) const override;
    inline virtual int getBlocks() { return k + m; }
    virtual int getDecodeBlocks() { return k; }
  };

  inline std::string to_string(std::unique_ptr<StorageType> storageType_ptr);

  void from_json(const nlohmann::json& j, std::shared_ptr<StorageType>& ptr);
}  // namespace spkdfs
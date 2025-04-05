#include "common/storage_type.h"

#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/utils.h"
#include "erasurecode.h"

namespace spkdfs {
  using json = nlohmann::json;
  using namespace std;
  static vector<string> rs_encode(const std::string& data, int k, int m) {
    struct ec_args args = {.k = k, .m = m};
    int instance = liberasurecode_instance_create(EC_BACKEND_ISA_L_RS_VAND, &args);
    if (instance <= 0) {
      throw runtime_error("liberasurecode_instance_create error: " + std::to_string(instance));
    }
    char** encoded_data = NULL;
    char** encoded_parity = NULL;
    uint64_t encoded_fragment_len = 0;

    int ret = liberasurecode_encode(instance, data.data(), data.size(), &encoded_data,
                                    &encoded_parity, &encoded_fragment_len);
    if (ret != 0) {
      liberasurecode_instance_destroy(instance);
      throw runtime_error("encode error, error code: " + std::to_string(ret));
    }
    vector<string> res(k + m, string(encoded_fragment_len, ' '));
    for (int i = 0; i < k; i++) {
      res[i] = string(encoded_data[i], encoded_fragment_len);
    }
    for (int i = 0; i < m; i++) {
      res[k + i] = string(encoded_parity[i], encoded_fragment_len);
    }

    liberasurecode_encode_cleanup(instance, encoded_data, encoded_parity);
    liberasurecode_instance_destroy(instance);
    return res;
  }

  std::string rs_decode(vector<std::string> vec, int k, int m) {
    struct ec_args args = {.k = k, .m = m};
    int instance = liberasurecode_instance_create(EC_BACKEND_ISA_L_RS_VAND, &args);
    int len = 0;
    char* datas[k];
    for (auto& str : vec) {
      VLOG(2) << "sha256sum: " << cal_sha256sum(str) << endl;
      datas[len++] = str.data();
      if (len == k) {
        break;
      }
    }
    uint64_t out_data_len = 0;
    char* out_data;
    int ret
        = liberasurecode_decode(instance, datas, len, vec[0].size(), 0, &out_data, &out_data_len);
    if (ret != 0) {
      liberasurecode_instance_destroy(instance);
      throw runtime_error("decode error");
    }
    return string(out_data, out_data_len);
  }

  std::shared_ptr<StorageType> StorageType::from_string(const std::string& input) {
    // std::regex pattern("RS<(\\d+),(\\d+),(\\d+)>|RE<(\\d+),(\\d+)>");
    // 扩展正则表达式，支持 "RS<k,m,b>", "RE<replications,b>", "AGG<k,m,b>"
    std::regex pattern("RS<(\\d+),(\\d+),(\\d+)>|RE<(\\d+),(\\d+)>|AGG<(\\d+),(\\d+),(\\d+)>");
    std::smatch match;
    if (std::regex_match(input, match, pattern)) {
      if (match[1].matched) {
        int k = std::stoi(match[1]);
        int m = std::stoi(match[2]);
        int b = std::stoi(match[3]);
        return std::make_shared<RSStorageType>(k, m, b);
      } else if (match[4].matched) {
        int replications = std::stoi(match[4]);
        int b = std::stoi(match[5]);
        return std::make_shared<REStorageType>(replications, b);
      } else if (match[6].matched) {  // 增加小文件类型的存储类型
        int k = std::stoi(match[6]);
        int m = std::stoi(match[7]);
        int b = std::stoi(match[8]);
        return std::make_shared<AggStorageType>(k, m, b);
      }
    }
    throw runtime_error("cannot solve " + input);
  }

  void from_json(const json& j, std::shared_ptr<StorageType>& ptr) {
    std::string type = j.at("type").get<string>();
    if (type == "RS") {
      int k = j.at("k").get<int>();
      int m = j.at("m").get<int>();
      int b = j.at("b").get<int>();
      ptr = std::make_shared<RSStorageType>(k, m, b);
    } else if (type == "RE") {
      int replications = j.at("replications").get<int>();
      int b = j.at("b").get<int>();
      ptr = std::make_shared<REStorageType>(replications, b);
    } else if (type == "AGG") {
      int k = j.at("k").get<int>();
      int m = j.at("m").get<int>();
      int b = j.at("b").get<int>();
      ptr = std::make_shared<AggStorageType>(k, m, b);
    } else {
      LOG(ERROR) << type;
      throw std::invalid_argument("Unknown StorageType");
    }
  }

  std::string RSStorageType::to_string() const {
    return "RS<" + std::to_string(k) + "," + std::to_string(m) + "," + std::to_string(b) + ">";
  }

  void RSStorageType::to_json(json& j) const {
    j["type"] = "RS";
    j["k"] = k;
    j["m"] = m;
    j["b"] = b;
  }

  std::vector<std::string> RSStorageType::encode(const std::string& data) const {
    return rs_encode(data, k, m);
  }

  std::string RSStorageType::decode(vector<std::string> vec) const {
    assert(vec.size() >= k);
    assert(vec.size() <= k + m);
    return rs_decode(vec, k, m);
  }

  bool RSStorageType::check(int succ_cout) const { return succ_cout > k; }

  std::vector<std::string> REStorageType::encode(const std::string& data) const {
    VLOG(2) << "re encode" << endl;
    vector<std::string> vec;
    for (int i = 0; i < replications; i++) {
      vec.push_back(data);
    }
    return vec;
  }

  std::string REStorageType::to_string() const {
    return "RE<" + std::to_string(replications) + "," + std::to_string(b) + ">";
  }

  void REStorageType::to_json(json& j) const {
    j["type"] = "RE";
    j["replications"] = replications;
    j["b"] = b;
  }

  std::string REStorageType::decode(std::vector<std::string> vec) const { return vec.front(); }

  bool REStorageType::check(int succ_cout) const { return succ_cout >= 1; }

  // for AggStorageType
  std::string AggStorageType::to_string() const {
    return "AGG<" + std::to_string(k) + "," + std::to_string(m) + "," + std::to_string(b) + ">";
  }

  void AggStorageType::to_json(nlohmann::json& j) const {
    j["type"] = "AGG";
    j["k"] = k;
    j["m"] = m;
    j["b"] = b;
  }
  std::vector<std::string> AggStorageType::encode(const std::string& data) const {
    return rs_encode(data, k, m);
  }

  std::string AggStorageType::decode(std::vector<std::string> vec) const {
    if (vec.empty()) {
      throw runtime_error("no data");
    }
    return rs_decode(vec, k, m);
  }
  bool AggStorageType::check(int success) const { return success > k; }

}  // namespace spkdfs
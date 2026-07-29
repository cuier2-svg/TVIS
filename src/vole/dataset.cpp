#include "dataset.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace vole {

  namespace {
    constexpr std::uintmax_t kBlockBytes = 16;
  }

  osuCrypto::u64 blockFileElementCount(const std::string &path) {
    if (path.empty()) {
      throw std::invalid_argument("block set file path is empty");
    }

    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
      throw std::runtime_error("cannot read block set file size: " + path);
    }
    if (bytes == 0 || bytes % kBlockBytes != 0) {
      throw std::runtime_error(
          "block set file must contain a non-zero multiple of 16 bytes: " + path);
    }

    const auto elements = bytes / kBlockBytes;
    if (elements > std::numeric_limits<osuCrypto::u64>::max()) {
      throw std::runtime_error("block set file is too large: " + path);
    }
    return static_cast<osuCrypto::u64>(elements);
  }

  std::vector<osuCrypto::block> loadBlockFile(
      const std::string &path,
      osuCrypto::u64 expectedElements) {
    static_assert(sizeof(osuCrypto::block) == kBlockBytes);

    const auto elements = blockFileElementCount(path);
    if (elements != expectedElements) {
      throw std::runtime_error(
          "block set element count changed after parameter parsing: " + path);
    }

    std::vector<osuCrypto::block> values(elements);
    std::ifstream source(path, std::ios::binary);
    if (!source) {
      throw std::runtime_error("cannot open block set file: " + path);
    }

    const auto bytes = static_cast<std::streamsize>(elements * kBlockBytes);
    source.read(reinterpret_cast<char *>(values.data()), bytes);
    if (source.gcount() != bytes || source.peek() != std::ifstream::traits_type::eof()) {
      throw std::runtime_error("failed to read complete block set file: " + path);
    }
    return values;
  }

}

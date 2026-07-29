#pragma once

#include <cryptoTools/Common/Defines.h>

#include <string>
#include <vector>

namespace vole {

  osuCrypto::u64 blockFileElementCount(const std::string &path);

  std::vector<osuCrypto::block> loadBlockFile(
      const std::string &path,
      osuCrypto::u64 expectedElements);

}

#pragma once

#include "vole.hpp"

#ifdef PSI_ENABLE_BATCHPIR

#include <array>
#include <vector>

#include <seal/seal.h>

namespace vole::cf_batchpir {

  struct Params {
    u64 queryCount = 0;
    u64 dbSize = 0;
    u64 internalBucketCount = 0;
    u64 dim = 0;
    u64 gB = 0;
    u64 polyModulusDegree = 0;
    u64 rowSize = 0;
  };

  struct Assignment {
    u64 queryOffset = 0;
    u64 cfBucket = 0;
    u64 internalBucket = 0;
    u64 position = 0;
    u64 x = 0;
    u64 y = 0;
    u64 z = 0;
  };

  seal::EncryptionParameters createEncryptionParameters();
  Params makeParams(u64 queryCount, u64 dbSize);
  std::vector<Assignment> assignQueries(const std::vector<u64> &cfBuckets,
                                        const Params &params);
  std::vector<int> galoisSteps(const Params &params);

  std::array<seal::Ciphertext, 3> createSelectors(
      const std::vector<Assignment> &assignments,
      const Params &params,
      seal::Encryptor &encryptor,
      seal::BatchEncoder &batchEncoder);

  std::array<seal::Ciphertext, 3> answer(
      const CuckooFilter &cf,
      const Params &params,
      const std::array<seal::Ciphertext, 3> &selectors,
      const seal::GaloisKeys &galoisKeys,
      const seal::RelinKeys &relinKeys,
      seal::Evaluator &evaluator,
      seal::BatchEncoder &batchEncoder);

  std::vector<std::array<u32, CuckooFilter::TagsPerBucket()>> decodeResponses(
      const std::array<seal::Ciphertext, 3> &responses,
      const std::vector<Assignment> &assignments,
      seal::Decryptor &decryptor,
      seal::BatchEncoder &batchEncoder);

}

#endif

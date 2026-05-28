#include "cf_batchpir.hpp"

#ifdef PSI_ENABLE_BATCHPIR

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vole::cf_batchpir {

  namespace {

    constexpr double kCuckooFactor = 2.0;
    constexpr u64 kHashFunctions = 3;
    u64 splitmix64(u64 x) {
      x += 0x9e3779b97f4a7c15ULL;
      x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
      x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
      return x ^ (x >> 31);
    }

    std::array<u64, kHashFunctions> candidates(u64 value, u64 bucketCount) {
      std::array<u64, kHashFunctions> result{};
      for (u64 i = 0; i < kHashFunctions; ++i) {
        result[i] = splitmix64(value * kHashFunctions + i) % bucketCount;
        bool duplicate = true;
        while (duplicate) {
          duplicate = false;
          for (u64 j = 0; j < i; ++j) {
            if (result[i] == result[j]) {
              result[i] = (result[i] + 1) % bucketCount;
              duplicate = true;
              break;
            }
          }
        }
      }
      return result;
    }

    u64 nextPowerOfTwo(u64 value) {
      u64 power = 1;
      while (power < value) {
        power <<= 1;
      }
      return power;
    }

    u64 ceilCubeRoot(u64 value) {
      u64 root = static_cast<u64>(std::ceil(std::cbrt(static_cast<double>(value))));
      while (root * root * root < value) {
        ++root;
      }
      return root;
    }

    u64 positionInInternalBucket(u64 cfBucket, u64 internalBucket,
                                 const Params &params) {
      u64 position = 0;
      for (u64 item = 0; item < cfBucket; ++item) {
        auto cand = candidates(item, params.internalBucketCount);
        if (std::find(cand.begin(), cand.end(), internalBucket) != cand.end()) {
          ++position;
        }
      }
      return position;
    }

    u64 maxInternalBucketSize(const Params &params) {
      std::vector<u64> sizes(params.internalBucketCount);
      for (u64 item = 0; item < params.dbSize; ++item) {
        auto cand = candidates(item, params.internalBucketCount);
        for (auto bucket : cand) {
          ++sizes[bucket];
        }
      }
      return *std::max_element(sizes.begin(), sizes.end());
    }

    std::vector<std::vector<u64>> buildInternalBuckets(const Params &params) {
      std::vector<std::vector<u64>> buckets(params.internalBucketCount);
      for (u64 item = 0; item < params.dbSize; ++item) {
        auto cand = candidates(item, params.internalBucketCount);
        for (auto bucket : cand) {
          buckets[bucket].push_back(item);
        }
      }
      return buckets;
    }

    void addOrAssign(seal::Ciphertext &acc, bool &hasValue,
                     const seal::Ciphertext &value,
                     seal::Evaluator &evaluator) {
      if (!hasValue) {
        acc = value;
        hasValue = true;
      } else {
        evaluator.add_inplace(acc, value);
      }
    }

    std::array<u32, CuckooFilter::TagsPerBucket()> readTags(
        const CuckooFilter &cf, u64 bucket) {
      return cf.ReadBucketTags(bucket);
    }

    std::vector<u64> nonEmptyPositions(const std::vector<std::vector<u64>> &buckets) {
      u64 maxSize = 0;
      for (const auto &bucket : buckets) {
        maxSize = std::max<u64>(maxSize, bucket.size());
      }
      std::vector<u64> positions(maxSize);
      for (u64 i = 0; i < maxSize; ++i) {
        positions[i] = i;
      }
      return positions;
    }

  }

  seal::EncryptionParameters createEncryptionParameters() {
    seal::EncryptionParameters params(seal::scheme_type::bfv);
    constexpr size_t polyModulusDegree = 32768;
    params.set_poly_modulus_degree(polyModulusDegree);
    params.set_coeff_modulus(
        seal::CoeffModulus::Create(polyModulusDegree, {60, 50, 50, 50, 60}));
    params.set_plain_modulus(seal::PlainModulus::Batching(polyModulusDegree, 40));
    return params;
  }

  Params makeParams(u64 queryCount, u64 dbSize) {
    Params params;
    params.queryCount = queryCount;
    params.dbSize = dbSize;
    params.internalBucketCount =
        static_cast<u64>(std::ceil(queryCount * kCuckooFactor));
    params.polyModulusDegree = 32768;
    params.rowSize = params.polyModulusDegree / 2;

    u64 maxBucket = maxInternalBucketSize(params);
    params.dim = nextPowerOfTwo(ceilCubeRoot(maxBucket));
    params.gB = params.rowSize / params.dim;

    if (params.internalBucketCount > params.gB) {
      throw std::runtime_error("CF BatchPIR parameters require more than one ciphertext group");
    }
    if (params.dim * params.dim * params.dim < maxBucket) {
      throw std::runtime_error("CF BatchPIR cube is too small");
    }
    return params;
  }

  std::vector<Assignment> assignQueries(const std::vector<u64> &cfBuckets,
                                        const Params &params) {
    std::vector<u64> assigned(params.internalBucketCount, UINT64_MAX);
    std::vector<Assignment> result(cfBuckets.size());

    auto insert = [&](auto &&self, u64 queryOffset, std::vector<u8> &seen) -> bool {
      u64 cfBucket = cfBuckets[queryOffset];
      auto cand = candidates(cfBucket, params.internalBucketCount);
      for (u64 bucket : cand) {
        if (seen[bucket]) {
          continue;
        }
        seen[bucket] = 1;
        if (assigned[bucket] == UINT64_MAX) {
          assigned[bucket] = queryOffset;
          return true;
        }
        u64 evicted = assigned[bucket];
        assigned[bucket] = queryOffset;
        if (self(self, evicted, seen)) {
          return true;
        }
        assigned[bucket] = evicted;
      }
      return false;
    };

    for (u64 i = 0; i < cfBuckets.size(); ++i) {
      std::vector<u8> seen(params.internalBucketCount, 0);
      if (!insert(insert, i, seen)) {
        throw std::runtime_error("CF BatchPIR cuckoo assignment failed");
      }
    }

    for (u64 internalBucket = 0; internalBucket < assigned.size(); ++internalBucket) {
      if (assigned[internalBucket] == UINT64_MAX) {
        continue;
      }
      u64 queryOffset = assigned[internalBucket];
      u64 cfBucket = cfBuckets[queryOffset];
      u64 position = positionInInternalBucket(cfBucket, internalBucket, params);
      Assignment assignment;
      assignment.queryOffset = queryOffset;
      assignment.cfBucket = cfBucket;
      assignment.internalBucket = internalBucket;
      assignment.position = position;
      assignment.x = position / (params.dim * params.dim);
      assignment.y = (position / params.dim) % params.dim;
      assignment.z = position % params.dim;
      if (assignment.x >= params.dim) {
        throw std::runtime_error("CF BatchPIR assigned position exceeds cube");
      }
      result[queryOffset] = assignment;
    }
    return result;
  }

  std::vector<int> galoisSteps(const Params &params) {
    std::vector<int> steps;
    steps.reserve(params.dim > 0 ? params.dim - 1 : 0);
    for (u64 coord = 1; coord < params.dim; ++coord) {
      steps.push_back(static_cast<int>(coord * params.gB));
    }
    return steps;
  }

  std::array<seal::Ciphertext, 3> createSelectors(
      const std::vector<Assignment> &assignments,
      const Params &params,
      seal::Encryptor &encryptor,
      seal::BatchEncoder &batchEncoder) {
    std::array<std::vector<uint64_t>, 3> plain;
    for (auto &v : plain) {
      v.assign(params.polyModulusDegree, 0);
    }

    for (const auto &assignment : assignments) {
      const u64 b = assignment.internalBucket;
      plain[0][b + assignment.x * params.gB] = 1;
      plain[1][b + assignment.y * params.gB] = 1;
      plain[2][b + assignment.z * params.gB] = 1;
    }

    std::array<seal::Ciphertext, 3> selectors;
    for (size_t i = 0; i < selectors.size(); ++i) {
      seal::Plaintext pt;
      batchEncoder.encode(plain[i], pt);
      encryptor.encrypt_symmetric(pt, selectors[i]);
    }
    return selectors;
  }

  std::array<seal::Ciphertext, 3> answer(
      const CuckooFilter &cf,
      const Params &params,
      const std::array<seal::Ciphertext, 3> &selectors,
      const seal::GaloisKeys &galoisKeys,
      const seal::RelinKeys &relinKeys,
      seal::Evaluator &evaluator,
      seal::BatchEncoder &batchEncoder) {
    auto buckets = buildInternalBuckets(params);

    std::array<std::vector<seal::Ciphertext>, 3> rotated;
    for (auto &axis : rotated) {
      axis.resize(params.dim);
    }
    for (u64 coord = 0; coord < params.dim; ++coord) {
      int steps = static_cast<int>(coord * params.gB);
      evaluator.rotate_rows(selectors[0], steps, galoisKeys, rotated[0][coord]);
      evaluator.rotate_rows(selectors[1], steps, galoisKeys, rotated[1][coord]);
      evaluator.rotate_rows(selectors[2], steps, galoisKeys, rotated[2][coord]);
    }

    auto positions = nonEmptyPositions(buckets);
    std::vector<seal::Ciphertext> positionSelectors(positions.size());
    for (u64 idx = 0; idx < positions.size(); ++idx) {
      u64 position = positions[idx];
      u64 x = position / (params.dim * params.dim);
      u64 y = (position / params.dim) % params.dim;
      u64 z = position % params.dim;

      seal::Ciphertext xySelector;
      evaluator.multiply(rotated[0][x], rotated[1][y], xySelector);
      evaluator.relinearize_inplace(xySelector, relinKeys);
      evaluator.multiply_inplace(xySelector, rotated[2][z]);
      evaluator.relinearize_inplace(xySelector, relinKeys);
      positionSelectors[idx] = std::move(xySelector);
    }

    std::array<seal::Ciphertext, 3> responses;
    for (u64 tagIdx = 0; tagIdx < CuckooFilter::TagsPerBucket(); ++tagIdx) {
      seal::Ciphertext response;
      bool hasResponse = false;

      for (u64 idx = 0; idx < positions.size(); ++idx) {
        u64 position = positions[idx];
        std::vector<uint64_t> values(params.polyModulusDegree, 0);
        bool nonZero = false;
        for (u64 b = 0; b < params.internalBucketCount; ++b) {
          if (position < buckets[b].size()) {
            auto tags = readTags(cf, buckets[b][position]);
            values[b] = tags[tagIdx];
            nonZero = nonZero || values[b] != 0;
          }
        }
        if (!nonZero) {
          continue;
        }

        seal::Plaintext pt;
        batchEncoder.encode(values, pt);
        seal::Ciphertext term = positionSelectors[idx];
        evaluator.multiply_plain_inplace(term, pt);
        addOrAssign(response, hasResponse, term, evaluator);
      }

      if (!hasResponse) {
        throw std::runtime_error("CF BatchPIR produced an empty response");
      }
      responses[tagIdx] = response;
    }
    return responses;
  }

  std::vector<std::array<u32, CuckooFilter::TagsPerBucket()>> decodeResponses(
      const std::array<seal::Ciphertext, 3> &responses,
      const std::vector<Assignment> &assignments,
      seal::Decryptor &decryptor,
      seal::BatchEncoder &batchEncoder) {
    std::array<std::vector<uint64_t>, CuckooFilter::TagsPerBucket()> decoded;
    for (size_t i = 0; i < responses.size(); ++i) {
      seal::Plaintext pt;
      decryptor.decrypt(responses[i], pt);
      batchEncoder.decode(pt, decoded[i]);
    }

    std::vector<std::array<u32, CuckooFilter::TagsPerBucket()>> tags(assignments.size());
    for (const auto &assignment : assignments) {
      for (u64 i = 0; i < CuckooFilter::TagsPerBucket(); ++i) {
        tags[assignment.queryOffset][i] =
            static_cast<u32>(decoded[i][assignment.internalBucket] & 0xffffffffULL);
      }
    }
    return tags;
  }

  std::vector<std::array<u32, CuckooFilter::TagsPerBucket()>> plainAnswerForDebug(
      const CuckooFilter &cf,
      const Params &params,
      const std::vector<Assignment> &assignments) {
    (void) params;
    std::vector<std::array<u32, CuckooFilter::TagsPerBucket()>> out(assignments.size());
    for (const auto &assignment : assignments) {
      out[assignment.queryOffset] = cf.ReadBucketTags(assignment.cfBucket);
    }
    return out;
  }

}

#endif

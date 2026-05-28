#include "vole.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>

#ifdef PSI_ENABLE_BATCHPIR
#include "batchpirclient.h"
#include "batchpirparams.h"
#include "src/utils.h"
#include <seal/seal.h>

#include <chrono>
#include <cmath>
#include <sstream>
#endif

using namespace std;

namespace vole {

#ifdef PSI_ENABLE_BATCHPIR
  namespace {
    constexpr size_t kCfBucketEntrySize = CuckooFilter::TagsPerBucket() * sizeof(u32);

    vector<u8> saveSealObject(const auto &value) {
      stringstream stream(ios::in | ios::out | ios::binary);
      value.save(stream);
      auto data = stream.str();
      return vector<u8>(data.begin(), data.end());
    }

    template <typename T>
    T loadSealObject(const seal::SEALContext &context, const vector<u8> &bytes) {
      T value;
      string data(reinterpret_cast<const char *>(bytes.data()), bytes.size());
      stringstream stream(data, ios::in | ios::out | ios::binary);
      value.load(context, stream);
      return value;
    }

    void sendBytes(Socket &ch, const vector<u8> &bytes) {
      u64 size = bytes.size();
      macoro::sync_wait(ch.send(size));
      if (size) {
        macoro::sync_wait(ch.send(bytes));
      }
    }

    void recvBytes(Socket &ch, vector<u8> &bytes) {
      u64 size = 0;
      macoro::sync_wait(ch.recv(size));
      bytes.resize(size);
      if (size) {
        macoro::sync_wait(ch.recv(bytes));
      }
    }

    u64 sendKeys(Socket &ch, const pair<seal::GaloisKeys, seal::RelinKeys> &keys) {
      auto galoisBytes = saveSealObject(keys.first);
      auto relinBytes = saveSealObject(keys.second);
      sendBytes(ch, galoisBytes);
      sendBytes(ch, relinBytes);
      return galoisBytes.size() + relinBytes.size() + 2 * sizeof(u64);
    }

    size_t estimateBatchPirDim(u64 cfBuckets, u64 queryBuckets) {
      const auto internalBuckets = static_cast<size_t>(ceil(queryBuckets * 1.2));
      const auto averageBucketSize =
          ceil((cfBuckets * 3.0) / static_cast<double>(internalBuckets));
      return utils::next_power_of_two(static_cast<size_t>(ceil(cbrt(averageBucketSize))));
    }

    seal::EncryptionParameters createCfBatchPirEncryptionParameters(u64 cfBuckets,
                                                                    u64 queryBuckets) {
      const auto dim = estimateBatchPirDim(cfBuckets, queryBuckets);
      const auto internalBuckets = static_cast<size_t>(ceil(queryBuckets * 1.2));
      const size_t polyModulusDegree =
          internalBuckets * dim <= 16384 ? 16384 : 32768;
      seal::EncryptionParameters params(seal::scheme_type::bfv);
      params.set_poly_modulus_degree(polyModulusDegree);
      if (polyModulusDegree == 32768) {
        params.set_coeff_modulus(
            seal::CoeffModulus::Create(polyModulusDegree, {60, 50, 50, 50, 60}));
      } else {
        params.set_coeff_modulus(
            seal::CoeffModulus::Create(polyModulusDegree, {55, 55, 48, 60}));
      }
      params.set_plain_modulus(seal::PlainModulus::Batching(polyModulusDegree, 22));
      return params;
    }

    u64 sendBatchPirQueries(Socket &ch, const vector<PIRQuery> &queries) {
      u64 bytesSent = sizeof(u64);
      u64 queryCount = queries.size();
      macoro::sync_wait(ch.send(queryCount));
      for (const auto &query : queries) {
        u64 ciphertextCount = query.size();
        macoro::sync_wait(ch.send(ciphertextCount));
        bytesSent += sizeof(u64);
        for (const auto &ciphertext : query) {
          auto bytes = saveSealObject(ciphertext);
          bytesSent += bytes.size() + sizeof(u64);
          sendBytes(ch, bytes);
        }
      }
      return bytesSent;
    }

    PIRResponseList recvBatchPirResponses(Socket &ch,
                                          const seal::SEALContext &context,
                                          u64 &bytesReceived) {
      u64 responseCount = 0;
      macoro::sync_wait(ch.recv(responseCount));
      bytesReceived = sizeof(u64);
      PIRResponseList responses(responseCount);
      for (auto &response : responses) {
        vector<u8> bytes;
        recvBytes(ch, bytes);
        bytesReceived += bytes.size() + sizeof(u64);
        response = loadSealObject<seal::Ciphertext>(context, bytes);
      }
      return responses;
    }

    u32 loadTag(const vector<unsigned char> &entry, u64 slot) {
      const auto offset = slot * sizeof(u32);
      return static_cast<u32>(entry[offset + 0]) |
             (static_cast<u32>(entry[offset + 1]) << 8) |
             (static_cast<u32>(entry[offset + 2]) << 16) |
             (static_cast<u32>(entry[offset + 3]) << 24);
    }

    void printElapsed(const char *label,
                      std::chrono::high_resolution_clock::time_point start) {
      auto end = std::chrono::high_resolution_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
      std::cout << label << "=" << ms.count() << "\n";
    }
  }
#endif

  void VOLE::runReceiver() {
    auto ch = cp::asioConnect("127.0.0.1:7700", false);

    vector<block> receiverSet(receiverSize);
    PRNG prng(oc::toBlock(123));

    PRNG prng2(oc::toBlock(456));
    prng.get<block>(receiverSet);

    receiverSet[2] = prng2.get<block>();
    receiverSet[3] = prng2.get<block>();
    receiverSet[5] = prng2.get<block>();
    receiverSet[7] = prng2.get<block>();

    runReceiver(PRNG(sysRandomSeed()), ch, receiverSet);

    macoro::sync_wait(ch.flush());
  }

  void VOLE::runReceiver(osuCrypto::PRNG prng, osuCrypto::Socket ch, const std::vector<block> &receiverSet) {
    Timer timer;
    timer.setTimePoint("Receiver start");

    PRNG commonPrng(commonSeed);
    vector<u128> cfParams(3);
    commonPrng.get((u8 *) cfParams.data(), cfParams.size() * sizeof(u128));

    Paxos<u32> okvs;
    block okvsSeed = commonPrng.get<block>();
    okvs.init(receiverSize, 3, 40, PaxosParam::Binary, okvsSeed);
    u64 kSize = okvs.size();
    std::cout << "param.okvs_size=" << kSize << "\n";

    vector<block> okvsData(kSize);
    commonPrng.get<block>(okvsData);

    // Slient VOLE
    SilentVoleReceiver<block, block, CoeffCtxGF128> voleReceiver;
    voleReceiver.mMalType = SilentSecType::SemiHonest;
    voleReceiver.configure(kSize, SilentBaseType::Base);

    // Noisy VOLE
//    CoeffCtxGF128 ctx;
//    NoisyVoleReceiver<block, block, CoeffCtxGF128> noisyVoleReceiver;

    CuckooFilter cf(senderSize);
    cf.SetTwoIndependentMultiplyShiftParams(cfParams);

    if (!indexedCf) {
      u64 cfSize;
      cp::sync_wait(ch.recv(cfSize));
      timer.setTimePoint("Receiver setup start");
      vector<u8> cfData(cfSize);
      cp::sync_wait(ch.recv(cfData));
      cf.deserialize(cfData);
//    cout << cf.Info() << endl;
      timer.setTimePoint("Receiver setup finished");
    } else {
      timer.setTimePoint("Receiver setup skipped");
    }

    macoro::sync_wait(ch.flush());
    u64 setupData = (ch.bytesSent() + ch.bytesReceived());

    AlignedUnVector<block> voleA(kSize);
    AlignedUnVector<block> voleB(kSize);

    // Silent VOLE
    macoro::sync_wait(voleReceiver.silentReceive(voleB, voleA, prng, ch));
    timer.setTimePoint("Receiver silent VOLE finished");

    // Noisy VOLE
//    IknpOtExtSender otExtSender;
//    macoro::sync_wait(noisyVoleReceiver.receive(voleB, voleA, prng, otExtSender, ch, ctx));
//    macoro::sync_wait(ch.flush());
//    timer.setTimePoint("Receiver noisy VOLE finished");

    u64 voleData = (ch.bytesSent() + ch.bytesReceived()) - setupData;
    std::cout << "comm.vole_mb=" << voleData / std::pow(2.0, 20) << "\n";

    vector<block> okvsValues(receiverSize);
    HASH hash;
    block* pblock = (block*) hash;
    for (u64 i = 0; i < receiverSize; i++) {
      SHA256(receiverSet[i].data(), sizeof(block), hash);
      okvsValues[i] = *pblock;
    }
    vector<block> encodedOKVS(kSize);
    PRNG encodePRNG(okvsSeed);
    okvs.solve<block>(receiverSet, okvsValues, encodedOKVS, &encodePRNG);
    for (u64 i = 0; i < kSize; i++) {
      voleB[i] = voleB[i] ^ encodedOKVS[i];
    }
    macoro::sync_wait(ch.send(voleB));

    vector<block> serverOKVS(kSize);
    macoro::sync_wait(ch.recv(serverOKVS));
    for (u64 i = 0; i < kSize; i++) {
      serverOKVS[i] = serverOKVS[i] ^ voleA[i];
    }

    vector<block> decodedValues(receiverSize);
    okvs.decode<block>(receiverSet, decodedValues, serverOKVS);

    u64 psi = 0;
    if (!indexedCf) {
      for (u64 i = 0; i < receiverSize; i++) {
        SHA256((u8 *) &decodedValues[i], sizeof(block), hash);
        if (cf.Contain((u64 *) &hash) == cuckoofilter::Status::Ok) {
          psi++;
        }
      }
    } else {
      vector<CuckooFilter::Query> queries(receiverSize);
      vector<u64> bucketIds;
      bucketIds.reserve(receiverSize * 2);

      for (u64 i = 0; i < receiverSize; i++) {
        SHA256((u8 *) &decodedValues[i], sizeof(block), hash);
        queries[i] = cf.GetQuery((u64 *) &hash);
        bucketIds.push_back(queries[i].i1);
        bucketIds.push_back(queries[i].i2);
      }

      sort(bucketIds.begin(), bucketIds.end());
      bucketIds.erase(unique(bucketIds.begin(), bucketIds.end()), bucketIds.end());
      u64 bucketCount = bucketIds.size();
      unordered_map<u64, u64> bucketOffset;
      bucketOffset.reserve(bucketIds.size());
      vector<u32> bucketTags(bucketIds.size() * CuckooFilter::TagsPerBucket());

      if (batchPirCf) {
#ifdef PSI_ENABLE_BATCHPIR
        macoro::sync_wait(ch.send(bucketCount));

        auto encryptionParams =
            createCfBatchPirEncryptionParameters(cf.NumBuckets(), bucketCount);
        seal::SEALContext context(encryptionParams);
        BatchPirParams params(static_cast<int>(bucketCount), cf.NumBuckets(),
                              kCfBucketEntrySize, encryptionParams);
        u64 maxBucketSize = 0;
        macoro::sync_wait(ch.recv(maxBucketSize));
        params.set_max_bucket_size(maxBucketSize);
        auto bpStart = std::chrono::high_resolution_clock::now();
        BatchPIRClient client(params);
        printElapsed("time.receiver_batchpir_client_prep_ms", bpStart);
        auto queryCipherStart = ch.bytesSent() + ch.bytesReceived();
        auto stepStart = std::chrono::high_resolution_clock::now();
        auto queriesForPir = client.create_queries(bucketIds);
        printElapsed("time.receiver_batchpir_query_ms", stepStart);
        stepStart = std::chrono::high_resolution_clock::now();
        auto keyBytes = sendKeys(ch, client.get_public_keys());
        printElapsed("time.receiver_batchpir_send_keys_ms", stepStart);
        stepStart = std::chrono::high_resolution_clock::now();
        auto queryBytes = sendBatchPirQueries(ch, queriesForPir);
        printElapsed("time.receiver_batchpir_send_queries_ms", stepStart);
        stepStart = std::chrono::high_resolution_clock::now();
        u64 responseBytes = 0;
        auto responses = recvBatchPirResponses(ch, context, responseBytes);
        printElapsed("time.receiver_batchpir_recv_responses_ms", stepStart);
        auto queryCipherEnd = ch.bytesSent() + ch.bytesReceived();
        stepStart = std::chrono::high_resolution_clock::now();
        auto decodedBucketGroups = client.decode_responses_chunks(responses);
        printElapsed("time.receiver_batchpir_decode_ms", stepStart);
        auto originalCuckooTable = client.get_original_cuckoo_table();

        u64 cuckooOffset = 0;
        for (const auto &decodedBuckets : decodedBucketGroups) {
          for (const auto &decodedBucket : decodedBuckets) {
            if (cuckooOffset >= originalCuckooTable.size()) {
              throw std::runtime_error("BatchPIR decoded more buckets than expected");
            }
            if (originalCuckooTable[cuckooOffset] != params.get_default_value()) {
              auto bucketOffsetIt =
                  lower_bound(bucketIds.begin(), bucketIds.end(),
                              originalCuckooTable[cuckooOffset]);
              if (bucketOffsetIt == bucketIds.end() ||
                  *bucketOffsetIt != originalCuckooTable[cuckooOffset]) {
                throw std::runtime_error("BatchPIR decoded a bucket not in the request batch");
              }
              auto outputOffset =
                  static_cast<u64>(bucketOffsetIt - bucketIds.begin()) *
                  CuckooFilter::TagsPerBucket();
              for (u64 j = 0; j < CuckooFilter::TagsPerBucket(); ++j) {
                bucketTags[outputOffset + j] = loadTag(decodedBucket, j);
              }
            }
            ++cuckooOffset;
          }
        }

        std::cout << "comm.batchpir_total_mb="
                  << (queryCipherEnd - queryCipherStart) / std::pow(2.0, 20)
                  << "\n";
        std::cout << "comm.batchpir_keys_mb="
                  << keyBytes / std::pow(2.0, 20) << "\n";
        std::cout << "comm.batchpir_query_mb="
                  << queryBytes / std::pow(2.0, 20) << "\n";
        std::cout << "comm.batchpir_response_mb="
                  << responseBytes / std::pow(2.0, 20) << "\n";
        std::cout << "param.batchpir_internal_buckets="
                  << static_cast<u64>(ceil(params.get_batch_size() *
                                           params.get_cuckoo_factor()))
                  << "\n";
        std::cout << "param.batchpir_dim="
                  << params.get_first_dimension_size() << "\n";
        std::cout << "param.batchpir_poly_degree="
                  << encryptionParams.poly_modulus_degree() << "\n";
#else
        throw std::runtime_error("BatchPIR support was not built");
#endif
      } else {
        macoro::sync_wait(ch.send(bucketCount));
        macoro::sync_wait(ch.send(bucketIds));
        macoro::sync_wait(ch.recv(bucketTags));
      }

      for (u64 i = 0; i < bucketIds.size(); ++i) {
        bucketOffset.emplace(i == 0 ? bucketIds[i] : bucketIds[i],
                             i * CuckooFilter::TagsPerBucket());
      }

      auto bucketContains = [&](u64 bucketId, u32 tag) {
        auto it = bucketOffset.find(bucketId);
        if (it == bucketOffset.end()) {
          return false;
        }
        u64 offset = it->second;
        for (u64 i = 0; i < CuckooFilter::TagsPerBucket(); ++i) {
          if (bucketTags[offset + i] == tag) {
            return true;
          }
        }
        return false;
      };

      for (const auto &query : queries) {
        if (bucketContains(query.i1, query.tag) ||
            bucketContains(query.i2, query.tag)) {
          psi++;
        }
      }

      std::cout << "param.cf_buckets=" << bucketIds.size() << "\n";
      if (!batchPirCf) {
        double indexedPayload =
            (bucketIds.size() * sizeof(u64) + bucketTags.size() * sizeof(u32)) /
            std::pow(2.0, 20);
        std::cout << "comm.indexed_cf_payload_mb=" << indexedPayload << "\n";
      }
    }

    timer.setTimePoint("Receiver intersection computed");
    auto receiverSetupMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timer["Receiver setup skipped"] - timer["Receiver start"]).count();
    if (!indexedCf) {
      receiverSetupMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              timer["Receiver setup finished"] - timer["Receiver start"]).count();
    }
    auto receiverVoleMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timer["Receiver silent VOLE finished"] - timer["Receiver start"]).count();
    auto receiverTotalMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timer["Receiver intersection computed"] - timer["Receiver start"]).count();

    std::cout << "time.receiver_setup_ms=" << receiverSetupMs << "\n";
    std::cout << "time.receiver_vole_done_ms=" << receiverVoleMs << "\n";
    std::cout << "time.receiver_total_ms=" << receiverTotalMs << "\n";
    std::cout << "result.intersection=" << psi << "\n";

//  Matrix<u8> matrixR(width, heightInBytes, AllocType::Uninitialized);
//  BitVector choices(width);
//  cp::sync_wait(ch.recv(matrixR));
//  cp::sync_wait(ch.recv(choices));
//  for (u64 i = 0; i < width; i++) {
//    if (choices[i]) {
//      for (u64 j = 0; j < heightInBytes; j++) {
//        if (matrixA[i][j] != (matrixR[i][j] ^ matrixD[i][j])) {
//          throw RTE_LOC;
//        }
//      }
//    } else {
//      for (u64 j = 0; j < heightInBytes; j++) {
//        if (matrixA[i][j] != matrixR[i][j]) {
//          throw RTE_LOC;
//        }
//      }
//    }
//  }
//  cout << "matrix checked\n";

    u64 sentData = ch.bytesSent();
    u64 recvData = ch.bytesReceived();
    u64 totalData = sentData + recvData;
    u64 onlineData = totalData - setupData;

//  std::cout << "Receiver sent communication: " << sentData / std::pow(2.0, 20) << " MB\n";
//  std::cout << "Receiver received communication: " << recvData / std::pow(2.0, 20) << " MB\n";
    std::cout << "comm.setup_mb=" << setupData / std::pow(2.0, 20) << "\n";
    std::cout << "comm.online_mb=" << onlineData / std::pow(2.0, 20) << "\n";
    std::cout << "comm.total_mb=" << totalData / std::pow(2.0, 20) << "\n";
  }

}

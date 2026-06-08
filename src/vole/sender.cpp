#include "vole.hpp"

#include <array>

#ifdef PSI_ENABLE_BATCHPIR
#include "batchpirclient.h"
#include "batchpirparams.h"
#include "batchpirserver.h"
#include "src/utils.h"
#include <seal/seal.h>

#include <chrono>
#include <cmath>
#include <sstream>
#endif

using namespace std;

namespace vole {

  namespace {
    using Clock = std::chrono::high_resolution_clock;

    i64 elapsedMs(Clock::time_point start,
                  Clock::time_point end = Clock::now()) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();
    }
  }

#ifdef PSI_ENABLE_BATCHPIR
  namespace {
    constexpr size_t kCfBucketEntrySize = CuckooFilter::TagsPerBucket() * sizeof(u32);
    constexpr size_t kCfBatchPirPolyModulusDegree = 8192;

    template <typename T>
    T loadSealObject(const seal::SEALContext &context, const vector<u8> &bytes) {
      T value;
      string data(reinterpret_cast<const char *>(bytes.data()), bytes.size());
      stringstream stream(data, ios::in | ios::out | ios::binary);
      value.load(context, stream);
      return value;
    }

    vector<u8> saveSealObject(const auto &value) {
      stringstream stream(ios::in | ios::out | ios::binary);
      value.save(stream);
      auto data = stream.str();
      return vector<u8>(data.begin(), data.end());
    }

    void recvBytes(Socket &ch, vector<u8> &bytes) {
      u64 size = 0;
      macoro::sync_wait(ch.recv(size));
      bytes.resize(size);
      if (size) {
        macoro::sync_wait(ch.recv(bytes));
      }
    }

    void sendBytes(Socket &ch, const vector<u8> &bytes) {
      u64 size = bytes.size();
      macoro::sync_wait(ch.send(size));
      if (size) {
        macoro::sync_wait(ch.send(bytes));
      }
    }

    seal::EncryptionParameters createCfBatchPirEncryptionParameters(u64 cfBuckets,
                                                                    u64 queryBuckets) {
      (void)cfBuckets;
      (void)queryBuckets;
      const size_t polyModulusDegree = kCfBatchPirPolyModulusDegree;
      seal::EncryptionParameters params(seal::scheme_type::bfv);
      params.set_poly_modulus_degree(polyModulusDegree);
      params.set_coeff_modulus(
          seal::CoeffModulus::Create(polyModulusDegree, {55, 55, 48, 60}));
      params.set_plain_modulus(seal::PlainModulus::Batching(polyModulusDegree, 22));
      return params;
    }

    pair<seal::GaloisKeys, seal::RelinKeys> recvBatchPirKeys(
        Socket &ch, const seal::SEALContext &context) {
      vector<u8> bytes;
      recvBytes(ch, bytes);
      auto galoisKeys = loadSealObject<seal::GaloisKeys>(context, bytes);
      recvBytes(ch, bytes);
      auto relinKeys = loadSealObject<seal::RelinKeys>(context, bytes);
      return {galoisKeys, relinKeys};
    }

    vector<PIRQuery> recvBatchPirQueries(Socket &ch,
                                         const seal::SEALContext &context) {
      u64 queryCount = 0;
      macoro::sync_wait(ch.recv(queryCount));
      vector<PIRQuery> queries(queryCount);
      for (auto &query : queries) {
        u64 ciphertextCount = 0;
        macoro::sync_wait(ch.recv(ciphertextCount));
        query.resize(ciphertextCount);
        for (auto &ciphertext : query) {
          vector<u8> bytes;
          recvBytes(ch, bytes);
          ciphertext = loadSealObject<seal::Ciphertext>(context, bytes);
        }
      }
      return queries;
    }

    void sendBatchPirResponses(Socket &ch, const PIRResponseList &responses) {
      u64 responseCount = responses.size();
      macoro::sync_wait(ch.send(responseCount));
      for (const auto &response : responses) {
        sendBytes(ch, saveSealObject(response));
      }
    }

    RawDB makeCfRawDb(const CuckooFilter &cf) {
      RawDB rawDb(cf.NumBuckets(), vector<unsigned char>(kCfBucketEntrySize));
      for (u64 bucket = 0; bucket < cf.NumBuckets(); ++bucket) {
        auto tags = cf.ReadBucketTags(bucket);
        for (u64 slot = 0; slot < tags.size(); ++slot) {
          const auto tag = tags[slot];
          auto offset = slot * sizeof(u32);
          rawDb[bucket][offset + 0] = static_cast<unsigned char>(tag & 0xff);
          rawDb[bucket][offset + 1] = static_cast<unsigned char>((tag >> 8) & 0xff);
          rawDb[bucket][offset + 2] = static_cast<unsigned char>((tag >> 16) & 0xff);
          rawDb[bucket][offset + 3] = static_cast<unsigned char>((tag >> 24) & 0xff);
        }
      }
      return rawDb;
    }

    i64 printElapsed(const char *label,
                     std::chrono::high_resolution_clock::time_point start) {
      auto end = std::chrono::high_resolution_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
      std::cout << label << "=" << ms.count() << "\n";
      return ms.count();
    }
  }
#endif

  void VOLE::runSender() {
    auto ch = cp::asioConnect("127.0.0.1:7700", true);

    vector<block> senderSet(senderSize);
    PRNG prng(oc::toBlock(123));
    prng.get<block>(senderSet);

    runSender(PRNG(sysRandomSeed()), ch, senderSet);

    macoro::sync_wait(ch.flush());
  }

  void VOLE::runSender(PRNG prng, Socket ch, const std::vector<block> &senderSet) {
    auto setupStart = Clock::now();

    PRNG commonPrng(commonSeed);
    vector<u128> cfParams(3);
    commonPrng.get((u8 *) cfParams.data(), cfParams.size() * sizeof(u128));

    Paxos<u32> okvs;
    block okvsSeed = commonPrng.get<block>();
    okvs.init(receiverSize, 3, 40, PaxosParam::Binary, okvsSeed);
    u64 kSize = okvs.size();

    block delta = prng.get<block>();
    delta = OneBlock;
    vector<block> okvsData(kSize);
    commonPrng.get<block>(okvsData);
    vector<block> decodedValues(senderSize);
    okvs.decode<block>(senderSet, decodedValues, okvsData);

    CuckooFilter cf(senderSize);
    cf.SetTwoIndependentMultiplyShiftParams(cfParams);

    // Silent VOLE
    SilentVoleSender<block, block, CoeffCtxGF128> voleSender;
    voleSender.mMalType = SilentSecType::SemiHonest;
    voleSender.configure(kSize, SilentBaseType::Base);

    // Noisy VOLE
//    CoeffCtxGF128 ctx;
//    NoisyVoleSender<block, block, CoeffCtxGF128> noisyVoleSender;

    HASH hash;
    block* pblock = (block*) hash;
    for (u64 i = 0; i < senderSize; i++) {
      SHA256(senderSet[i].data(), sizeof(block), hash);
      block b = decodedValues[i] ^ (delta.gf128Mul(*pblock));
      SHA256((u8 *) &b, sizeof(block), hash);
      cf.Add((u64 *) &hash);
    }

    auto senderSetupMs = elapsedMs(setupStart);
    std::cout << "time.sender_setup_ms=" << senderSetupMs << "\n";

    if (!indexedCf) {
      vector<u8> cfData = cf.serialize();
      u64 cfSize = cfData.size();
      macoro::sync_wait(ch.send(cfSize));
      macoro::sync_wait(ch.send(cfData));
    }
    macoro::sync_wait(ch.send(senderSetupMs));

    AlignedUnVector<block> voleC(kSize);

    // Silent VOLE
    macoro::sync_wait(voleSender.silentSend(delta, voleC, prng, ch));

    // Noisy VOLE
//    IknpOtExtReceiver otExtReceiver;
//    macoro::sync_wait(noisyVoleSender.send(delta, voleC, prng, otExtReceiver, ch, ctx));

    vector<block> voleBxor(kSize);
    macoro::sync_wait(ch.recv(voleBxor));

    for (u64 i = 0; i < kSize; i++) {
      okvsData[i] = okvsData[i] ^ voleC[i] ^ delta.gf128Mul(voleBxor[i]);
    }
    macoro::sync_wait(ch.send(okvsData));

    i64 senderBatchPirServerPrepMs = 0;
    i64 senderBatchPirRecvQueriesMs = 0;
    i64 senderBatchPirResponseMs = 0;
    i64 senderBatchPirSendResponsesMs = 0;

    if (batchPirCf) {
#ifdef PSI_ENABLE_BATCHPIR
      u64 bucketCount;
      macoro::sync_wait(ch.recv(bucketCount));

      auto encryptionParams =
          createCfBatchPirEncryptionParameters(cf.NumBuckets(), bucketCount);
      seal::SEALContext context(encryptionParams);
      BatchPirParams params(static_cast<int>(bucketCount), cf.NumBuckets(),
                            kCfBucketEntrySize, encryptionParams);

      auto bpStart = std::chrono::high_resolution_clock::now();
      BatchPIRServer server(params, makeCfRawDb(cf));
      senderBatchPirServerPrepMs =
          printElapsed("time.sender_batchpir_server_prep_ms", bpStart);
      std::cout << "param.batchpir_poly_degree="
                << encryptionParams.poly_modulus_degree() << "\n";
      u64 maxBucketSize = params.get_max_bucket_size();
      macoro::sync_wait(ch.send(maxBucketSize));
      auto keys = recvBatchPirKeys(ch, context);
      auto stepStart = std::chrono::high_resolution_clock::now();
      auto queries = recvBatchPirQueries(ch, context);
      senderBatchPirRecvQueriesMs =
          printElapsed("time.sender_batchpir_recv_queries_ms", stepStart);
      server.set_client_keys(0, keys);
      stepStart = std::chrono::high_resolution_clock::now();
      auto responses = server.generate_response(0, queries);
      senderBatchPirResponseMs =
          printElapsed("time.sender_batchpir_response_ms", stepStart);
      stepStart = std::chrono::high_resolution_clock::now();
      sendBatchPirResponses(ch, responses);
      senderBatchPirSendResponsesMs =
          printElapsed("time.sender_batchpir_send_responses_ms", stepStart);
      std::array<i64, 3> senderBatchPirTimings{
          senderBatchPirRecvQueriesMs,
          senderBatchPirResponseMs,
          senderBatchPirSendResponsesMs};
      macoro::sync_wait(ch.send(senderBatchPirTimings));
      std::cout << "param.cf_buckets=" << bucketCount << "\n";
#else
      throw std::runtime_error("BatchPIR support was not built");
#endif
    } else if (indexedCf) {
      u64 bucketCount;
      macoro::sync_wait(ch.recv(bucketCount));
      vector<u64> bucketIds;
      bucketIds.resize(bucketCount);
      macoro::sync_wait(ch.recv(bucketIds));

      vector<u32> bucketTags(bucketIds.size() * CuckooFilter::TagsPerBucket());
      for (u64 i = 0; i < bucketIds.size(); ++i) {
        auto tags = cf.ReadBucketTags(bucketIds[i]);
        for (u64 j = 0; j < tags.size(); ++j) {
          bucketTags[i * tags.size() + j] = tags[j];
        }
      }
      macoro::sync_wait(ch.send(bucketTags));
      std::cout << "param.cf_buckets=" << bucketIds.size() << "\n";
    }

    auto senderTotalMs = senderSetupMs + senderBatchPirServerPrepMs;
    std::cout << "time.sender_total_ms=" << senderTotalMs << "\n";
  }

}

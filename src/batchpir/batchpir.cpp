#include "batchpir.hpp"

#ifdef PSI_ENABLE_BATCHPIR

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <cryptoTools/Common/CLP.h>

#include "batchpirclient.h"
#include "batchpirparams.h"
#include "batchpirserver.h"
#include "src/utils.h"

namespace batchpir {

  namespace {

    std::vector<uint64_t> makeQueryBatch(size_t batchSize, size_t numEntries) {
      std::vector<uint64_t> batch(batchSize);
      for (size_t i = 0; i < batch.size(); ++i) {
        batch[i] = static_cast<uint64_t>(std::rand()) % numEntries;
      }
      return batch;
    }

  }

  int main(int argc, char **argv) {
    oc::CLP cmd;
    cmd.parse(argc, argv);

    cmd.setDefault("bp_batch", 32);
    cmd.setDefault("bp_n", 1048576);
    cmd.setDefault("bp_s", 32);

    const auto batchSize = cmd.get<int>("bp_batch");
    const auto numEntries = cmd.get<size_t>("bp_n");
    const auto entrySize = cmd.get<size_t>("bp_s");
    constexpr uint32_t clientId = 0;

    if (batchSize <= 0 || numEntries == 0 || entrySize == 0) {
      throw std::invalid_argument("BatchPIR parameters must be positive");
    }

    std::cout << "BatchPIR parameters: batch=" << batchSize
              << ", entries=" << numEntries
              << ", entry_size=" << entrySize << "\n";

    const auto selection = std::to_string(batchSize) + "," +
                           std::to_string(numEntries) + "," +
                           std::to_string(entrySize);
    auto encryptionParams = utils::create_encryption_parameters(selection);
    BatchPirParams params(batchSize, numEntries, entrySize, encryptionParams);
    params.print_params();

    auto start = std::chrono::high_resolution_clock::now();
    BatchPIRServer server(params);
    auto end = std::chrono::high_resolution_clock::now();
    auto initMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    BatchPIRClient client(params);
    client.set_map(server.get_hash_map());
    server.set_client_keys(clientId, client.get_public_keys());

    auto queryBatch = makeQueryBatch(static_cast<size_t>(batchSize), numEntries);

    start = std::chrono::high_resolution_clock::now();
    auto queries = client.create_queries(queryBatch);
    end = std::chrono::high_resolution_clock::now();
    auto queryMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    start = std::chrono::high_resolution_clock::now();
    auto responses = server.generate_response(clientId, queries);
    end = std::chrono::high_resolution_clock::now();
    auto responseMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    auto decodedResponses = client.decode_responses_chunks(responses);
    const bool matched =
        server.check_decoded_entries(decodedResponses, client.get_cuckoo_table());

    std::cout << "BatchPIR init: " << initMs.count() << " ms\n";
    std::cout << "BatchPIR query generation: " << queryMs.count() << " ms\n";
    std::cout << "BatchPIR response generation: " << responseMs.count()
              << " ms\n";
    std::cout << "BatchPIR communication: "
              << client.get_serialized_commm_size() << " KB\n";
    std::cout << "BatchPIR decoded entries matched: "
              << (matched ? "yes" : "no") << "\n";

    return matched ? 0 : 1;
  }

}

#else

#include <iostream>

namespace batchpir {

  int main(int, char **) {
    std::cerr << "BatchPIR support was not built because Microsoft SEAL was not found.\n";
    return 1;
  }

}

#endif

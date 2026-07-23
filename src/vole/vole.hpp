#pragma once

#include <cryptoTools/Network/IOService.h>
#include <cryptoTools/Network/Endpoint.h>
#include <cryptoTools/Network/Channel.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <cryptoTools/Crypto/RandomOracle.h>
#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Common/Timer.h>
#include <coproto/Socket/AsioSocket.h>
#include <libOTe/TwoChooseOne/Iknp/IknpOtExtSender.h>
#include <libOTe/TwoChooseOne/Iknp/IknpOtExtReceiver.h>
#include <libOTe/Vole/Silent/SilentVoleSender.h>
#include <libOTe/Vole/Silent/SilentVoleReceiver.h>
#include <cryptoTools/Common/Matrix.h>
#include "cuckoofilter.h"

#include "volePSI/Paxos.h"

#include <openssl/sha.h>
#include <string>
#include <utility>

using volePSI::Paxos;
using volePSI::Baxos;
using volePSI::PaxosParam;
using namespace oc;
using u128 = unsigned __int128;
using CuckooFilter = cuckoofilter::CuckooFilter<
    uint64_t *, 32, cuckoofilter::SingleTable,
    cuckoofilter::TwoIndependentMultiplyShift128>;
using HASH = unsigned char[SHA256_DIGEST_LENGTH];
using HASH64 = unsigned char[SHA512_DIGEST_LENGTH];

namespace vole {

  class VOLE {
  public:
    const block commonSeed;
    const u64 senderSize;
    const u64 receiverSize;
    const bool indexedCf;
    const bool batchPirCf;
    const u64 batchPirChunkSize;
    const u64 updateSize;
    const std::string updateOp;

    VOLE(block commonSeed,
         u64 senderSize,
         u64 receiverSize,
         bool indexedCf = false,
         bool batchPirCf = false,
         u64 batchPirChunkSize = 64,
         u64 updateSize = 0,
         std::string updateOp = "insert"
    )
        : commonSeed(commonSeed),
          senderSize(senderSize),
          receiverSize(receiverSize),
          indexedCf(indexedCf),
          batchPirCf(batchPirCf),
          batchPirChunkSize(batchPirChunkSize),
          updateSize(updateSize),
          updateOp(std::move(updateOp)) {}

    void runSender(PRNG prng, Socket ch, const std::vector<block> &senderSet);
    void runReceiver(PRNG prng, Socket ch, const std::vector<block> &receiverSet);

    void runSender();
    void runReceiver();
  };

  int main(int argc, char **argv);
}

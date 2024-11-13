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
#include <cryptoTools/Common/Matrix.h>
#include "cuckoofilter.h"

using namespace oc;
using u128 = unsigned __int128;
using CuckooFilter = cuckoofilter::CuckooFilter<
    uint64_t *, 32, cuckoofilter::SingleTable,
    cuckoofilter::TwoIndependentMultiplyShift128>;

namespace prec {

class PreC {
 public:
  const block commonSeed;
  const u64 senderSize;
  const u64 receiverSize;
  const u64 width;
  const u64 height;
  const u64 logHeight;
  const u64 hashLengthInBytes;
  const u64 h1LengthInBytes;
  const u64 bucket1, bucket2;

  PreC(block commonSeed,
       u64 senderSize,
       u64 receiverSize,
       u64 width,
       u64 height,
       u64 logHeight,
       u64 hashLengthInBytes,
       u64 h1LengthInBytes,
       u64 bucket1,
       u64 bucket2)
      : commonSeed(commonSeed),
        senderSize(senderSize),
        receiverSize(receiverSize),
        width(width),
        height(height),
        logHeight(logHeight),
        hashLengthInBytes(hashLengthInBytes),
        h1LengthInBytes(h1LengthInBytes),
        bucket1(bucket1),
        bucket2(bucket2) {}

  void runSender(PRNG prng, Socket ch, const std::vector<block> &senderSet);
  void runReceiver(PRNG prng, Socket ch, const std::vector<block> &receiverSet);

  void runSender();
  void runReceiver();
};

int main(int argc, char **argv);
}
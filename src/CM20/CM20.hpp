#include <cryptoTools/Network/IOService.h>
#include <cryptoTools/Network/Endpoint.h>
#include <cryptoTools/Network/Channel.h>
#include <cryptoTools/Crypto/PRNG.h>
//#include <cryptoTools/Crypto/Curve.h>
#include <cryptoTools/Crypto/RandomOracle.h>
#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Common/Timer.h>
#include <coproto/Socket/AsioSocket.h>
#include <libOTe/TwoChooseOne/Iknp/IknpOtExtSender.h>
#include <libOTe/TwoChooseOne/Iknp/IknpOtExtReceiver.h>

using namespace oc;

namespace CM20 {

void ReceiverRun(PRNG &prng,
                 Socket &ch,
                 block commonSeed,
                 const u64 &senderSize,
                 const u64 &receiverSize,
                 const u64 &height,
                 const u64 &logHeight,
                 const u64 &width,
                 std::vector<block> &receiverSet,
                 const u64 &hashLengthInBytes,
                 const u64 &h1LengthInBytes,
                 const u64 &bucket1,
                 const u64 &bucket2);
void SenderRun(PRNG &prng,
               Socket &ch,
               block commonSeed,
               const u64 &senderSize,
               const u64 &receiverSize,
               const u64 &height,
               const u64 &logHeight,
               const u64 &width,
               std::vector<block> &senderSet,
               const u64 &hashLengthInBytes,
               const u64 &h1LengthInBytes,
               const u64 &bucket1,
               const u64 &bucket2);

int main(int argc, char **argv);

}
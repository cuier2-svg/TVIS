#include "vole.hpp"

using namespace std;

namespace vole {

  void VOLE::runSender() {
    auto ch = cp::asioConnect("127.0.0.1:7700", true);

    vector<block> senderSet(senderSize);
    PRNG prng(oc::toBlock(123));
    prng.get<block>(senderSet);

    runSender(PRNG(sysRandomSeed()), ch, senderSet);

    macoro::sync_wait(ch.flush());
  }

  void VOLE::runSender(PRNG prng, Socket ch, const std::vector<block> &senderSet) {
    Timer timer;
    timer.setTimePoint("Sender start");

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

    vector<u8> cfData = cf.serialize();
    timer.setTimePoint("Sender setup done");

    u64 cfSize = cfData.size();
    macoro::sync_wait(ch.send(cfSize));
    macoro::sync_wait(ch.send(cfData));
    timer.setTimePoint("Sender setup sent");

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

    timer.setTimePoint("Sender done");
    cout << timer << endl;
  }

}

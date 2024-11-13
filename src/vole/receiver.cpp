#include "vole.hpp"

using namespace std;

namespace vole {

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
    cout << "okvs size: " << kSize << endl;

    vector<block> okvsData(kSize);
    commonPrng.get<block>(okvsData);

    // Slient VOLE
    SilentVoleReceiver<block, block, CoeffCtxGF128> voleReceiver;
    voleReceiver.mMalType = SilentSecType::SemiHonest;
    voleReceiver.configure(kSize, SilentBaseType::Base);

    // Noisy VOLE
//    CoeffCtxGF128 ctx;
//    NoisyVoleReceiver<block, block, CoeffCtxGF128> noisyVoleReceiver;

    u64 cfSize;
    cp::sync_wait(ch.recv(cfSize));
    timer.setTimePoint("Receiver setup start");
    vector<u8> cfData(cfSize);
    cp::sync_wait(ch.recv(cfData));
    CuckooFilter cf(senderSize);
    cf.SetTwoIndependentMultiplyShiftParams(cfParams);
    cf.deserialize(cfData);
//  cout << cf.Info() << endl;

    timer.setTimePoint("Receiver setup finished");
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
    std::cout << "Receiver VOLE communication: " << voleData / std::pow(2.0, 20) << " MB\n";

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
    for (u64 i = 0; i < receiverSize; i++) {
      SHA256((u8 *) &decodedValues[i], sizeof(block), hash);
      if (cf.Contain((u64 *) &hash) == cuckoofilter::Status::Ok) {
        psi++;
      }
    }

    timer.setTimePoint("Receiver intersection computed");
    cout << timer << endl;

    std::cout << "Receiver intersection size: " << psi << "\n";
    if (psi == 100) {
      std::cout << "Receiver intersection computed - correct!\n";
    }

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
    std::cout << "Receiver setup communication: " << setupData / std::pow(2.0, 20) << " MB\n";
    std::cout << "Receiver online communication: " << onlineData / std::pow(2.0, 20) << " MB\n";
    std::cout << "Receiver total communication: " << totalData / std::pow(2.0, 20) << " MB\n";
  }

}

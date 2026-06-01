#include "prec.hpp"

using namespace std;

namespace prec {

namespace {

i64 elapsedMs(Timer::timeUnit start, Timer::timeUnit end) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
      .count();
}

}

void PreC::runSender() {
  auto ch = cp::asioConnect("127.0.0.1:7700", true);

  vector<block> senderSet(senderSize);
  PRNG prng(oc::toBlock(123));
  for (u64 i = 0; i < senderSize; ++i) {
    senderSet[i] = prng.get<block>();
  }

  runSender(PRNG(sysRandomSeed()), ch, senderSet);

  cp::sync_wait(ch.flush());
}

void PreC::runSender(PRNG prng, Socket ch, const std::vector<block> &senderSet) {
  Timer timer;
  timer.setTimePoint("Sender start");

  u64 heightInBytes = (height + 7) / 8;
  u64 widthInBytes = (width + 7) / 8;
  u64 locationInBytes = (logHeight + 7) / 8;
  u64 senderSizeInBytes = (senderSize + 7) / 8;
  u64 shift = (1 << logHeight) - 1;
  u64 widthBucket1 = sizeof(block) / locationInBytes;
  u64 numKeys = (width + widthBucket1 - 1) / widthBucket1;

  PRNG commonPrng(commonSeed);
  vector<u128> cfParams(3);
  commonPrng.get((u8 *) cfParams.data(), cfParams.size() * sizeof(u128));
  vector<block> commonKeys(numKeys);
  commonPrng.get(commonKeys.data(), commonKeys.size());
  AES commonAes;

  Matrix<u8> matrixR(width, heightInBytes, AllocType::Uninitialized);
  prng.get(matrixR.data(), matrixR.size());

  RandomOracle H(hashLengthInBytes);
  u8 hashOutput[sizeof(block)];

  CuckooFilter cf(senderSize);
  cf.SetTwoIndependentMultiplyShiftParams(cfParams);
  Matrix<u8> senderHashes(senderSize, hashLengthInBytes, AllocType::Uninitialized);
  Matrix<u8> hashInputs(bucket1, widthInBytes, AllocType::Uninitialized);
  vector<block> randomLocations(bucket1);

  for (u64 low = 0; low < senderSize; low += bucket1) {
    memset(hashInputs.data(), 0, hashInputs.size());
    u64 batch = low + bucket1 < senderSize ? bucket1 : senderSize - low;

    for (u64 wLeft = 0; wLeft < width; wLeft += widthBucket1) {
      u64 wRight = wLeft + widthBucket1 < width ? wLeft + widthBucket1 : width;

      commonAes.setKey(commonKeys[wLeft / widthBucket1]);
      commonAes.ecbEncBlocks(senderSet.data() + low, batch, randomLocations.data());

      for (u64 i = 0; i < batch; i++) {
        for (u64 j = wLeft; j < wRight; j++) {
          u16 location = *((u16 *) &randomLocations[i] + (j - wLeft)) & shift;
          hashInputs[i][j >> 3] |= (u8) ((bool) (matrixR[j][location >> 3] & (1 << (location & 7)))) << (j & 7);
        }
      }
    }

    for (u64 i = 0; i < batch; i++) {
      H.Reset();
      H.Update(hashInputs[i].data(), widthInBytes);
      H.Final(hashOutput);
      cf.Add((u64 *) &hashOutput);
//      memcpy(senderHashes[j].data(), hashOutput, hashLengthInBytes);
    }
  }

  vector<u8> cfData = cf.serialize();
  timer.setTimePoint("Sender setup done");

//  cp::sync_wait(ch.send(senderHashes));
//  cout << cf.Info() << endl;
  u64 cfSize = cfData.size();
  cp::sync_wait(ch.send(cfSize));
  cp::sync_wait(ch.send(cfData));

  timer.setTimePoint("Sender setup sent");

  IknpOtExtReceiver otExtReceiver;
  BitVector choices(width);
  std::vector<block> otMessages(width);
  prng.get(choices.data(), choices.sizeBytes());
  cp::sync_wait(otExtReceiver.receive(choices, otMessages, prng, ch));

  Matrix<u8> matrixC(width, heightInBytes, AllocType::Uninitialized);
  Matrix<u8> matrixRecv(width, heightInBytes, AllocType::Uninitialized);
  cp::sync_wait(ch.recv(matrixRecv));
  PRNG prngOT;
  for (u64 i = 0; i < width; i++) {
    prngOT.SetSeed(otMessages[i], heightInBytes);
    memcpy(matrixC[i].data(), prngOT.mBuffer.data(), heightInBytes);
    if (choices[i]) {
      for (u64 j = 0; j < heightInBytes; j++) {
        matrixC[i][j] ^= matrixRecv[i][j];
      }
    }
  }

  Matrix<u8> matrixP(width, heightInBytes, AllocType::Uninitialized);
  for (u64 i = 0; i < width; ++i) {
    for (u64 j = 0; j < heightInBytes; ++j) {
      matrixP[i][j] = matrixC[i][j] ^ matrixR[i][j];
    }
  }
  cp::sync_wait(ch.send(matrixP));

  timer.setTimePoint("Sender done");
  std::cout << "time.sender_setup_ms="
            << elapsedMs(timer["Sender start"],
                         timer["Sender setup done"]) << "\n";
  std::cout << "time.sender_setup_send_ms="
            << elapsedMs(timer["Sender setup done"],
                         timer["Sender setup sent"]) << "\n";
  std::cout << "time.sender_online_ms="
            << elapsedMs(timer["Sender setup sent"],
                         timer["Sender done"]) << "\n";
  std::cout << "time.sender_total_ms="
            << elapsedMs(timer["Sender start"],
                         timer["Sender done"]) << "\n";

//  cp::sync_wait(ch.send(matrixR));
//  cp::sync_wait(ch.send(choices));
}

}

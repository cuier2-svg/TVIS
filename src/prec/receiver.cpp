#include "prec.hpp"

using namespace std;

namespace prec {

namespace {

i64 elapsedMs(Timer::timeUnit start, Timer::timeUnit end) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
      .count();
}

}

void PreC::runReceiver() {
  auto ch = cp::asioConnect("127.0.0.1:7700", false);

  vector<block> receiverSet(receiverSize);
  PRNG prng(oc::toBlock(123));
//	for (auto i = 0; i < 100; ++i) {
//		receiverSet[i] = prng.get<block>();
//	}

  PRNG prng2(oc::toBlock(456));
  for (u64 i = 0; i < receiverSize; ++i) {
    receiverSet[i] = prng.get<block>();
  }

  receiverSet[2] = prng2.get<block>();
  receiverSet[3] = prng2.get<block>();
  receiverSet[5] = prng2.get<block>();
  receiverSet[7] = prng2.get<block>();

//  for (u64 i = 0; i < 100; ++i) {
//    u64 pos = 4 * i * i + 3;
//    receiverSet[pos % receiverSize] = prng.get<block>();
//  }

  runReceiver(PRNG(sysRandomSeed()), ch, receiverSet);

  cp::sync_wait(ch.flush());
}

void PreC::runReceiver(osuCrypto::PRNG prng, osuCrypto::Socket ch, const std::vector<block> &receiverSet) {
  Timer timer;
  timer.setTimePoint("Receiver start");

  auto heightInBytes = (height + 7) / 8;
  auto widthInBytes = (width + 7) / 8;
  auto locationInBytes = (logHeight + 7) / 8;
  auto receiverSizeInBytes = (receiverSize + 7) / 8;
  auto shift = (1 << logHeight) - 1;
  auto widthBucket1 = sizeof(block) / locationInBytes;
  u64 numKeys = (width + widthBucket1 - 1) / widthBucket1;

  PRNG commonPrng(commonSeed);
  vector<u128> cfParams(3);
  commonPrng.get((u8 *) cfParams.data(), cfParams.size() * sizeof(u128));
  vector<block> commonKeys(numKeys);
  commonPrng.get(commonKeys.data(), commonKeys.size());
  AES commonAes;

//  Matrix<u8> senderHashes(senderSize, hashLengthInBytes);
//  cp::sync_wait(ch.recv(senderHashes));
  timer.setTimePoint("Receiver setup start");
  u64 cfSize;
  cp::sync_wait(ch.recv(cfSize));
  vector<u8> cfData(cfSize);
  cp::sync_wait(ch.recv(cfData));
  CuckooFilter cf(senderSize);
  cf.SetTwoIndependentMultiplyShiftParams(cfParams);
  cf.deserialize(cfData);
//  cout << cf.Info() << endl;

  timer.setTimePoint("Receiver setup finished");
  u64 setupData = (ch.bytesSent() + ch.bytesReceived());

  Matrix<u8> matrixD(width, heightInBytes, AllocType::Uninitialized);
  memset(matrixD.data(), 255, matrixD.size());

  vector<block> randomLocations(bucket1);
  Matrix<u16> locations(width, receiverSize, AllocType::Uninitialized);

  for (u64 low = 0; low < receiverSize; low += bucket1) {
    u64 batch = low + bucket1 < receiverSize ? bucket1 : receiverSize - low;

    for (u64 wLeft = 0; wLeft < width; wLeft += widthBucket1) {
      u64 wRight = wLeft + widthBucket1 < width ? wLeft + widthBucket1 : width;
      u64 w = wRight - wLeft;

      commonAes.setKey(commonKeys[wLeft / widthBucket1]);
      commonAes.ecbEncBlocks(receiverSet.data() + low, batch, randomLocations.data());

      for (u64 i = 0; i < batch; i++) {
        for (u64 j = 0; j < w; j++) {
          u16 location = *((u16 *) &randomLocations[i] + j) & shift;
          locations[wLeft + j][low + i] = location;
          matrixD[wLeft + j][location >> 3] &= ~(1 << (location & 7));
        }
      }
    }
  }

  timer.setTimePoint("MatrixD computed");

  IknpOtExtSender otExtSender;
  std::vector<std::array<block, 2> > otMessages(width);
  cp::sync_wait(otExtSender.send(otMessages, prng, ch));

  Matrix<u8> matrixA(width, heightInBytes, AllocType::Uninitialized);
  Matrix<u8> matrixSent(width, heightInBytes, AllocType::Uninitialized);
  PRNG prngOT;
  for (u64 i = 0; i < width; i++) {
    prngOT.SetSeed(otMessages[i][0], heightInBytes);
    memcpy(matrixA[i].data(), prngOT.mBuffer.data(), heightInBytes);
    prngOT.SetSeed(otMessages[i][1], heightInBytes);
    memcpy(matrixSent[i].data(), prngOT.mBuffer.data(), heightInBytes);
    for (u64 j = 0; j < heightInBytes; j++) {
      matrixSent[i][j] ^= matrixA[i][j] ^ matrixD[i][j];
    }
  }
  cp::sync_wait(ch.send(matrixSent));

  Matrix<u8> matrixP(width, heightInBytes, AllocType::Uninitialized);
  cp::sync_wait(ch.recv(matrixP));
  for (u64 i = 0; i < width; i++) {
    for (u64 j = 0; j < heightInBytes; j++) {
      matrixA[i][j] ^= matrixP[i][j];
    }
  }

  timer.setTimePoint("MatrixA computed");

  u64 psi = 0;
  RandomOracle H(hashLengthInBytes);
  vector<u8> hashInput(widthInBytes);
//  block hashOutput;
  u8 hashOutput[sizeof(block)];

  unordered_map<u64, vector<block>> recvHashMap;
  vector<block> recvHashes;

  for (u64 i = 0; i < receiverSize; i++) {
    memset(hashInput.data(), 0, widthInBytes);
    for (u64 j = 0; j < width; j++) {
      u16 location = locations[j][i];
      hashInput[j >> 3] |= (u8) ((bool) (matrixA[j][location >> 3] & (1 << (location & 7)))) << (j & 7);
    }
    H.Reset();
    H.Update(hashInput.data(), widthInBytes);
    H.Final(hashOutput);
    if (cf.Contain((u64 *) &hashOutput) == cuckoofilter::Status::Ok) {
      ++psi;
    }
//    recvHashMap[*(u64 *) &hashOutput].push_back(hashOutput);
//    recvHashes.push_back(hashOutput);
  }

//  for (u64 i = 0; i < senderSize; i++) {
//    u64 mapIdx = *(u64 *) (senderHashes[i].data());
//
//    auto found = recvHashMap.find(mapIdx);
//    if (found == recvHashMap.end()) continue;
//
//    for (u64 j = 0; j < found->second.size(); ++j) {
//      ++psi;
//      break;
//    }
//  }

  timer.setTimePoint("Receiver intersection computed");
  std::cout << "time.receiver_setup_ms="
            << elapsedMs(timer["Receiver setup start"],
                         timer["Receiver setup finished"]) << "\n";
  std::cout << "time.receiver_matrix_d_ms="
            << elapsedMs(timer["Receiver setup finished"],
                         timer["MatrixD computed"]) << "\n";
  std::cout << "time.receiver_matrix_a_ms="
            << elapsedMs(timer["MatrixD computed"],
                         timer["MatrixA computed"]) << "\n";
  std::cout << "time.receiver_intersection_ms="
            << elapsedMs(timer["MatrixA computed"],
                         timer["Receiver intersection computed"]) << "\n";
  std::cout << "time.receiver_total_ms="
            << elapsedMs(timer["Receiver start"],
                         timer["Receiver intersection computed"]) << "\n";

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

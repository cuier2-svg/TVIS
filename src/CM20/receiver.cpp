#include "CM20.hpp"

namespace CM20 {

namespace {

i64 elapsedMs(Timer::timeUnit start, Timer::timeUnit end) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
      .count();
}

}

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
                 const u64 &bucket2) {

  Timer timer;

  timer.setTimePoint("Receiver start");

  auto heightInBytes = (height + 7) / 8;
  auto widthInBytes = (width + 7) / 8;
  auto locationInBytes = (logHeight + 7) / 8;
  auto receiverSizeInBytes = (receiverSize + 7) / 8;
  auto shift = (1 << logHeight) - 1;
  auto widthBucket1 = sizeof(block) / locationInBytes;


  ///////////////////// Base OTs ///////////////////////////

  IknpOtExtSender otExtSender;
//  otExtSender.genBaseOts(prng, ch);

  std::vector<std::array<block, 2> > otMessages(width);

  cp::sync_wait(otExtSender.send(otMessages, prng, ch));

  // std::cout << "Receiver base OT finished\n";
  timer.setTimePoint("Receiver base OT finished");




  //////////// Initialization ///////////////////

  PRNG commonPrng(commonSeed);
  block commonKey;
  AES commonAes;

  u8 *matrixA[widthBucket1];
  u8 *matrixDelta[widthBucket1];
  for (u64 i = 0; i < widthBucket1; ++i) {
    matrixA[i] = new u8[heightInBytes];
    matrixDelta[i] = new u8[heightInBytes];
  }

  u8 *transLocations[widthBucket1];
  for (u64 i = 0; i < widthBucket1; ++i) {
    transLocations[i] = new u8[receiverSize * locationInBytes + sizeof(u32)];
  }

  block randomLocations[bucket1];

  u8 *transHashInputs[width];
  for (u64 i = 0; i < width; ++i) {
    transHashInputs[i] = new u8[receiverSizeInBytes];
    memset(transHashInputs[i], 0, receiverSizeInBytes);
  }

  // std::cout << "Receiver initialized\n";
  timer.setTimePoint("Receiver initialized");




  /////////// Transform input /////////////////////

  commonPrng.get((u8 * ) & commonKey, sizeof(block));
  commonAes.setKey(commonKey);

  block *recvSet = new block[receiverSize];
  block *aesInput = new block[receiverSize];
  block *aesOutput = new block[receiverSize];

  RandomOracle H1(h1LengthInBytes);
  u8 h1Output[h1LengthInBytes];

  for (u64 i = 0; i < receiverSize; ++i) {
    H1.Reset();
    H1.Update((u8 * )(receiverSet.data() + i), sizeof(block));
    H1.Final(h1Output);

    aesInput[i] = *(block *) h1Output;
    recvSet[i] = *(block * )(h1Output + sizeof(block));
  }

  commonAes.ecbEncBlocks(aesInput, receiverSize, aesOutput);
  for (u64 i = 0; i < receiverSize; ++i) {
    recvSet[i] ^= aesOutput[i];
  }

  // std::cout << "Receiver set transformed\n";
  timer.setTimePoint("Receiver set transformed");

  for (u64 wLeft = 0; wLeft < width; wLeft += widthBucket1) {
    u64 wRight = wLeft + widthBucket1 < width ? wLeft + widthBucket1 : width;
    u64 w = wRight - wLeft;


    //////////// Compute random locations (transposed) ////////////////

    commonPrng.get((u8 * ) & commonKey, sizeof(block));
    commonAes.setKey(commonKey);

    for (u64 low = 0; low < receiverSize; low += bucket1) {

      u64 up = low + bucket1 < receiverSize ? low + bucket1 : receiverSize;

      commonAes.ecbEncBlocks(recvSet + low, up - low, randomLocations);

      for (u64 i = 0; i < w; ++i) {
        for (u64 j = low; j < up; ++j) {
          memcpy(transLocations[i] + j * locationInBytes,
                 (u8 * )(randomLocations + (j - low)) + i * locationInBytes,
                 locationInBytes);
        }
      }
    }



    //////////// Compute matrix Delta /////////////////////////////////

    for (u64 i = 0; i < widthBucket1; ++i) {
      memset(matrixDelta[i], 255, heightInBytes);
    }

    for (u64 i = 0; i < w; ++i) {
      for (u64 j = 0; j < receiverSize; ++j) {
        u32 location = (*(u32 * )(transLocations[i] + j * locationInBytes)) & shift;

        matrixDelta[i][location >> 3] &= ~(1 << (location & 7));
      }
    }



    //////////////// Compute matrix A & sent matrix ///////////////////////

    u8 *sentMatrix[w];

    for (u64 i = 0; i < w; ++i) {
      PRNG prng(otMessages[i + wLeft][0]);
      prng.get(matrixA[i], heightInBytes);

      sentMatrix[i] = new u8[heightInBytes];
      prng.SetSeed(otMessages[i + wLeft][1]);
      prng.get(sentMatrix[i], heightInBytes);

      for (u64 j = 0; j < heightInBytes; ++j) {
        sentMatrix[i][j] ^= matrixA[i][j] ^ matrixDelta[i][j];
      }

      cp::sync_wait(ch.send(span<u8>(sentMatrix[i], heightInBytes)));
    }



    ///////////////// Compute hash inputs (transposed) /////////////////////

    for (u64 i = 0; i < w; ++i) {
      for (u64 j = 0; j < receiverSize; ++j) {
        u32 location = (*(u32 * )(transLocations[i] + j * locationInBytes)) & shift;

        transHashInputs[i + wLeft][j >> 3] |=
            (u8)((bool) (matrixA[i][location >> 3] & (1 << (location & 7)))) << (j & 7);
      }
    }

  }

  timer.setTimePoint("Receiver matrix sent and transposed hash input computed");




  /////////////////// Compute hash outputs ///////////////////////////

  RandomOracle H(hashLengthInBytes);
  u8 hashOutput[sizeof(block)];
  std::unordered_map<u64, std::vector<std::pair<block, u32>>> allHashes;
  u8 *hashInputs[bucket2];
  for (u64 i = 0; i < bucket2; ++i) {
    hashInputs[i] = new u8[widthInBytes];
  }

  for (u64 low = 0; low < receiverSize; low += bucket2) {
    u64 up = low + bucket2 < receiverSize ? low + bucket2 : receiverSize;

    for (u64 j = low; j < up; ++j) {
      memset(hashInputs[j - low], 0, widthInBytes);
    }

    for (u64 i = 0; i < width; ++i) {
      for (u64 j = low; j < up; ++j) {
        hashInputs[j - low][i >> 3] |= (u8)((bool) (transHashInputs[i][j >> 3] & (1 << (j & 7)))) << (i & 7);
      }
    }

    for (u64 j = low; j < up; ++j) {
      H.Reset();
      H.Update(hashInputs[j - low], widthInBytes);
      H.Final(hashOutput);

      allHashes[*(u64 *) hashOutput].push_back(std::make_pair(*(block *) hashOutput, j));
    }
  }

  // std::cout << "Receiver hash outputs computed\n";
  timer.setTimePoint("Receiver hash outputs computed");




  ///////////////// Receive hash outputs from sender and compute PSI ///////////////////

  u8 *recvBuff = new u8[bucket2 * hashLengthInBytes];

  u64 psi = 0;

  for (u64 low = 0; low < senderSize; low += bucket2) {
    u64 up = low + bucket2 < senderSize ? low + bucket2 : senderSize;

    cp::sync_wait(ch.recv(span<u8>(recvBuff, (up - low) * hashLengthInBytes)));

    for (u64 idx = 0; idx < up - low; ++idx) {
      u64 mapIdx = *(u64 * )(recvBuff + idx * hashLengthInBytes);

      auto found = allHashes.find(mapIdx);
      if (found == allHashes.end()) continue;

      for (u64 i = 0; i < found->second.size(); ++i) {
        if (memcmp(&(found->second[i].first), recvBuff + idx * hashLengthInBytes, hashLengthInBytes) == 0) {
          ++psi;
          break;
        }
      }
    }
  }

  std::cout << "Receiver intersection size: " << psi << "\n";
  if (psi == 100) {
    std::cout << "Receiver intersection computed - correct!\n";
  }
  timer.setTimePoint("Receiver intersection computed");

  std::cout << "time.receiver_base_ot_ms="
            << elapsedMs(timer["Receiver start"],
                         timer["Receiver base OT finished"]) << "\n";
  std::cout << "time.receiver_init_ms="
            << elapsedMs(timer["Receiver base OT finished"],
                         timer["Receiver initialized"]) << "\n";
  std::cout << "time.receiver_transform_ms="
            << elapsedMs(timer["Receiver initialized"],
                         timer["Receiver set transformed"]) << "\n";
  std::cout << "time.receiver_matrix_ms="
            << elapsedMs(timer["Receiver set transformed"],
                         timer["Receiver matrix sent and transposed hash input computed"]) << "\n";
  std::cout << "time.receiver_hash_ms="
            << elapsedMs(timer["Receiver matrix sent and transposed hash input computed"],
                         timer["Receiver hash outputs computed"]) << "\n";
  std::cout << "time.receiver_intersection_ms="
            << elapsedMs(timer["Receiver hash outputs computed"],
                         timer["Receiver intersection computed"]) << "\n";
  std::cout << "time.receiver_total_ms="
            << elapsedMs(timer["Receiver start"],
                         timer["Receiver intersection computed"]) << "\n";



  //////////////// Output communication /////////////////

  u64 sentData = ch.bytesSent();
  u64 recvData = ch.bytesReceived();
  u64 totalData = sentData + recvData;

  std::cout << "Receiver sent communication: " << sentData / std::pow(2.0, 20) << " MB\n";
  std::cout << "Receiver received communication: " << recvData / std::pow(2.0, 20) << " MB\n";
  std::cout << "Receiver total communication: " << totalData / std::pow(2.0, 20) << " MB\n";
}

}

#include "CM20.hpp"

namespace CM20 {

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
               const u64 &bucket2) {

  Timer timer;
  timer.setTimePoint("Sender start");

  auto heightInBytes = (height + 7) / 8;
  auto widthInBytes = (width + 7) / 8;
  auto locationInBytes = (logHeight + 7) / 8;
  auto senderSizeInBytes = (senderSize + 7) / 8;
  auto shift = (1 << logHeight) - 1;
  auto widthBucket1 = sizeof(block) / locationInBytes;

//      vector<u8> matrixR(widthBucket1 * heightInBytes);
//      prng.get(matrixR.data(), matrixR.size());



  //////////////////// Base OTs /////////////////////////////////

  IknpOtExtReceiver otExtReceiver;
//  otExtReceiver.genBaseOts(prng, ch);
  BitVector choices(width);
  std::vector<block> otMessages(width);
  prng.get(choices.data(), choices.sizeBytes());
  cp::sync_wait(otExtReceiver.receive(choices, otMessages, prng, ch));

  // std::cout << "Sender base OT finished\n";
  timer.setTimePoint("Sender base OT finished");




  ////////////// Initialization //////////////////////

  PRNG commonPrng(commonSeed);
  block commonKey;
  AES commonAes;

  u8 *transLocations[widthBucket1];
  for (u64 i = 0; i < widthBucket1; ++i) {
    transLocations[i] = new u8[senderSize * locationInBytes + sizeof(u32)];
  }

  block randomLocations[bucket1];

  u8 *matrixC[widthBucket1];
  for (u64 i = 0; i < widthBucket1; ++i) {
    matrixC[i] = new u8[heightInBytes];
  }

  u8 *transHashInputs[width];
  for (u64 i = 0; i < width; ++i) {
    transHashInputs[i] = new u8[senderSizeInBytes];
    memset(transHashInputs[i], 0, senderSizeInBytes);
  }




  /////////// Transform input /////////////////////

  commonPrng.get((u8 * ) & commonKey, sizeof(block));
  commonAes.setKey(commonKey);

  block *sendSet = new block[senderSize];
  block *aesInput = new block[senderSize];
  block *aesOutput = new block[senderSize];

  RandomOracle H1(h1LengthInBytes);
  u8 h1Output[h1LengthInBytes];

  for (u64 i = 0; i < senderSize; ++i) {
    H1.Reset();
    H1.Update((u8 * )(senderSet.data() + i), sizeof(block));
    H1.Final(h1Output);

    aesInput[i] = *(block *) h1Output;
    sendSet[i] = *(block * )(h1Output + sizeof(block));
  }

  commonAes.ecbEncBlocks(aesInput, senderSize, aesOutput);
  for (u64 i = 0; i < senderSize; ++i) {
    sendSet[i] ^= aesOutput[i];
  }

  // std::cout << "Sender set transformed\n";
  timer.setTimePoint("Sender set transformed");

  for (u64 wLeft = 0; wLeft < width; wLeft += widthBucket1) {
    u64 wRight = wLeft + widthBucket1 < width ? wLeft + widthBucket1 : width;
    u64 w = wRight - wLeft;

    //////////// Compute random locations (transposed) ////////////////

    commonPrng.get((u8 * ) & commonKey, sizeof(block));
    commonAes.setKey(commonKey);

    for (u64 low = 0; low < senderSize; low += bucket1) {

      u64 up = low + bucket1 < senderSize ? low + bucket1 : senderSize;

      commonAes.ecbEncBlocks(sendSet + low, up - low, randomLocations);

      for (u64 i = 0; i < w; ++i) {
        for (u64 j = low; j < up; ++j) {
          memcpy(transLocations[i] + j * locationInBytes,
                 (u8 * )(randomLocations + (j - low)) + i * locationInBytes,
                 locationInBytes);
        }
      }
    }



    //////////////// Extend OTs and compute matrix C ///////////////////

    u8 *recvMatrix;
    recvMatrix = new u8[heightInBytes];

    for (u64 i = 0; i < w; ++i) {
      PRNG prng(otMessages[i + wLeft]);
      prng.get(matrixC[i], heightInBytes);

      cp::sync_wait(ch.recv(span<u8>(recvMatrix, heightInBytes)));

      if (choices[i + wLeft]) {
        for (u64 j = 0; j < heightInBytes; ++j) {
          matrixC[i][j] ^= recvMatrix[j];
        }
      }
    }


    ///////////////// Compute hash inputs (transposed) /////////////////////

    for (u64 i = 0; i < w; ++i) {
      for (u64 j = 0; j < senderSize; ++j) {
        u32 location = (*(u32 * )(transLocations[i] + j * locationInBytes)) & shift;

        transHashInputs[i + wLeft][j >> 3] |=
            (u8)((bool) (matrixC[i][location >> 3] & (1 << (location & 7)))) << (j & 7);
      }
    }

  }

  // std::cout << "Sender transposed hash input computed\n";
  timer.setTimePoint("Sender transposed hash input computed");




  /////////////////// Compute hash outputs ///////////////////////////

  RandomOracle H(hashLengthInBytes);
  u8 hashOutput[sizeof(block)];

  u8 *hashInputs[bucket2];

  for (u64 i = 0; i < bucket2; ++i) {
    hashInputs[i] = new u8[widthInBytes];
  }

  for (u64 low = 0; low < senderSize; low += bucket2) {
    u64 up = low + bucket2 < senderSize ? low + bucket2 : senderSize;

    for (u64 j = low; j < up; ++j) {
      memset(hashInputs[j - low], 0, widthInBytes);
    }

    for (u64 i = 0; i < width; ++i) {
      for (u64 j = low; j < up; ++j) {
        hashInputs[j - low][i >> 3] |= (u8)((bool) (transHashInputs[i][j >> 3] & (1 << (j & 7)))) << (i & 7);
      }
    }

    u8 *sentBuff = new u8[(up - low) * hashLengthInBytes];

    for (u64 j = low; j < up; ++j) {
      H.Reset();
      H.Update(hashInputs[j - low], widthInBytes);
      H.Final(hashOutput);

      memcpy(sentBuff + (j - low) * hashLengthInBytes, hashOutput, hashLengthInBytes);
    }

    cp::sync_wait(ch.send(span<u8>(sentBuff, (up - low) * hashLengthInBytes)));
  }

  // std::cout << "Sender hash outputs computed and sent\n";
  timer.setTimePoint("Sender hash outputs computed and sent");

  std::cout << timer;

}

}

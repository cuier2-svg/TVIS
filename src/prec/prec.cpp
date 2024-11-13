#include "prec.hpp"

#include <cryptoTools/Common/CLP.h>
#include <cryptoTools/Common/Log.h>

using namespace std;

namespace prec {

const block commonSeed = oc::toBlock(123456);

int main(int argc, char **argv) {
  oc::CLP cmd;
  cmd.parse(argc, argv);

  u64 senderSize;
  u64 receiverSize;
  u64 width;
  u64 height;
  u64 logHeight;
  u64 hashLengthInBytes;
  u64 bucket, bucket1, bucket2;
  string ip;

  cmd.setDefault("ss", 20);
  senderSize = cmd.get<u64>("ss");
  if (senderSize <= 32) {
    senderSize = 1 << senderSize;
  }

  cmd.setDefault("rs", 10);
  receiverSize = cmd.get<u64>("rs");
  if (receiverSize <= 32) {
    receiverSize = 1 << receiverSize;
  }

  cmd.setDefault("w", 622);
  width = cmd.get<u64>("w");

  cmd.setDefault("h", receiverSize);
  height = cmd.get<u64>("h");
  if (height <= 32) {
    height = 1 << height;
  }
  logHeight = oc::log2ceil(height);
  cout << "logHeight: " << logHeight << endl;

  cmd.setDefault("hash", 16);
  hashLengthInBytes = cmd.get<u64>("hash");

  cmd.setDefault("ip", "localhost");
  ip = cmd.get<string>("ip");

  bucket1 = bucket2 = 1 << 8;

  PreC psi(commonSeed,
           senderSize,
           receiverSize,
           width,
           height,
           logHeight,
           hashLengthInBytes,
           32,
           bucket1,
           bucket2);

  bool noneSet = !cmd.isSet("r");
  if (noneSet) {
    std::cout
        << "=================================\n"
        << "||  Private Set Intersection   ||\n"
        << "=================================\n"
        << "\n"
        << "This program reports the performance of the private set intersection protocol.\n"
        << "\n"
        << "Experimenet flag:\n"
        << " -r 0    to run a sender.\n"
        << " -r 1    to run a receiver.\n"
        << "\n"
        << "Parameters:\n"
        << " -ss     log(#elements) on sender side.\n"
        << " -rs     log(#elements) on receiver side.\n"
        << " -w      width of the matrix.\n"
        << " -h      log(height) of the matrix.\n"
        << " -hash   hash output length in bytes.\n"
        << " -ip     ip address (and port).\n";
  } else {
    if (cmd.get<u64>("r") == 0) {
      psi.runSender();
    } else if (cmd.get<u64>("r") == 1) {
      psi.runReceiver();
    } else if (cmd.get<u64>("r") == 2) {
      thread t0 = thread([&] {
        psi.runSender();
      });
      thread t1 = thread([&] {
        psi.runReceiver();
      });
      t0.join();
      t1.join();
    }
  }

  return 0;
}

}

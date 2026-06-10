#include "vole.hpp"

#include <cryptoTools/Common/CLP.h>
#include <cryptoTools/Common/Log.h>
#include <exception>

using namespace std;

namespace vole {

  const block commonSeed = oc::toBlock(123456);

  int main(int argc, char **argv) {
    oc::CLP cmd;
    cmd.parse(argc, argv);

    u64 senderSize;
    u64 receiverSize;
    string ip;

    cmd.setDefault("ss", 20);
    senderSize = cmd.get<u64>("ss");
    if (senderSize <= 32) {
      senderSize = 1 << senderSize;
    }

    cmd.setDefault("rs", 8);
    receiverSize = cmd.get<u64>("rs");
    if (receiverSize <= 32) {
      receiverSize = 1 << receiverSize;
    }

    cmd.setDefault("ip", "localhost");
    ip = cmd.get<string>("ip");

    cmd.setDefault("cf", "full");
    auto cfMode = cmd.get<string>("cf");
    bool indexedCf = cfMode == "indexed" || cfMode == "batchpir";
    bool batchPirCf = cfMode == "batchpir";
    cmd.setDefault("bp_cf_batch", 64);
    u64 batchPirChunkSize = cmd.get<u64>("bp_cf_batch");

    VOLE psi(commonSeed,
             senderSize,
             receiverSize,
             indexedCf,
             batchPirCf,
             batchPirChunkSize
             );

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
          << " -ip     ip address (and port).\n"
          << " -cf     full, indexed, or batchpir CF transfer.\n"
          << " -bp_cf_batch   max CF buckets per BatchPIR query chunk.\n";
    } else {
      if (cmd.get<u64>("r") == 0) {
        psi.runSender();
      } else if (cmd.get<u64>("r") == 1) {
        psi.runReceiver();
      } else if (cmd.get<u64>("r") == 2) {
        std::exception_ptr senderException;
        std::exception_ptr receiverException;
        thread t0 = thread([&] {
          try {
            psi.runSender();
          } catch (...) {
            senderException = std::current_exception();
          }
        });
        thread t1 = thread([&] {
          try {
            psi.runReceiver();
          } catch (...) {
            receiverException = std::current_exception();
          }
        });
        t0.join();
        t1.join();

        auto reportException = [](const char *role, std::exception_ptr eptr) {
          if (!eptr) {
            return false;
          }
          try {
            std::rethrow_exception(eptr);
          } catch (const std::exception &e) {
            std::cerr << "error." << role << "=" << e.what() << "\n";
          } catch (...) {
            std::cerr << "error." << role << "=unknown exception\n";
          }
          return true;
        };

        bool failed = reportException("sender", senderException);
        failed = reportException("receiver", receiverException) || failed;
        if (failed) {
          return 1;
        }
      }
    }

    return 0;
  }

}

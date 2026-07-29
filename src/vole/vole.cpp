#include "vole.hpp"
#include "dataset.hpp"

#include <cryptoTools/Common/CLP.h>
#include <cryptoTools/Common/Log.h>
#include <algorithm>
#include <exception>

using namespace std;

namespace vole {

  const block commonSeed = oc::toBlock(123456);

  int main(int argc, char **argv) {
    oc::CLP cmd;
    cmd.parse(argc, argv);

    u64 senderSize;
    u64 receiverSize;

    cmd.setDefault("ss", 20);
    senderSize = cmd.get<u64>("ss");
    if (senderSize <= 32) {
      senderSize = u64{1} << senderSize;
    }

    cmd.setDefault("rs", 8);
    receiverSize = cmd.get<u64>("rs");
    if (receiverSize <= 32) {
      receiverSize = u64{1} << receiverSize;
    }

    string senderFile = cmd.isSet("sf") ? cmd.get<string>("sf") : "";
    string receiverFile = cmd.isSet("rf") ? cmd.get<string>("rf") : "";
    if (!senderFile.empty()) {
      senderSize = blockFileElementCount(senderFile);
    }
    if (!receiverFile.empty()) {
      receiverSize = blockFileElementCount(receiverFile);
    }

    string datasetName =
        cmd.isSet("dataset") ? cmd.get<string>("dataset") : "";
    bool hasExpectedIntersection = cmd.isSet("ei");
    u64 expectedIntersection =
        hasExpectedIntersection ? cmd.get<u64>("ei") : 0;
    if (hasExpectedIntersection &&
        expectedIntersection > std::min(senderSize, receiverSize)) {
      std::cerr << "error.expected_intersection=expected intersection exceeds a set size\n";
      return 1;
    }

    cmd.setDefault("ip", "127.0.0.1:7700");
    string endpoint = cmd.get<string>("ip");

    cmd.setDefault("cf", "full");
    auto cfMode = cmd.get<string>("cf");
    bool indexedCf = cfMode == "indexed" || cfMode == "batchpir";
    bool batchPirCf = cfMode == "batchpir";
    cmd.setDefault("bp_cf_batch", 64);
    u64 batchPirChunkSize = cmd.get<u64>("bp_cf_batch");

    cmd.setDefault("us", 0);
    u64 updateSize = cmd.get<u64>("us");
    if (updateSize > 20000) {
      std::cerr << "error.update_size=update set size must be <= 20000\n";
      return 1;
    }

    cmd.setDefault("uop", "insert");
    string updateOp = cmd.get<string>("uop");
    if (updateOp != "insert" && updateOp != "delete") {
      std::cerr << "error.update_op=update operation must be insert or delete\n";
      return 1;
    }

    VOLE psi(commonSeed,
             senderSize,
             receiverSize,
             indexedCf,
             batchPirCf,
             batchPirChunkSize,
             updateSize,
             updateOp,
             endpoint,
             senderFile,
             receiverFile,
             datasetName,
             hasExpectedIntersection,
             expectedIntersection
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
          << " -ip     endpoint in host:port form.\n"
          << " -cf     full, indexed, or batchpir CF transfer.\n"
          << " -bp_cf_batch   max CF buckets per BatchPIR query chunk.\n"
          << " -sf     sender block file; raw concatenated 16-byte elements.\n"
          << " -rf     receiver block file; raw concatenated 16-byte elements.\n"
          << " -dataset   dataset label printed with the results.\n"
          << " -ei     expected intersection size for correctness reporting.\n"
          << " -us     update set size, literal value, max 20000.\n"
          << " -uop    update operation: insert or delete.\n";
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

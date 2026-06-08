#include "vole/vole.hpp"

using namespace std;

void testCuckoo() {
  u64 ns = 1 << 20;
  vector<block> elements(ns);
  for (u64 i = 0; i < ns; ++i) {
    elements[i] = oc::toBlock(i);
  }

  CuckooFilter f0(ns);
  cout << f0.Contain((u64 *) &elements[1]) << endl;
  for (u64 i = 0; i < ns - 1; ++i) {
    f0.Add((u64 *) &elements[i]);
  }
  cout << f0.Contain((u64 *) &elements[1]) << endl;

  cout << f0.Info() << endl;
  vector<u8> data = f0.serialize();
  cout << data.size() << endl;
  auto param = f0.GetTwoIndependentMultiplyShiftParams();

  CuckooFilter f1(ns);
  cout << f1.Info() << endl;
  cout << f1.Contain((u64 *) &elements[1]) << endl;
  f1.deserialize(data);
  f1.SetTwoIndependentMultiplyShiftParams(param);
  cout << f1.Info() << endl;
  cout << f1.Contain((u64 *) &elements[1]) << endl;
  cout << f1.Contain((u64 *) &elements[ns - 1]) << endl;
}

void testNoisy() {
  u64 n = 5447;
  CoeffCtxGF128 ctx;

  thread t0 = thread([&]() {
    PRNG prng(oc::toBlock(456));
    NoisyVoleSender<block, block, CoeffCtxGF128> sender;
    IknpOtExtReceiver otRecv;
    auto chl = cp::asioConnect("127.0.0.1:7700", true);

    block delta = prng.get<block>();
    AlignedUnVector<block> voleC(n);
    macoro::sync_wait(sender.send(delta, voleC, prng, otRecv, chl, ctx));
  });


  thread t1([&]() {
    PRNG prng(oc::toBlock(123));
    NoisyVoleReceiver<block, block, CoeffCtxGF128> receiver;
    IknpOtExtSender otSend;
    auto chl = cp::asioConnect("127.0.0.1:7700", false);

    AlignedUnVector<block> voleA(n);
    AlignedUnVector<block> voleB(n);
    macoro::sync_wait(receiver.receive(voleB, voleA, prng, otSend, chl, ctx));
    macoro::sync_wait(chl.flush());
    std::printf("receiver send: %lu \n", chl.bytesSent());
    std::printf("receiver recv: %lu \n", chl.bytesReceived());
  });

  t0.join();
  t1.join();
}
int main(int argc, char **argv) {
  return vole::main(argc, argv);
}

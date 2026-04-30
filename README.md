# mpsi

## Build

This project depends on `boost` and `libOTe`. The author used `gcc 11.4.1` and libOTe version `17c85f7252058f008877ad8706108d026064a6e3`.

```shell
cd libOTe
python build.py --all --boost --sodium
cd .. 
mkdir build
cd build
cmake ..
make -j
```

To run it:
```shell
./upsi -r 2
```

## Contact

Create a GitHub issue or contact `lzjluzijie@gmail.com` if you have any question.

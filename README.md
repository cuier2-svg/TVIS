# mpsi

## Build

This project depends on `boost` and `libOTe`. The author used `gcc 11.4.1`.

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

Contact `lzjluzijie@gmail.com` if you have any question.

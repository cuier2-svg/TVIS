# UPSI

本项目是一个基于 C++20、libOTe 和 Boost 实现的私有集合求交（Private Set Intersection，PSI）原型。程序支持分别启动发送方和接收方，也支持在单个进程中启动双方，用于本地功能验证和性能测试。

## 功能特性

- 基于 VOLE 的 PSI 协议实现
- 支持发送方和接收方独立运行
- 支持单进程本地测试模式
- 支持 `full`、`indexed` 和 `batchpir` 三种 CF 传输模式
- 支持集合插入、删除更新及通信量、耗时统计
- BatchPIR 为可选功能，检测到 Microsoft SEAL 时才会启用

## 项目结构

```text
.
├── CMakeLists.txt          # CMake 构建配置
├── psi                     # 启动脚本，转发参数到 build/psi
├── src/
│   ├── main.cpp            # 程序入口
│   └── vole/               # VOLE PSI 协议实现
└── build/                  # 默认构建目录
```

libOTe 是外部依赖，不包含在本仓库中，也不会上传到 GitHub。

## 环境要求

- 支持 C++20 的编译器
- CMake 3.15 或更高版本
- Boost
- libOTe
- Microsoft SEAL（可选，仅 BatchPIR 模式需要）

项目开发时使用过 GCC 11.4.1，以及提交版本为 `17c85f7252058f008877ad8706108d026064a6e3` 的 libOTe。

## 构建

### macOS（Homebrew）

先安装 Boost，并确保 libOTe 已完成编译和安装：

```shell
brew install cmake boost@1.85
```

然后配置并编译项目：

```shell
mkdir -p build
cd build

cmake .. \
  -DCMAKE_PREFIX_PATH="/opt/homebrew" \
  -DBoost_ROOT=/opt/homebrew/opt/boost@1.85 \
  -DBoost_NO_BOOST_CMAKE=ON \
  -DBoost_USE_STATIC_LIBS=OFF \
  -DBoost_FIND_VERSION_EXACT=OFF \
  -DlibOTe_DIR=/usr/local/lib/cmake/libOTe

cmake --build . -j
```

其中，`libOTe_DIR` 必须指向包含 `libOTeConfig.cmake` 的目录。如果 libOTe 安装在其他位置，请相应修改该参数。

### 安装 libOTe

本仓库不包含 libOTe。请在项目目录之外单独获取并安装 libOTe，例如：

```shell
git clone https://github.com/osu-crypto/libOTe.git
cd libOTe
git checkout 17c85f7252058f008877ad8706108d026064a6e3
python3 build.py --all --boost --sodium
```

安装完成后，通过 `CMAKE_PREFIX_PATH` 或 `libOTe_DIR` 告诉 CMake `libOTeConfig.cmake` 的位置，再执行项目构建。libOTe 的具体构建和安装选项请以其上游文档为准。

如果不需要 BatchPIR，或者本机未安装 SEAL，可以显式关闭该功能：

```shell
cmake .. -DENABLE_BATCHPIR=OFF -DlibOTe_DIR=/path/to/lib/cmake/libOTe
```

构建成功后生成可执行文件 `build/psi`。仓库根目录下的 `psi` 脚本也可用于启动它。

## 运行

### 本地测试

在同一进程中启动发送方和接收方：

```shell
./psi -r 2 -ss 8 -rs 4 -cf full
```

也可以直接运行：

```shell
./build/psi -r 2 -ss 8 -rs 4 -cf full
```

### 分别启动双方

在两个终端中分别执行：

```shell
# 终端 1：发送方
./psi -r 0 -ss 20 -rs 8 -cf full

# 终端 2：接收方
./psi -r 1 -ss 20 -rs 8 -cf full
```

当前实现会使用 `127.0.0.1:7700` 建立连接。虽然程序接受 `-ip` 参数，但发送方和接收方的运行代码目前仍使用上述固定地址；跨主机运行前需要调整对应实现。

## 参数说明

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-r` | 无 | 运行角色：`0` 为发送方，`1` 为接收方，`2` 为本地双方模式 |
| `-ss` | `20` | 发送方集合规模；不大于 32 时按 `2^ss` 计算 |
| `-rs` | `8` | 接收方集合规模；不大于 32 时按 `2^rs` 计算 |
| `-ip` | `localhost` | IP 地址和端口参数；当前协议运行代码仍固定使用 `127.0.0.1:7700` |
| `-cf` | `full` | CF 传输模式：`full`、`indexed` 或 `batchpir` |
| `-bp_cf_batch` | `64` | 每个 BatchPIR 查询分块包含的最大 CF bucket 数量 |
| `-us` | `0` | 更新集合大小，填写实际数量，最大值为 `20000` |
| `-uop` | `insert` | 更新操作：`insert` 或 `delete` |

例如，测试大小分别为 `2^12` 和 `2^8` 的集合：

```shell
./psi -r 2 -ss 12 -rs 8 -cf indexed
```

执行更新测试：

```shell
./psi -r 2 -ss 12 -rs 8 -cf full -us 100 -uop insert
```

## 输出说明

程序运行后会输出协议参数、耗时、通信量和交集大小等指标，常见字段包括：

| 字段 | 说明 |
| --- | --- |
| `param.okvs_size` | OKVS 数据规模 |
| `time.sender_setup_ms` | 发送方初始化耗时（毫秒） |
| `time.sender_total_ms` | 发送方总耗时（毫秒） |
| `time.receiver_setup_ms` | 接收方初始化耗时（毫秒） |
| `time.receiver_vole_ms` | 接收方 VOLE 阶段耗时（毫秒） |
| `time.receiver_total_ms` | 接收方总耗时（毫秒） |
| `comm.vole_mb` | VOLE 阶段通信量（MB） |
| `comm.online_mb` | 在线阶段通信量（MB） |
| `result.intersection` | 计算得到的交集元素数量 |

## BatchPIR 说明

BatchPIR 支持由 CMake 选项 `ENABLE_BATCHPIR` 控制。启用该选项后，只有在 CMake 成功找到 SEAL 时，项目才会链接 BatchPIR 并启用 `batchpir` 模式；否则项目仍可构建，但只能使用其他 CF 传输模式。

## 常见问题

### CMake 找不到 libOTe

确认 libOTe 已完成构建或安装，并查找 `libOTeConfig.cmake` 的实际位置：

```shell
find /usr/local /opt/homebrew -name libOTeConfig.cmake 2>/dev/null
```

然后将其所在目录传给 CMake：

```shell
cmake .. -DlibOTe_DIR=/path/to/lib/cmake/libOTe
```

### CMake 找不到 Boost

确认 Boost 的安装位置，并设置 `Boost_ROOT`。Apple Silicon Mac 使用 Homebrew 时通常位于 `/opt/homebrew/opt/boost@1.85`。

### `batchpir` 模式不可用

确认 SEAL 和 BatchPIR 已正确安装且能被 CMake 找到；如果暂时不需要该功能，请使用 `-DENABLE_BATCHPIR=OFF` 构建。

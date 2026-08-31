# UPSI

本项目是一个基于 C++20、libOTe 和 Boost 实现的私有集合求交（Private Set Intersection，PSI）原型。程序支持分别启动发送方和接收方，也支持在单个进程中启动双方，用于本地功能验证和性能测试。

## 功能特性

- 基于 VOLE 的 PSI 协议实现
- 支持发送方和接收方独立运行
- 支持单进程本地测试模式
- 支持 `full`、 `batchpir` 两种 CF 传输模式
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
├── BatchPIR                # BatchPIR协议实现
├── cuckoofilter            # cuckoofilter协议实现
```

需安装libOTe 

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
./psi -r 2 -ss 20 -rs 8 -cf batchpir
```

也可以直接运行：

```shell
./build/psi -r 2 -ss 20 -rs 8 -cf batchpir
```

### 分别启动双方

在两个终端中分别执行：

```shell
# 终端 1：发送方
./psi -r 0 -ss 20 -rs 8 -cf batchpir

# 终端 2：接收方
./psi -r 1 -ss 20 -rs 8 -cf batchpir
```
运行结果如下图所示：
![alt text](https://file%2B.vscode-resource.vscode-cdn.net/Users/cuiyang/upsi-main-2/picture/batchPIR.png?version%3D1788164317185)
当前实现会使用 `127.0.0.1:7700` 建立连接。虽然程序接受 `-ip` 参数，但发送方和接收方的运行代码目前仍使用上述固定地址；跨主机运行前需要调整对应实现。

## 参数说明

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-r` | 无 | 运行角色：`0` 为发送方，`1` 为接收方，`2` 为本地双方模式 |
| `-ss` | `20` | 发送方集合规模；不大于 32 时按 `2^ss` 计算 |
| `-rs` | `8` | 接收方集合规模；不大于 32 时按 `2^rs` 计算 |
| `-ip` | `127.0.0.1:7700` | 通信端点，格式为 `host:port`；发送方监听，接收方连接 |
| `-cf` | `full` | CF 传输模式：`full` 或 `batchpir` |
| `-bp_cf_batch` | `64` | 每个 BatchPIR 查询分块包含的最大 CF bucket 数量 |
| `-sf` | 空 | 发送方集合文件；内容为连续的 16 字节元素，文件大小决定发送方集合规模 |
| `-rf` | 空 | 接收方集合文件；内容为连续的 16 字节元素，文件大小决定接收方集合规模 |
| `-dataset` | 空 | 输出中显示的数据集名称 |
| `-ei` | 空 | 预期交集大小；设置后输出 `result.correct` |
| `-us` | `0` | 更新集合大小，填写实际数量，最大值为 `20000` |
| `-uop` | `insert` | 更新操作：`insert` 或 `delete` |

例如，测试大小分别为 `2^12` 和 `2^8` 的集合：
执行更新测试：

```shell
./psi -r 2 -ss 12 -rs 8 -cf full -us 100 -uop insert
```
运行结果如下图所示：
![alt text](https://file%2B.vscode-resource.vscode-cdn.net/Users/cuiyang/upsi-main-2/picture/Uop.png?version%3D1788164294426)
<!-- ## VERI-Wild 真实车辆数据

仓库提供 `tools/preprocess_veriwild.py`，用于从 VERI-Wild 标注文件中构造真实车辆 PSI 输入。默认实验参数为：

- 服务端目标车辆：`32768`
- 客户端通行车辆：`256`
- 预期交集：`64`
- 元素编码：`Trunc128(SHA256("TVIS-v1|vehicle|" || vehicle_id))`

脚本默认按下面的空白分隔标注格式读取数据：

```text
image_path vehicle_id camera_id
```

如果实际标注列顺序不同，可以通过 `--vehicle-id-column` 和 `--camera-id-column` 指定从 0 开始的列号。生成数据：

```shell
python3 tools/preprocess_veriwild.py \
  --annotations /path/to/veriwild_annotations.txt
```

如果单个训练标注中的唯一车辆数不足 `32768`，可以重复传入该参数，合并官方训练集与测试身份列表：

```shell
python3 tools/preprocess_veriwild.py \
  --annotations /path/to/train_list_start0.txt \
  --annotations /path/to/test_10000_id.txt \
  --vehicle-id-column 0 \
  --vehicle-id-path-prefix
```

VERI-Wild 的训练集第二列使用局部重编号，测试集第二列则使用原始编号，因此合并时应从第一列图片路径的目录前缀提取全局车辆 ID。上述命令会得到 `30671 + 10000 = 40671` 个不同车辆身份。

如果标注不含独立的摄像头列，使用 `--camera-id-column -1`；此时脚本从全部车辆身份中构造客户端集合。

默认输出：

```text
data/processed/veriwild_server.bin
data/processed/veriwild_client.bin
data/processed/veriwild_metadata.json
```

随后运行真实数据实验：

```shell
./psi -r 2 \
  -sf data/processed/veriwild_server.bin \
  -rf data/processed/veriwild_client.bin \
  -dataset VERI-Wild \
  -ei 64 \
  -cf batchpir
```

`-sf` 和 `-rf` 会覆盖 `-ss` 与 `-rs`，集合规模直接由文件大小计算。数据集原始标注解析、车辆 ID 哈希和文件生成均在协议运行前完成，不计入 PSI 耗时。未指定集合文件时，程序仍使用原有的固定种子数据模式。 -->
<!-- 
## City-scale Traffic Camera 真实车辆数据

`tools/preprocess_citycam.py` 用于处理论文 [City-scale Vehicle Trajectory Data from Traffic Camera Videos](https://doi.org/10.1038/s41597-023-02589-y) 公开的轨迹 CSV。数据可从 [Figshare](https://doi.org/10.6084/m9.figshare.c.6676199.v1) 下载。

推荐使用深圳 `2021-04-16` 的单日文件 [`traj_shenzhen_20210416.csv`](https://springernature.figshare.com/articles/dataset/traj_shenzhen_20210416_csvx/23282771)。本项目下载到的文件实测包含 `1,650,253` 条轨迹和 `1,122,385` 个不同车辆身份，足以构造 `2^20` 个服务端元素。公开 CSV 的主要字段为：

```text
VehicleID,TripID,Points,DepartureTime,Duration,Length
```

同一车辆一天内可能有多条轨迹，因此预处理只读取 `VehicleID`，忽略 `TripID` 和所有轨迹字段。元素编码为：

```text
Trunc128(SHA256("TVIS-v1|city-camera-trajectory|" ||
                "Shenzhen|2021-04-16|" || VehicleID))
```

生成 `2^20` 个服务端元素、256 个客户端元素和64个交集元素：

```shell
python3 tools/preprocess_citycam.py \
  --input /Users/cuiyang/Downloads/traj_shenzhen_20210416.csv
```

脚本也支持 `.csv.gz`，以及仅包含目标轨迹 CSV 的 `.zip`。默认输出：

```text
data/processed/citycam_server.bin
data/processed/citycam_client.bin
data/processed/citycam_metadata.json
```

选择过程完全确定：对编码后的真实车辆元素排序，前 `2^20` 个构成服务端；客户端包含服务端中的64个元素，以及服务端之外的192个真实车辆元素。脚本不生成模拟车辆，也不使用随机抽样。

本地功能验证：

```shell
./psi -r 2 \
  -sf data/processed/citycam_server.bin \
  -rf data/processed/citycam_client.bin \
  -dataset CityCam-SZ-20210416 \
  -ei 64 \
  -cf batchpir
```

该公开数据没有提供物理摄像头编号与车辆身份的逐条对应表。`VehicleID` 是作者根据多个交通摄像头记录聚类得到的匿名车辆编号，不是车牌。项目将其解释为城市边缘节点汇总并去重后的车辆集合，不把道路节点或推断的轨迹点伪装成原始 `camera_id`。

## Waymo Motion 真实车辆轨迹数据

`tools/preprocess_waymo.py` 直接流式解析 Waymo Open Motion Dataset v1.3.1
的 Scenario TFRecord，不依赖 TensorFlow 或 Waymo Python 包。脚本只读取：

```text
Scenario.scenario_id
Track.id
Track.object_type == TYPE_VEHICLE
```

Waymo 的 `Track.id` 只在单个 Scenario 中唯一，因此车辆轨迹身份定义为
`scenario_id|track_id`，再编码为16字节 PSI 元素。该身份表示一个真实场景
窗口中的车辆轨迹，不解释为跨场景永久车辆身份。

先统计已下载分片中的车辆数：

```shell
python3 tools/preprocess_waymo.py \
  --input /Users/cuiyang/Downloads/waymo_motion/scenario/training \
  --count-only
```

下载到足够分片后，去掉 `--count-only` 即可生成：

```text
data/processed/waymo_server.bin
data/processed/waymo_client.bin
data/processed/waymo_metadata.json
```

默认构造 `2^20` 个 Server 元素、256个 Client 元素和64个交集元素：

```shell
./psi -r 2 \
  -sf data/processed/waymo_server.bin \
  -rf data/processed/waymo_client.bin \
  -dataset Waymo-Motion-v1.3.1 \
  -ei 64 \
  -cf batchpir
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
| `time.end_to_end_ms` | 接收方从协议开始到得到交集结果的端到端耗时（毫秒） |
| `comm.full_cf_mb` | `-cf full` 模式发送完整序列化 Cuckoo Filter 的实际通信量（含消息封装，仅该模式输出） |
| `comm.vole_mb` | VOLE 阶段通信量（MB） |
| `comm.online_mb` | 在线阶段通信量（MB） |
| `comm.total_mb` | `-cf full` 模式的总通信量，即 `comm.full_cf_mb + comm.online_mb` |
| `result.intersection` | 计算得到的交集元素数量 |
| `dataset.expected_intersection` | 通过 `-ei` 设置的预期交集数量 |
| `result.correct` | 协议交集数量是否与预期值一致 | -->

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

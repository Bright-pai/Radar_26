# Radar_26 — 使用说明与开发者文档

该仓库包含 Radar_26 C++ 主程序（CMake + OpenCV + TensorRT）。

---

## 快速开始

```bash
# 构建
cmake -S . -B build -DTENSORRT_ROOT=/usr/local/TensorRT-10.8.0.43
cmake --build build -j$(nproc)

# 运行
./build/Radar_26 --config config/app.yaml
```

生成目标：`build/Radar_26`（主程序）、`build/Radar_26_calibration`（标定工具）、`build/Radar_26_camera_tuner`（相机调参）、`build/Radar_26_camera_test`（相机快照）

一键启动脚本：`~/Desktop/start_radar.sh`（双击桌面 `start_radar.desktop`）

---

## 配置说明 (`config/app.yaml`)

| 段 | 键 | 说明 |
|---|---|---|
| 顶层 | `team` | `"R"` 红方 / `"B"` 蓝方 |
| | `debug` | `1` 开启 debug 模式（TCP 直覆 + 详细日志） |
| | `show_ui` | `1` 显示 img / map / Serial Monitor 三个窗口 |
| `camera` | `mode` | `"daheng"` / `"video_file"` / `"test"` |
| | `exposure_time` | 曝光时间（us） |
| | `gain` | 模拟增益 |
| `serial` | `enable` | `1` 启用串口收发 |
| | `send_period_ms` | 0x0305 发送间隔（200 = 5Hz） |
| `tcp` | `enable` | `1` 启用 TCP 触发信号 + 数据接收 |
| `model` | `car_engine` / `armor_engine` | TensorRT 引擎路径 |
| `calibration` | `red_path` / `blue_path` | 标定 yaml 路径 |
| `detection` | `carConf` / `carMaxDet` 等 | 检测阈值 |

---

## 架构总览

### 数据流

```
相机采集 → grabThread
  → 主循环 Run(): carDetector + armorDetector 推理
  → targetFilter 暂存视觉定位
  → 透视投影 → 地图坐标
  → SerialSendLoop: 0x0305(坐标), 0x0301(66B), 0x0121(雷达命令)
  → TCP 8001: 0x0A01(对方坐标) + 0x0A02~0A05(66B段)
  → TCP 8002/8003: 0x0A06(破解密钥)
  → TcpSendLoop: TCP 0x00/0x01(触发信号 → 192.168.12.2:5000)
```

### 线程模型

| 线程 | 启动位置 | 函数 | 用途 |
|---|---|---|---|
| `grabThread_` | `Run()` [1528行](src/radar_app.cpp#L1528) | lambda | 异步帧采集 |
| `sendThread_` | `StartSerialThreads()` [805行](src/radar_app.cpp#L805) | `SerialSendLoop` | 周期串口发送 |
| `recvThread_` | `StartSerialThreads()` [805行](src/radar_app.cpp#L805) | `SerialReceiveLoop` | 串口字节读取 |
| `tcpThread_` | `StartSerialThreads()` [805行](src/radar_app.cpp#L805) | `TcpSendLoop` | TCP 0x00/0x01 触发信号 |
| `parserThreads_` | `StartSerialThreads()` [805行](src/radar_app.cpp#L805) | `ParserWorker` | 解帧 → 状态更新 |
| `tcpRxThread8001_` | `StartTcpReceivers()` [546行](src/radar_app.cpp#L546) | `TcpReaderLoop(8001)` | TCP 8001 接收 |
| `tcpRxThread8002_` | `StartTcpReceivers()` [546行](src/radar_app.cpp#L546) | `TcpReaderLoop(8002)` | TCP 8002 接收 |
| `tcpRxThread8003_` | `StartTcpReceivers()` [546行](src/radar_app.cpp#L546) | `TcpReaderLoop(8003)` | TCP 8003 接收 |

---

## 核心类

| 类 | 文件 | 职责 |
|---|---|---|
| `RadarApp` | [include/radar_app.hpp](include/radar_app.hpp)  / [src/radar_app.cpp](src/radar_app.cpp) | 应用总控，持有所有模块和线程（~1900行） |
| `SerialByteRingBuffer` | [src/radar_app.cpp:69](src/radar_app.cpp#L69) | 环形缓冲区，用于字节流解帧 |
| `TcpClient` | [include/tcp_client.hpp](include/tcp_client.hpp)  / [src/tcp_client.cpp](src/tcp_client.cpp) | Linux socket TCP 客户端（非阻塞连接+2s超时） |
| `DahengCamera` | [include/daheng_camera.hpp](include/daheng_camera.hpp) / [src/daheng_camera.cpp](src/daheng_camera.cpp) | 大恒 SDK 封装（PIMPL），支持实时调曝光/增益 |
| `ConfigLoader` | [src/config_loader.cpp](src/config_loader.cpp) | YAML 配置 → `AppConfig` / `CalibrationData` |
| `SerialStatus` | [include/radar_types.hpp:118](include/radar_types.hpp#L118) | 原子计数 + 文本，供 SerialMonitor 显示 |

---

## RadarApp 函数一览

| 函数 | 行号 | 可见性 | 说明 |
|---|---|---|---|
| `RadarApp()` | [273](src/radar_app.cpp#L273) | public | 构造：move config + 存 configPath |
| `~RadarApp()` | [277](src/radar_app.cpp#L277) | public | 析构：停线程、关串口/TCP |
| `Initialize(error)` | [285](src/radar_app.cpp#L285) | public | 加载标定→建滤波器→初始密钥→载模型→开串口/TCP |
| `Run()` | [1487](src/radar_app.cpp#L1487) | public | 主循环：采帧→检测→投影→地图→UI→滑轨→退出 |
| `OpenSerial(error)` | [331](src/radar_app.cpp#L331) | private | 打开串口设备 |
| `OpenTcp(error)` | [340](src/radar_app.cpp#L340) | private | TCP 客户端连接 + 0x00 握手 |
| `TcpSendLoop()` | [370](src/radar_app.cpp#L370) | private | TCP 触发信号线程：T1/T2/T3 判断 + 断线重连 |
| `TcpReaderLoop(port)` | [568](src/radar_app.cpp#L568) | private | TCP 接收线程：连 192.168.12.99:port → 解 0xA5 帧 → 回调 |
| `OnTcp8001Frame(frame)` | [687](src/radar_app.cpp#L687) | private | 处理 8001 端口帧：0x0A01 坐标 / 0x0A02~0A05 66B 段 |
| `OnTcpKeyFrame(level, frame)` | [773](src/radar_app.cpp#L773) | private | 处理 8002/8003 端口帧：0x0A06 密钥 |
| `UpdateTcp8001Status(seq)` | [792](src/radar_app.cpp#L792) | private | 合并位置行+66B 行写入 tcpRx8001_ 状态 |
| `StartTcpReceivers()` | [546](src/radar_app.cpp#L546) | private | 启动 8001/8002/8003 接收线程 |
| `StopTcpReceivers()` | [561](src/radar_app.cpp#L561) | private | 等待 8001/8002/8003 线程结束 |
| `StartSerialThreads()` | [805](src/radar_app.cpp#L805) | private | running_=true → 启动 send/recv/tcp/parser 线程 |
| `StopSerialThreads()` | [821](src/radar_app.cpp#L821) | private | running_=false → 等待所有线程结束 |
| `SerialSendLoop()` | [844](src/radar_app.cpp#L844) | private | 串口发送主循环（0x0305 + 0x0301 + 0x0121 + 双倍触发） |
| `SerialReceiveLoop()` | [1254](src/radar_app.cpp#L1254) | private | 串口接收：读字节→环缓冲→扫 0xA5 头→白名单→入队 |
| `ParserWorker()` | [1341](src/radar_app.cpp#L1341) | private | 解析工作线程：出队→CRC→dispatch(0x020E/0301/0001/0003/0101) |
| `MapToSerCoords(name, mapX, mapY)` | [1462](src/radar_app.cpp#L1462) | private | 地图坐标 → 串口协议坐标 |
| `ProjectPoint(transform, point, mapX, mapY)` | [1468](src/radar_app.cpp#L1468) | private | 透视变换：图像点 → 地图坐标 |
| `UpdateSerialStatus(status, seq, text)` | [1478](src/radar_app.cpp#L1478) | private | 更新 SerialStatus 的原子计数+文本 |
| `SaveCameraParams()` | [1865](src/radar_app.cpp#L1865) | private | 行级替换 app.yaml 的 exposure_time/gain |

---

## 协议一览

### 串口接收（SerialReceiveLoop 白名单过滤）

| cmdId | 子内容 | Payload | 处理位置 | 解析结果 |
|---|---|---|---|---|
| `0x020E` | — | 1 字节 | [ParserWorker:1342](src/radar_app.cpp#L1342) | `RadarInfoBits` → 双倍机会/加密等级/密钥可修改标志 |
| `0x0301` | `0x0200` | 46 字节 | [ParserWorker:1369](src/radar_app.cpp#L1369) | 英雄/工程/步3/步4 坐标 → `sendLastSerCoords_` |
| `0x0001` | — | 11 字节 | [ParserWorker:1380](src/radar_app.cpp#L1380) | `game_type` `game_progress` `stage_remain_time` |
| `0x0003` | — | 16 字节 | [ParserWorker:1398](src/radar_app.cpp#L1398) | 前哨站血量 → `outpostHealthDecreased_` |
| `0x0101` | — | 4 字节 | [ParserWorker:1422](src/radar_app.cpp#L1422) | 小/大能量机关激活状态 |

### 串口发送

| cmdId | 子内容 | 发送位置 | 触发条件 | 内容 |
|---|---|---|---|---|
| `0x0305` | — | [SerialSendLoop:970](src/radar_app.cpp#L970) | 5Hz 定时 + debug TCP 实时 | 12 机器人 × 2 坐标（24 × uint16 LE） |
| `0x0301` | `0x0121` INIT | [SerialSendLoop:1064](src/radar_app.cpp#L1064) | 仅一次 | 初始密钥注册 DBD31D |
| `0x0301` | `0x0121` KEYMOD | [SerialSendLoop:1111](src/radar_app.cpp#L1111) | keyModifiable 0→1 | 轮换密钥 DBD31D→C74B91→2E4512 |
| `0x0301` | `0x0121` CRACKED | [SerialSendLoop:1125](src/radar_app.cpp#L1125) | encryptLevel≥1 + 13s冷却 | 破解密钥(来自TCP 8002/8003) |
| `0x0301` | `0x0121` DOUBLE | [SerialSendLoop:1213](src/radar_app.cpp#L1213) | C1/C2/C3 + 有机会 | 双倍易伤触发(radarCmd计数, key全零) |
| `0x0301` | `0x0206` | [SerialSendLoop:998](src/radar_app.cpp#L998) | 1Hz 定时 | 66B TCP 真实数据 / 默认填0 → 9个接收者 |
| TCP `0x00` | — | [OpenTcp:353](src/radar_app.cpp#L353) | 连接成功后 | 握手信号 |
| TCP `0x01` | — | [TcpSendLoop:451](src/radar_app.cpp#L451) | T1/T2/T3 任一触发 | 触发信号 |

### TCP 接收（客户端连 192.168.12.99）

| cmdId | 端口 | Payload | 处理位置 | 内容 |
|---|---|---|---|---|
| `0x0A01` | 8001 | 24 字节 | [OnTcp8001Frame:698](src/radar_app.cpp#L698) | 对方6台(12×int16 LE) → `tcpEnemyCoords_[12]` |
| `0x0A02` | 8001 | 12 字节 | [OnTcp8001Frame:732](src/radar_app.cpp#L732) | 66B 段1/4 |
| `0x0A03` | 8001 | 10 字节 | [OnTcp8001Frame:738](src/radar_app.cpp#L738) | 66B 段2/4 |
| `0x0A04` | 8001 | 8 字节 | [OnTcp8001Frame:744](src/radar_app.cpp#L744) | 66B 段3/4 |
| `0x0A05` | 8001 | 36 字节 | [OnTcp8001Frame:750](src/radar_app.cpp#L750) | 66B 段4/4 → 四段齐 `tcp0206AllValid_` |
| `0x0A06` | 8002 | 6 字节 | [OnTcpKeyFrame:773](src/radar_app.cpp#L773) | 破解密钥 L1 → `tcpCrackedKey_[0]` |
| `0x0A06` | 8003 | 6 字节 | [OnTcpKeyFrame:773](src/radar_app.cpp#L773) | 破解密钥 L2 → `tcpCrackedKey_[1]` |

---

## SerialSendLoop 每轮执行顺序

```
1. 0x0305 位置包 [912-976]
   ├─ 对方(ri<6) + TCP有效
   │   ├─ debug:     直接 TCP 覆盖
   │   └─ normal:    TCP 与视觉校验 ±300 → 通过用TCP，不过用视觉
   ├─ 视觉检测到 → MapToSerCoords
   └─ 都没有 → 上次已知坐标

2. 0x0301→0x0206 66B 转发 [978-1025]
   ├─ 1Hz 定时
   ├─ TCP 四段齐 → 真实 66 字节 → 9 接收者（红:6,1,3,4,7,101,103,104,107 / 蓝:106,101,103,104,107,1,3,4,7）
   └─ TCP 不齐   → 默认填充

3. 0x0121 INIT [1062-1066]  仅一次

4. 0x020E 事件 [1068-1131]
   ├─ hasOpportunity 0→1 → doublePendingCount_++（上限2）
   ├─ keyModifiable 0→1 → 轮换密钥
   └─ encryptLevel≥1   → 13s冷却 CRACKED

5. C3 + 双倍触发 [1133-1220]
   ├─ debug: 从 TCP 取 Y 坐标
   ├─ normal: 从视觉 allData 取坐标
   └─ C1||C2||C3 + 有机会 → DOUBLE
```

---

## 双倍易伤触发条件

| 条件 | 行号 | 逻辑 |
|---|---|---|
| 机会累积 | [1080](src/radar_app.cpp#L1080) | hasOpportunity 0→1 上升沿，`doublePendingCount_++`（上限 2） |
| C1 | [1191](src/radar_app.cpp#L1191) | `stage==4` + `remain>0` + (`remain<330 & smallE` 或 `remain<240`) |
| C2 | [1196](src/radar_app.cpp#L1196) | `stage==4` + `remain>0` + (`remain<180 & bigE` 或 `remain<60`) |
| C3 debug | [1137](src/radar_app.cpp#L1137) | 从 TCP `tcpEnemyCoords_` 取 Y：红 `Y>1400`（半场）/`Y>1900`（深线），蓝反之 |
| C3 normal | [1145](src/radar_app.cpp#L1145) | 从视觉 `allData` → `MapToSerCoords`：serX < 1400（半场）/< 900 或 > 1900（深线） |

---

## TCP 0x01 触发条件（TcpSendLoop）

| 触发 | 行号 | 条件 |
|---|---|---|
| T1 | [413](src/radar_app.cpp#L413) | `gameProgress==4` + `remainTime>0 && <390s` + `outpostHealthDecreased_` |
| T2 | [422](src/radar_app.cpp#L422) | `gameProgress==4` + `remainTime>0 && ≤360s` + 工程师被视觉检测 + 到达指定位置 |
| T3 | [438](src/radar_app.cpp#L438) | `gameProgress==4` + `remainTime>0 && <180s` |

---

## Debug 模式日志

全部输出到 `logs/` 目录：

| 日志文件 | 触发条件 | 内容 |
|---|---|---|
| `raw_serial.log` | `debug:1` | 串口接收原始字节 hex |
| `raw_tcp_8001~8003.log` | `debug:1` | TCP 各端口原始字节 hex |
| `vision_coords.log` | `debug:1` + `game_progress==4` | 视觉检测 12 机器人坐标 + 剩余时间 |
| `tcp0A01_coords.log` | `debug:1` + `game_progress==4` + TCP有效 | TCP 0x0A01 对方坐标 + 剩余时间 |
| `double_vuln.log` | `debug:1` | 双倍机会接收 + C3 评估(每10帧) |
| `tcp_trigger.log` | `debug:1` + 触发满足 | T1/T2/T3 触发参数 + 是否发出 |
| `radar_0121.log` | `debug:1` | 所有 0x0121 发送（INIT/KEYMOD/CRACKED/DOUBLE）+ 剩余时间 |

---

## 0x0301→0x0206 转发接收者

红方 senderId=9：`6, 1, 3, 4, 7, 101, 103, 104, 107`
蓝方 senderId=109：`106, 101, 103, 104, 107, 1, 3, 4, 7`

---

## 标定

1. 点选工具生成 `.npy` → `scripts/convert_calibration.py` 转 `calibration_*.yaml`
2. 或使用 `build/Radar_26_calibration` GUI 工具
3. `config/app.yaml` 中设置 `calibration.red_path` / `calibration.blue_path`

标定文件保存路径：
- NPY: `/home/liu/Desktop/Radar_26/arrays_test_{red,blue}.npy`
- YAML: `/home/liu/Desktop/Radar_26/config/calibration_{red,blue}.yaml`

---

## 依赖

- CMake ≥ 3.18, GCC C++17
- OpenCV（core/imgproc/highgui/imgcodecs/videoio）
- CUDA Toolkit + TensorRT（libnvinfer, libnvinfer_plugin）
- pthread, Daheng GxIAPI SDK（可选，`-DRADAR26_WITH_DAHENG=OFF` 跳过）

---

## 故障排查

| 问题 | 检查 |
|---|---|
| 串口打开失败 | `ls -l /dev/serial/by-id/`，用户是否在 `dialout` 组 |
| 相机打不开 | 大恒相机不能多进程同时占用，`ps aux \| grep Radar` 杀残留 |
| 性能问题 | 关闭 `show_ui`，降低分辨率，调整 `max_det` |
| 地图不显示坐标 | 确认 `show_ui:1`，标定文件路径正确 |
| TCP 端口连不上 | `nc -zv 192.168.12.99 8001` 测试连通性 |
| CUDA 错误 999 | `sudo nvidia-smi` 确认驱动正常，或重启 |
# Radar_26 — 使用说明

该仓库包含 Radar_26 C++ 主程序（CMake + OpenCV + TensorRT）。本 README 已同步到当前代码实现：串口接收处理为 `0x020E` 与 `0x0301`，忽略 `0x020B`。

主要功能概览

- 相机采集：`video_file` / `usb` / `daheng`（可选）
- 车辆检测（TensorRT）与装甲板检测（TensorRT）
- 透视映射到比赛地图，生成序列化机器人坐标
- 串口发送：`0x0305`（坐标）、`0x0121`（雷达命令）
- 串口接收（本实现）：`0x020E`（雷达状态）与 `0x0301`（交互坐标封装）；`0x020B` 被忽略
- 可视化 UI：`img`、`map`、协议监控等

快速开始

1. 准备依赖（Ubuntu 示例）：

- CMake >= 3.18
- g++ (支持 C++17)
- OpenCV（core/imgproc/highgui/imgcodecs/videoio）
- CUDA & TensorRT（若使用 engine 推理）
- pthread

2. 配置 `config/app.yaml`（示例见仓库内 `config/app.yaml`）

3. 构建：

```bash
cd Radar_26
cmake -S . -B build -DTENSORRT_ROOT=/usr/local/TensorRT-10.8.0.43
cmake --build build -j$(nproc)
```

生成可执行：`build/Radar_26` 与 `build/Radar_26_calibration`

运行：

```bash
./build/Radar_26 --config config/app.yaml
```

配置说明要点

- `camera.mode`：`video_file` / `usb` / `daheng`
- `serial.enable`：是否启用串口线程
- `serial.port`：优先使用 `/dev/serial/by-id/...`
- `serial.baudrate`：一般为 `115200`
- 模型路径（`model.car_engine` / `model.armor_engine`）应指向有效 engine 文件

串口协议（关键点）

- 发送：
  - `0x0305`：发送 12 个机器人的坐标（24 个 uint16，顺序与代码内 `BuildPositionPacket` 一致）
  - `0x0121`：雷达命令（由 `0x020E` 的状态位决定是否发送）

- 接收（当前实现）：
  - `0x020E`：雷达状态位，payload 1 字节，解析成 `RadarInfoBits`（双倍机会、敌方触发、加密等级、key 可修改标志）
  - `0x0301`：交互坐标封装，含子内容 `0x0201`，代码会尝试解析并在协议监控中展示 `sender`、`receiver` 及部分坐标信息


实现注意：接收线程已使用有界环形缓冲以减少内存搬移开销，帧解析只在完整帧到达时计算 CRC 并进一步解码；非目标 cmd 直接丢弃以降低用户态解析成本。

标定（简要）

仓库提供点选 + 转换流程：

1. 使用 Python 点选工具生成 `.npy`（`arrays_test_red.npy` / `arrays_test_blue.npy`）
2. 用 `scripts/convert_calibration.py` 将 `.npy` 转为 `config/calibration_*.yaml`
3. 在 `config/app.yaml` 中设置 `calibration.red_path` / `calibration.blue_path`

若需要离线 GUI 标定，可使用 `build/Radar_26_calibration`（构建步骤同上）

UI 与调试

- 打开 `show_ui: 1` 可以看到 `img`（检测）、`map`（投影）与协议监控
- 协议监控显示发送/接收最近包的 hex 与解析信息，方便调试串口状态

日志与问题排查

- 串口打开失败：检查设备路径、权限（是否在 `dialout` 组）、是否被其它进程占用
- 未收到裁判包：确认 `serial.enable` 与 `serial.port`、波特率一致，且没有多进程争用串口
- 性能问题：关闭 UI（`show_ui: 0`），降低分辨率，或调整检测阈值与 `max_det`

变更记录（本次主要变更）

- README 同步到代码：接收端现在处理 `0x020E` 与 `0x0301`
- 在 UI 协议监控中新增 `0x0301` 的解析显示

如果你希望我把 README 扩展为更详细的开发者文档（例如完整字段表、序列样例 hex、或示例抓包/解析脚本），告诉我具体想要的部分，我会继续补充。


SOF   0xA5
data_length  2字节表示101
seq   自增即可 1字节
CRC8  帧头 CRC8 校验
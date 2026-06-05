# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
cmake -S . -B build -DTENSORRT_ROOT=/usr/local/TensorRT-10.8.0.43
cmake --build build -j$(nproc)
```

Three executables are produced:
- `build/Radar_26` — main application
- `build/Radar_26_calibration` — offline calibration GUI
- `build/Radar_26_camera_test` — camera snapshot test tool

Run: `./build/Radar_26 --config config/app.yaml`

## Architecture

All code lives in `namespace radar26`. The project is a RoboMaster radar system that combines camera input, TensorRT-based object detection (cars + armor plates), perspective mapping to a competition map, and serial/TCP communication with the referee system and other robots.

### Data flow

```
Camera (Daheng/USB/video) → frame grab thread
  → main loop: carDetector_.Infer() + armorDetector_.Infer()
  → TargetFilter (timeout-based smoothing per robot name)
  → perspective projection (image → map coords via calibration homography)
  → serial send (0x0305 position, 0x0121 radar cmd, 0x0301 custom data)
  → TCP send (0x01 trigger signal to 192.168.12.2:5000)
```

### Thread model (`radar_app.cpp`)

`RadarApp` owns all threads, started in `StartSerialThreads()`:

| Thread | Role |
|---|---|
| `grabThread_` | Async frame capture from camera |
| `sendThread_` | Periodic serial transmission (0x0305, 0x0121) |
| `recvThread_` | Serial byte read, feeds `parseQueue_` |
| `parserThreads_` | Pool parsing frames from `parseQueue_`, updates state |
| `tcpThread_` | TCP trigger signal send (0x01) |
| `tcpRxThread8001_`–`8004_` | TCP client receivers for enemy data (ports 8001–8004) |

### Key classes

- **RadarApp** — application orchestrator; owns all modules and threads. The biggest file (~78K source).
- **TrtYoloDetector** — TensorRT YOLO inference wrapper with PIMPL. Supports single and batch inference, dynamic batch detection.
- **SerialProtocol** — packet framing (SOF 0xA5), CRC8/CRC16, build/parse for all command IDs (0x0305, 0x0121, 0x0301, 0x020E, 0x020B, 0x0001, 0x0003, 0x0101, 0x0206).
- **SerialPort** — raw Linux termios serial wrapper (open/read/write/close).
- **SerialMonitor** — OpenCV `imshow` window rendering TX/RX status for 12 channels.
- **TcpClient** — blocking TCP client with connect/read/write (used for both trigger send and referee data receive).
- **TcpServer** — minimal TCP listener that parses 0xA5-framed packets in a worker thread.
- **TargetFilter** — fixed-slot map (name → slot index) with timeout-based invalidation for 12 robot targets.
- **ConfigLoader** — YAML → `AppConfig` / `CalibrationData` struct deserialization.
- **DahengCamera** — Daheng GxIAPI SDK wrapper (PIMPL, optional at build time via `RADAR26_WITH_DAHENG`).

### Protocol IDs received (serial)

| Cmd ID | Content | Handled by |
|---|---|---|
| `0x020E` | Radar info bits (encrypt level, double-vulnerability, key-modifiable, enemy-triggered) | `UpdateRadarInfoFromParsedPacket` |
| `0x0301` | Robot interaction positions (wraps 0x0201 sub-content) | `DecodeAllyRobotPositions0301` |
| `0x0001` | Game status (type, progress, remaining time) | `DecodeGameStatus0001` |
| `0x0003` | Outpost health | `DecodeOutpostHealth0003` |
| `0x0101` | Energy mechanism activation state | Parsed directly in `ParserWorker` |
| `0x020B` | Ally robot positions (ignored in current implementation) | — |

### Protocol IDs sent (serial)

| Cmd ID | Content | Trigger |
|---|---|---|
| `0x0305` | 12 robots × 2 coords (24 × uint16) | Periodic (config `send_period_ms`) |
| `0x0121` | Radar command + key data (double-vuln, encrypt key) | Event-driven from 0x020E state changes |
| `0x0301` | Custom radar data (forwards 0x0206, 66-byte payload) | When TCP 0x0A02–0x0A05 data is available |

### TCP referee data flow

- **Port 8001**: receives 0x0A01 (enemy positions, 24 bytes → forwarded to 0x0305 enemy slots), 0x0A02–0x0A05 (enemy HP/status, 66 bytes → forwarded via 0x0301/0x0206)
- **Ports 8002–8004**: receive 0x0A06 (cracked keys, 6 bytes each, one per interference level)
- TCP 800x data is rate-limited to 1Hz forwarding, and 0x0A02–0x0A05 segments are only forwarded once all four are valid.

### Calibration

1. Use the Python tool to click points → generates `.npy` (e.g., `arrays_test_red.npy`)
2. `scripts/convert_calibration.py` converts `.npy` → `config/calibration_*.yaml` (OpenCV FileStorage format, stores `M_ground`, `M_height_r`, `M_height_g` homography matrices + map/mask image paths)
3. Set `calibration.red_path` / `calibration.blue_path` in `config/app.yaml`

Alternatively use `build/Radar_26_calibration` for an interactive GUI calibration tool.

## Configuration

`config/app.yaml` drives everything. Key sections:
- `team`: `"R"` or `"B"`
- `camera.mode`: `"video_file"` | `"usb"` | `"daheng"`
- `serial.enable` / `serial.port` / `serial.baudrate` / `serial.send_period_ms`
- `tcp.enable` / `tcp.ip` / `tcp.port` — trigger signal target
- `model.car_engine` / `model.armor_engine` — TensorRT engine paths
- `detection` — confidence/IoU/max-det thresholds for car and armor detectors
- `calibration` — paths to homography YAML + map/mask images

Debug flags: `debug_request_key_update`, `debug_request_enemy_key`, `show_ui`, `opencv_threads`.

## Dependencies

- CMake ≥ 3.18, GCC with C++17
- OpenCV (core, imgproc, highgui, imgcodecs, videoio)
- CUDA Toolkit + TensorRT (libnvinfer, libnvinfer_plugin)
- pthread
- Optional: Daheng GxIAPI SDK (set `-DRADAR26_WITH_DAHENG=OFF` to skip)

/**
 * @file    camera_test_main.cpp
 * @brief   大恒相机独立测试工具。
 *
 * 本程序提供命令行界面，用于快速验证大恒相机是否正常工作：
 *   - 打开相机并持续采集画面
 *   - 显示实时帧率与分辨率
 *   - 支持按键保存快照（s 键）
 *   - 支持从 app.yaml 配置文件加载相机参数
 *
 * 使用方法：
 *   Radar_26_camera_test [--config PATH] [--daheng-index N] [--width N] ...
 *
 * 运行时快捷键：
 *   q — 退出
 *   s — 保存当前帧为 JPEG 快照
 */

#include "daheng_camera.hpp"
#include "config_loader.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// 匿名命名空间：所有工具函数和常量限定在本翻译单元内，避免符号冲突。
namespace {

/**
 * @brief 相机测试工具的命令行参数集合。
 *
 * 所有字段均提供合理默认值，用户可通过 CLI 参数覆写。
 */
struct CameraTestArgs {
    std::string configPath;          ///< 配置文件路径（app.yaml），空串表示使用默认路径
    int dahengIndex = 1;             ///< 大恒相机设备索引（从 1 开始）
    int width = 1920;                ///< 采集宽度（像素）
    int height = 1080;               ///< 采集高度（像素）
    double exposureUs = 18000.0;     ///< 曝光时间（微秒）
    double gain = 14.0;              ///< 模拟增益（dB）
    bool autoWhiteBalance = true;    ///< 是否启用自动白平衡
    bool flipVertical = false;       ///< 是否垂直翻转图像
    std::string savePath;            ///< 快照保存路径，空串使用默认文件名
};

/**
 * @brief 打印命令行帮助信息到标准输出。
 *
 * 列出所有可用的 CLI 参数含义、默认值及运行时按键说明。
 *
 * 参数：无。
 * 返回：无。
 * 副作用：向 stdout 写入文本。
 */
void PrintHelp() {
    std::cout << "Usage: Radar_26_camera_test [options]\n"
              << "\n"
              << "Options:\n"
              << "  --config PATH             Load camera settings from app.yaml\n"
              << "  --daheng-index N          Daheng index, 1-based (default: 1)\n"
              << "  --width N                 Capture width (default: 1920)\n"
              << "  --height N                Capture height (default: 1080)\n"
              << "  --exposure-us X           Exposure time in microseconds\n"
              << "  --gain X                  Analog gain\n"
              << "  --auto-wb 0|1             Daheng auto white balance (default: 1)\n"
              << "  --flip-vertical 0|1       Vertical flip (default: 0)\n"
              << "  --save PATH               Snapshot output path (default: camera.snapshot_path)\n"
              << "  -h, --help                Show this help\n"
              << "\n"
              << "Runtime keys:\n"
              << "  q : quit\n"
              << "  s : save current frame\n";
}

/**
 * @brief 将字符串解析为布尔值。
 *
 * 支持的 true 值： "1", "true", "TRUE"
 * 支持的 false 值："0", "false", "FALSE"
 *
 * @param name   参数名（仅用于错误提示）。
 * @param value  待解析的字符串值。
 * @param out    输出参数，解析成功时写入解析结果。
 * @param error  可选输出参数，解析失败时写入错误描述。
 * @return       true 表示解析成功；false 表示输入不是有效布尔字符串。
 *
 * 副作用：无（仅读取参数，通过 out/error 输出）。
 */
bool ParseBool01(const std::string& name, const std::string& value, bool* out, std::string* error) {
    if (value == "1" || value == "true" || value == "TRUE") {
        *out = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "FALSE") {
        *out = false;
        return true;
    }
    if (error != nullptr) {
        *error = "invalid value for " + name + ": " + value + " (expect 0/1/true/false)";
    }
    return false;
}

/**
 * @brief 解析配置文件路径，将相对路径规范化为绝对/标准形式。
 *
 * 若用户未指定路径，返回默认路径 "../config/app.yaml"。
 *
 * @param inputPath  用户原始输入路径（可为空字符串）。
 * @return           规范化后的路径字符串。
 *
 * 副作用：无（纯函数，不涉及文件 I/O）。
 */
std::string ResolveConfigPath(const std::string& inputPath) {
    if (!inputPath.empty()) {
        return std::filesystem::path(inputPath).lexically_normal().string();
    }

    return "../config/app.yaml";
}

/**
 * @brief 从 app.yaml 配置文件加载相机参数，填入 CameraTestArgs 结构体。
 *
 * 加载规则：
 *   - 若 args->configPath 非空则使用该路径，否则尝试默认路径。
 *   - 配置文件中的值覆盖 args 中的对应字段。
 *   - 若配置中未设置 savePath，则不会覆盖命令行指定的保存路径。
 *
 * @param args        输入输出参数，加载成功后各字段被配置文件值覆写。
 * @param loadedPath  可选输出参数，写入实际加载的配置文件路径。
 * @param error       可选输出参数，加载失败时写入错误描述。
 * @return            true 表示加载成功；false 表示解析或文件系统错误。
 *
 * 副作用：
 *   - 修改 args 指向的结构体字段。
 *   - 可能会读取磁盘文件（ConfigLoader::LoadAppConfig）。
 */
bool TryLoadCameraFromConfig(CameraTestArgs* args, std::string* loadedPath, std::string* error) {
    if (args == nullptr) {
        return false;
    }

    const std::string resolvedPath = ResolveConfigPath(args->configPath);

    radar26::AppConfig config;
    std::string loadError;
    if (!radar26::ConfigLoader::LoadAppConfig(resolvedPath, &config, &loadError)) {
        if (error != nullptr) {
            *error = "failed to load config " + resolvedPath + ": " + loadError;
        }
        return false;
    }

    args->dahengIndex = config.camera.dahengDeviceIndex;
    args->width = config.camera.width;
    args->height = config.camera.height;
    args->exposureUs = config.camera.exposureTime;
    args->gain = config.camera.gain;
    args->autoWhiteBalance = config.camera.dahengAutoWhiteBalance;
    args->flipVertical = config.camera.dahengFlipVertical;
    if (args->savePath.empty() && !config.camera.snapshotPath.empty()) {
        args->savePath = config.camera.snapshotPath;
    }

    if (loadedPath != nullptr) {
        *loadedPath = resolvedPath;
    }

    return true;
}

/**
 * @brief 解析命令行参数，填入 CameraTestArgs 结构体。
 *
 * 支持的参数请参考 PrintHelp() 输出。解析失败时通过 error 返回原因。
 *
 * @param argc  命令行参数个数（来自 main）。
 * @param argv  命令行参数字符串数组。
 * @param args  输出参数，解析成功后填入用户指定的值。
 * @param error 可选输出参数，解析失败时写入错误描述。
 * @return      true 表示解析成功；false 表示参数格式错误或未知参数。
 *
 * 副作用：
 *   - 遇到 --help / -h 时直接调用 std::exit(0) 终止进程。
 *   - 读取 argv 中的字符串（不修改 argv 本身）。
 */
bool ParseArgs(int argc, char** argv, CameraTestArgs* args, std::string* error) {
    if (args == nullptr) {
        if (error != nullptr) {
            *error = "args is null";
        }
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        /**
         * requireValue lambda：获取当前参数的下一个 token 作为值。
         * 若已到达参数列表末尾（缺少值），设置 error 并返回 nullptr。
         */
        auto requireValue = [&](const std::string& name) -> const char* {
            if (i + 1 >= argc) {
                if (error != nullptr) {
                    *error = "missing value for " + name;
                }
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            PrintHelp();
            std::exit(0);
        } else if (arg == "--config") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->configPath = v;
        } else if (arg == "--daheng-index") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->dahengIndex = std::max(1, std::stoi(v));
        } else if (arg == "--width") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->width = std::max(1, std::stoi(v));
        } else if (arg == "--height") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->height = std::max(1, std::stoi(v));
        } else if (arg == "--exposure-us") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->exposureUs = std::stod(v);
        } else if (arg == "--gain") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->gain = std::stod(v);
        } else if (arg == "--auto-wb") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            if (!ParseBool01(arg, v, &args->autoWhiteBalance, error)) {
                return false;
            }
        } else if (arg == "--flip-vertical") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            if (!ParseBool01(arg, v, &args->flipVertical, error)) {
                return false;
            }
        } else if (arg == "--save") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->savePath = v;
        } else {
            if (error != nullptr) {
                *error = "unknown argument: " + arg;
            }
            return false;
        }
    }

    return true;
}

/**
 * @brief 在帧上绘制信息叠加层（模式、帧率、分辨率、快捷键提示）。
 *
 * 将 frame 拷贝到 out，然后在 out 左上角绘制绿色文字叠加层。
 *
 * @param args   当前测试参数（用于读取模式名等）。
 * @param fps    当前实时帧率（浮点数）。
 * @param frame  原始采集帧（只读）。
 * @param out    输出参数，调用后包含带叠加层的新图像。
 *
 * 副作用：
 *   - out 的内存可能被重新分配（cv::copyTo）。
 *   - 不修改 frame。
 */
void DrawOverlay(const CameraTestArgs& args, double fps, const cv::Mat& frame, cv::Mat* out) {
    if (out == nullptr || frame.empty()) {
        return;
    }

    frame.copyTo(*out);
    const cv::Scalar textColor(0, 255, 0);

    cv::putText(*out, "mode=daheng", cv::Point(20, 35), cv::FONT_HERSHEY_SIMPLEX, 0.9, textColor, 2);
    cv::putText(*out, "fps=" + cv::format("%.2f", fps), cv::Point(20, 70), cv::FONT_HERSHEY_SIMPLEX, 0.9,
                textColor, 2);
    cv::putText(*out,
                "size=" + std::to_string(frame.cols) + "x" + std::to_string(frame.rows),
                cv::Point(20, 105), cv::FONT_HERSHEY_SIMPLEX, 0.9, textColor, 2);
    cv::putText(*out, "keys: q=quit, s=save", cv::Point(20, 140), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                cv::Scalar(255, 255, 255), 2);
}

}  // namespace

/**
 * @brief 程序入口：解析参数、打开相机、进入采集显示循环。
 *
 * 执行流程：
 *   1. 解析命令行参数。
 *   2. 尝试从配置文件加载相机参数（失败则使用 CLI 参数/默认值）。
 *   3. 构造 DahengCameraOptions 并打开相机。
 *   4. 创建 OpenCV 显示窗口，进入主循环：
 *      - 每帧调用 dahengCamera.Read() 读取图像
 *      - 计算并显示实时帧率（每 20 帧更新一次）
 *      - 绘制信息叠加层并 imshow
 *      - 响应按键：q=退出，s=保存快照
 *   5. 退出时关闭相机并销毁窗口。
 *
 * @param argc  命令行参数个数。
 * @param argv  命令行参数数组。
 * @return      0 表示正常退出；1 表示参数解析错误或相机打开失败。
 *
 * 副作用：
 *   - 创建 GUI 窗口（cv::namedWindow / cv::imshow）。
 *   - 可能创建快照文件（按 s 键时 cv::imwrite）。
 *   - 打开/关闭大恒相机设备。
 */
int main(int argc, char** argv) {
    CameraTestArgs args;
    std::string parseError;
    if (!ParseArgs(argc, argv, &args, &parseError)) {
        std::cerr << "Argument parse failed: " << parseError << std::endl;
        PrintHelp();
        return 1;
    }

    // 尝试从配置文件加载相机参数
    std::string loadedConfigPath;
    std::string configError;
    if (TryLoadCameraFromConfig(&args, &loadedConfigPath, &configError)) {
        std::cout << "camera config loaded from: " << loadedConfigPath << std::endl;
    } else {
        std::cout << "camera config load failed: " << configError
                  << " ; fallback to CLI/default daheng settings" << std::endl;
    }

    // 若未指定保存路径，使用默认文件名
    if (args.savePath.empty()) {
        args.savePath = "camera_test_snapshot.jpg";
    }

    std::cout << "camera test start mode=daheng index=" << args.dahengIndex << " size=" << args.width << "x"
              << args.height << " exposure=" << args.exposureUs << " gain=" << args.gain << std::endl;

    // 构造相机选项
    radar26::DahengCamera dahengCamera;
    radar26::DahengCameraOptions options;
    options.deviceIndex = args.dahengIndex;
    options.width = args.width;
    options.height = args.height;
    options.exposureTimeUs = args.exposureUs;
    options.gain = args.gain;
    options.autoWhiteBalance = args.autoWhiteBalance;
    options.flipVertical = args.flipVertical;

    // 打开相机
    std::string openError;
    if (!dahengCamera.Open(options, &openError)) {
        std::cerr << "failed to open daheng camera: " << openError << std::endl;
        return 1;
    }

    // 创建显示窗口
    cv::namedWindow("camera_test", cv::WINDOW_NORMAL);
    cv::resizeWindow("camera_test", 1280, 720);

    int frameCount = 0;              ///< 帧率采样计数器，每 20 帧重置一次
    double fps = 0.0;                ///< 当前实时帧率
    auto fpsT0 = std::chrono::steady_clock::now();  ///< 帧率采样起始时间点
    int continuousReadFailCount = 0; ///< 连续读取失败次数（用于降频告警）

    // ——— 主采集循环 ———
    while (true) {
        cv::Mat frame;
        bool ok = false;

        // 读取一帧
        std::string readError;
        ok = dahengCamera.Read(&frame, &readError);
        if (!ok) {
            continuousReadFailCount++;
            // 首次失败和每 60 次失败时打印错误，避免日志刷屏
            if (continuousReadFailCount == 1 || continuousReadFailCount % 60 == 0) {
                std::cerr << "read daheng frame failed(" << continuousReadFailCount
                          << "): " << readError << std::endl;
            }
        }

        if (!ok || frame.empty()) {
            continue;
        }

        // 帧读取成功，清零失败计数器
        continuousReadFailCount = 0;

        // ——— 帧率计算（每 20 帧更新一次） ———
        frameCount++;
        if (frameCount >= 20) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - fpsT0).count();
            if (elapsed > 1e-9) {
                fps = static_cast<double>(frameCount) / elapsed;
            }
            frameCount = 0;
            fpsT0 = now;
        }

        // ——— 绘制叠加层并显示 ———
        cv::Mat show;
        DrawOverlay(args, fps, frame, &show);
        cv::imshow("camera_test", show);

        // ——— 按键处理 ———
        const int key = cv::waitKey(1) & 0xFF;
        if (key == 'q') {
            break;  // 退出循环
        }
        if (key == 's') {
            // 保存当前帧为 JPEG 快照，必要时创建父目录
            std::error_code ec;
            const std::filesystem::path p(args.savePath);
            if (!p.parent_path().empty()) {
                std::filesystem::create_directories(p.parent_path(), ec);
            }
            if (cv::imwrite(args.savePath, frame)) {
                std::cout << "saved snapshot: " << args.savePath << std::endl;
            } else {
                std::cerr << "failed to save snapshot: " << args.savePath << std::endl;
            }
        }
    }

    // 清理资源
    dahengCamera.Close();
    cv::destroyAllWindows();
    return 0;
}

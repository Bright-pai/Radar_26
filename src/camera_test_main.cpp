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

namespace {

struct CameraTestArgs {
    std::string configPath;
    int dahengIndex = 1;
    int width = 1920;
    int height = 1080;
    double exposureUs = 18000.0;
    double gain = 14.0;
    bool autoWhiteBalance = true;
    bool flipVertical = false;
    std::string savePath;
};

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

std::string ResolveConfigPath(const std::string& inputPath) {
    if (!inputPath.empty()) {
        return std::filesystem::path(inputPath).lexically_normal().string();
    }

    return "../config/app.yaml";
}

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

bool ParseArgs(int argc, char** argv, CameraTestArgs* args, std::string* error) {
    if (args == nullptr) {
        if (error != nullptr) {
            *error = "args is null";
        }
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
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

int main(int argc, char** argv) {
    CameraTestArgs args;
    std::string parseError;
    if (!ParseArgs(argc, argv, &args, &parseError)) {
        std::cerr << "Argument parse failed: " << parseError << std::endl;
        PrintHelp();
        return 1;
    }

    std::string loadedConfigPath;
    std::string configError;
    if (TryLoadCameraFromConfig(&args, &loadedConfigPath, &configError)) {
        std::cout << "camera config loaded from: " << loadedConfigPath << std::endl;
    } else {
        std::cout << "camera config load failed: " << configError
                  << " ; fallback to CLI/default daheng settings" << std::endl;
    }

    if (args.savePath.empty()) {
        args.savePath = "camera_test_snapshot.jpg";
    }

    std::cout << "camera test start mode=daheng index=" << args.dahengIndex << " size=" << args.width << "x"
              << args.height << " exposure=" << args.exposureUs << " gain=" << args.gain << std::endl;

    radar26::DahengCamera dahengCamera;
    radar26::DahengCameraOptions options;
    options.deviceIndex = args.dahengIndex;
    options.width = args.width;
    options.height = args.height;
    options.exposureTimeUs = args.exposureUs;
    options.gain = args.gain;
    options.autoWhiteBalance = args.autoWhiteBalance;
    options.flipVertical = args.flipVertical;

    std::string openError;
    if (!dahengCamera.Open(options, &openError)) {
        std::cerr << "failed to open daheng camera: " << openError << std::endl;
        return 1;
    }

    cv::namedWindow("camera_test", cv::WINDOW_NORMAL);
    cv::resizeWindow("camera_test", 1280, 720);

    int frameCount = 0;
    double fps = 0.0;
    auto fpsT0 = std::chrono::steady_clock::now();
    int continuousReadFailCount = 0;

    while (true) {
        cv::Mat frame;
        bool ok = false;

        std::string readError;
        ok = dahengCamera.Read(&frame, &readError);
        if (!ok) {
            continuousReadFailCount++;
            if (continuousReadFailCount == 1 || continuousReadFailCount % 60 == 0) {
                std::cerr << "read daheng frame failed(" << continuousReadFailCount
                          << "): " << readError << std::endl;
            }
        }

        if (!ok || frame.empty()) {
            continue;
        }

        continuousReadFailCount = 0;

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

        cv::Mat show;
        DrawOverlay(args, fps, frame, &show);
        cv::imshow("camera_test", show);

        const int key = cv::waitKey(1) & 0xFF;
        if (key == 'q') {
            break;
        }
        if (key == 's') {
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

    dahengCamera.Close();
    cv::destroyAllWindows();
    return 0;
}

/**
 * @file camera_tuner.cpp
 * @brief 大恒相机实时调参工具 — 预览 + 滑轨 + 点击按钮保存退出
 *
 * 独立于主程序，不依赖 TensorRT / 串口 / TCP。
 * 调节后的参数直接写回 config/app.yaml 的 camera 节。
 */

#include "config_loader.hpp"
#include "daheng_camera.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---- 按钮区域（由主循环在每帧绘制后更新）----
cv::Rect gBtnRect(-1, -1, 0, 0);  // 初始化为屏幕外，避免误点击
bool gSaveClicked = false;
cv::Point gMousePos(-1, -1);

// ---- 鼠标回调：跟踪位置 + 检测按钮点击 ----
void OnMouse(int event, int x, int y, int, void*) {
    gMousePos = cv::Point(x, y);
    if (event == cv::EVENT_LBUTTONDOWN && gBtnRect.contains(gMousePos)) {
        gSaveClicked = true;
    }
}

// ---- 绘制按钮 ----
void DrawButton(cv::Mat& img, const cv::Rect& r, const std::string& text, bool hover) {
    cv::Scalar fill(0, 150, 0), border(0, 220, 0), txtColor(255, 255, 255);
    if (hover) { fill = cv::Scalar(0, 200, 0); border = cv::Scalar(0, 255, 0); }
    cv::rectangle(img, r, fill, cv::FILLED);
    cv::rectangle(img, r, border, 2);
    int baseline = 0;
    cv::Size ts = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.7, 2, &baseline);
    cv::Point org(r.x + (r.width - ts.width) / 2, r.y + (r.height + ts.height) / 2);
    cv::putText(img, text, org, cv::FONT_HERSHEY_SIMPLEX, 0.7, txtColor, 2);
}

// ---- YAML 写回：行级替换 camera 节内的 key ----
void SaveCameraParam(const std::string& configPath, const std::string& key, double value) {
    std::ifstream ifs(configPath);
    if (!ifs.is_open()) return;
    std::vector<std::string> lines;
    std::string line;
    bool inCamera = false;
    while (std::getline(ifs, line)) {
        if (!inCamera && line.find("camera:") != std::string::npos &&
            line.find_first_not_of(" \t") == 0) {
            inCamera = true;
        } else if (inCamera && !line.empty() && (line[0] != ' ' && line[0] != '\t')) {
            inCamera = false;
        }
        if (inCamera) {
            auto pos = line.find(key + ":");
            if (pos != std::string::npos && pos > 0 && line[pos - 1] == ' ') {
                auto valStart = line.find_first_of("0123456789", pos);
                auto valEnd   = line.find_first_not_of("0123456789.", valStart);
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << value;
                if (valStart != std::string::npos) {
                    line.replace(valStart,
                                 (valEnd == std::string::npos ? line.size() : valEnd) - valStart,
                                 oss.str());
                }
            }
        }
        lines.push_back(line);
    }
    ifs.close();
    std::ofstream ofs(configPath, std::ios::trunc);
    if (!ofs.is_open()) return;
    for (const auto& l : lines) ofs << l << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath = "../config/app.yaml";
    for (int i = 1; i < argc; ++i) {
        if (std::string arg = argv[i]; (arg == "--config" || arg == "-c") && i + 1 < argc)
            configPath = argv[++i];
    }

    // 1. 读取相机配置
    radar26::AppConfig cfg;
    std::string err;
    if (!radar26::ConfigLoader::LoadAppConfig(configPath, &cfg, &err)) {
        std::cerr << "Failed to load config: " << err << std::endl;
        return 1;
    }

    // 2. 打开大恒相机
    radar26::DahengCamera cam;
    radar26::DahengCameraOptions opts;
    opts.deviceIndex       = cfg.camera.dahengDeviceIndex;
    opts.width             = cfg.camera.width;
    opts.height            = cfg.camera.height;
    opts.exposureTimeUs    = cfg.camera.exposureTime;
    opts.gain              = cfg.camera.gain;
    opts.autoWhiteBalance  = cfg.camera.dahengAutoWhiteBalance;
    opts.flipVertical      = cfg.camera.dahengFlipVertical;

    if (!cam.Open(opts, &err)) {
        std::cerr << "Failed to open camera: " << err << std::endl;
        return 1;
    }
    std::cout << "Camera opened." << std::endl;

    // 3. 创建窗口 + 滑轨 + 鼠标回调
    const std::string kWinName = "Camera Tuner";
    cv::namedWindow(kWinName, cv::WINDOW_NORMAL);
    cv::resizeWindow(kWinName, 1280, 820);
    cv::setMouseCallback(kWinName, OnMouse);

    const int kMaxExp  = 20000;
    const int kMaxGain = 200;
    int initExp  = static_cast<int>(cfg.camera.exposureTime);
    int initGain = static_cast<int>(cfg.camera.gain * 10.0);

    cv::createTrackbar("Exposure (us)", kWinName, nullptr, kMaxExp);
    cv::createTrackbar("Gain (x0.1)",   kWinName, nullptr, kMaxGain);
    cv::setTrackbarPos("Exposure (us)", kWinName, std::clamp(initExp,  0, kMaxExp));
    cv::setTrackbarPos("Gain (x0.1)",   kWinName, std::clamp(initGain, 0, kMaxGain));

    double curExp  = static_cast<double>(initExp);
    double curGain = static_cast<double>(initGain) / 10.0;

    // 4. 主循环
    while (true) {
        cv::Mat frame;
        bool gotFrame = cam.Read(&frame, &err);

        if (gotFrame) {
            double scale = std::min(1280.0 / frame.cols, 680.0 / frame.rows);
            cv::Mat disp;
            cv::resize(frame, disp, cv::Size(), scale, scale);

            // 参数值显示
            std::ostringstream oss;
            oss << "Exp: " << static_cast<int>(curExp) << " us  "
                << "Gain: " << std::fixed << std::setprecision(1) << curGain;
            cv::putText(disp, oss.str(), cv::Point(10, 26),
                        cv::FONT_HERSHEY_SIMPLEX, 0.60, cv::Scalar(0, 255, 0), 2);

            // 保存并退出按钮（右下角）
            const int btnW = 180, btnH = 44, pad = 16;
            gBtnRect = cv::Rect(disp.cols - btnW - pad, disp.rows - btnH - pad, btnW, btnH);
            bool hover = gBtnRect.contains(gMousePos);

            DrawButton(disp, gBtnRect, "Save & Exit", hover);
            cv::imshow(kWinName, disp);
        } else {
            // 没拿到帧时显示空画布，按钮仍可点击
            cv::Mat blank(480, 640, CV_8UC3, cv::Scalar(40, 40, 40));
            gBtnRect = cv::Rect(640 - 180 - 16, 480 - 44 - 16, 180, 44);
            DrawButton(blank, gBtnRect, "Save & Exit", false);
            cv::imshow(kWinName, blank);
        }

        // 读取滑轨值
        int trackExp  = cv::getTrackbarPos("Exposure (us)", kWinName);
        int trackGain = cv::getTrackbarPos("Gain (x0.1)",   kWinName);
        double newExp  = static_cast<double>(std::clamp(trackExp,  0, kMaxExp));
        double newGain = static_cast<double>(std::clamp(trackGain, 0, kMaxGain)) / 10.0;

        if (newExp != curExp) { cam.SetExposureTime(newExp); curExp = newExp; }
        if (newGain != curGain) { cam.SetGain(newGain); curGain = newGain; }

        if (gSaveClicked) {
            SaveCameraParam(configPath, "exposure_time", curExp);
            SaveCameraParam(configPath, "gain",          curGain);
            std::cout << "Saved: exposure_time=" << static_cast<int>(curExp)
                      << " gain=" << curGain << std::endl;
            break;
        }

        int key = cv::waitKey(1) & 0xFF;
        if (key == 27) {  // ESC 放弃退出
            std::cout << "Discarded changes." << std::endl;
            break;
        }
    }

    cam.Close();
    cv::destroyAllWindows();
    return 0;
}

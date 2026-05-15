#include "daheng_camera.hpp"

#include "config_loader.hpp"
#include "radar_types.hpp"

#include <opencv2/core/persistence.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#if !defined(RADAR26_WITH_OPENCV_FREETYPE)
#define RADAR26_WITH_OPENCV_FREETYPE 0
#endif

#if RADAR26_WITH_OPENCV_FREETYPE
#include <opencv2/freetype.hpp>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kLayerCount = 3;
constexpr int kPointsPerLayer = 4;
constexpr char kLeftWindowName[] = "标定左窗（相机/视频）";
constexpr char kRightWindowName[] = "标定右窗（地图）";

const std::array<cv::Scalar, kLayerCount> kLayerColors = {
    cv::Scalar(255, 255, 255),
    cv::Scalar(0, 255, 0),
    cv::Scalar(0, 0, 255),
};

constexpr std::array<const char*, kLayerCount> kLayerNames = {
    "地面",
    "R高地",
    "环高",
};

std::string ResolveFirstExisting(const std::vector<std::string>& candidates) {
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec) && std::filesystem::is_regular_file(c, ec)) {
            return std::filesystem::path(c).lexically_normal().string();
        }
    }
    return candidates.empty() ? std::string() : candidates.front();
}

std::string ResolveFirstParentExisting(const std::vector<std::string>& candidates) {
    for (const auto& c : candidates) {
        const std::filesystem::path p(c);
        const std::filesystem::path parent = p.parent_path();
        std::error_code ec;
        if (parent.empty() || std::filesystem::exists(parent, ec)) {
            return p.lexically_normal().string();
        }
    }
    return candidates.empty() ? std::string() : candidates.front();
}

std::string NormalizeTeamToken(std::string team) {
    std::transform(team.begin(), team.end(), team.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (team == "B") {
        return "B";
    }
    return "R";
}

std::string DefaultMapPathForTeam(const std::string& team) {
    if (NormalizeTeamToken(team) == "R") {
        return ResolveFirstExisting({
            "../images/map_red.jpg",
            "../../images/map_red.jpg",
            "images/map_red.jpg",
        });
    }
    return ResolveFirstExisting({
        "../images/map_blue.jpg",
        "../../images/map_blue.jpg",
        "images/map_blue.jpg",
    });
}

std::string DefaultOutputPathForTeam(const std::string& team) {
    if (NormalizeTeamToken(team) == "R") {
        return ResolveFirstParentExisting({
            "../arrays_test_red.npy",
            "../../arrays_test_red.npy",
            "arrays_test_red.npy",
        });
    }
    return ResolveFirstParentExisting({
        "../arrays_test_blue.npy",
        "../../arrays_test_blue.npy",
        "arrays_test_blue.npy",
    });
}

std::string DefaultOutputYamlPathForTeam(const std::string& team) {
    if (NormalizeTeamToken(team) == "R") {
        return ResolveFirstParentExisting({
            "../config/calibration_red.yaml",
            "config/calibration_red.yaml",
            "Radar_26/config/calibration_red.yaml",
        });
    }
    return ResolveFirstParentExisting({
        "../config/calibration_blue.yaml",
        "config/calibration_blue.yaml",
        "Radar_26/config/calibration_blue.yaml",
    });
}

std::string DefaultMaskPathForTeam(const std::string& team) {
    if (NormalizeTeamToken(team) == "R") {
        return ResolveFirstExisting({
            "../images/map_red_s_mask.jpg",
            "../../images/map_red_s_mask.jpg",
            "images/map_red_s_mask.jpg",
        });
    }
    return ResolveFirstExisting({
        "../images/map_blue_stest_mask.jpg",
        "../../images/map_blue_stest_mask.jpg",
        "../images/map_blue_s_mask.jpg",
        "../../images/map_blue_s_mask.jpg",
        "images/map_blue_stest_mask.jpg",
    });
}

bool LoadMapAndMaskPathsFromYaml(const std::string& yamlPath, std::string* mapImagePath, std::string* maskImagePath) {
    if (mapImagePath == nullptr || maskImagePath == nullptr) {
        return false;
    }

    cv::FileStorage fs(yamlPath, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        return false;
    }

    const std::string mapPath = static_cast<std::string>(fs["map_image"]);
    const std::string maskPath = static_cast<std::string>(fs["mask_image"]);
    if (mapPath.empty() || maskPath.empty()) {
        return false;
    }

    *mapImagePath = mapPath;
    *maskImagePath = maskPath;
    return true;
}

bool WriteNpy3x3x3Float32(const std::string& path, const std::array<cv::Mat, kLayerCount>& matrices,
                          std::string* error) {
    std::array<float, kLayerCount * 3 * 3> data{};
    std::size_t idx = 0;
    for (int i = 0; i < kLayerCount; ++i) {
        if (matrices[i].rows != 3 || matrices[i].cols != 3) {
            if (error != nullptr) {
                *error = "matrix shape is not 3x3";
            }
            return false;
        }
        cv::Mat m32;
        matrices[i].convertTo(m32, CV_32F);
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                data[idx++] = m32.at<float>(r, c);
            }
        }
    }

    const std::string descr = "<f4";
    std::string header = "{'descr': '" + descr + "', 'fortran_order': False, 'shape': (3, 3, 3), }";
    const std::size_t preambleLen = 10;  // magic(6) + version(2) + header_len(2)
    const std::size_t rem = (preambleLen + header.size() + 1) % 16;
    const std::size_t pad = rem == 0 ? 0 : (16 - rem);
    header.append(pad, ' ');
    header.push_back('\n');

    if (header.size() > 65535U) {
        if (error != nullptr) {
            *error = "npy header too large";
        }
        return false;
    }

    std::error_code ec;
    const std::filesystem::path outPath(path);
    if (!outPath.parent_path().empty()) {
        std::filesystem::create_directories(outPath.parent_path(), ec);
    }

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) {
        if (error != nullptr) {
            *error = "无法打开 npy 输出路径: " + path;
        }
        return false;
    }

    const char magic[6] = {static_cast<char>(0x93), 'N', 'U', 'M', 'P', 'Y'};
    ofs.write(magic, 6);
    const char version[2] = {1, 0};
    ofs.write(version, 2);

    const uint16_t headerLen = static_cast<uint16_t>(header.size());
    const char h0 = static_cast<char>(headerLen & 0xFF);
    const char h1 = static_cast<char>((headerLen >> 8) & 0xFF);
    ofs.put(h0);
    ofs.put(h1);
    ofs.write(header.data(), static_cast<std::streamsize>(header.size()));

    ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(sizeof(float) * data.size()));
    ofs.flush();

    if (!ofs.good()) {
        if (error != nullptr) {
            *error = "failed while writing npy file";
        }
        return false;
    }

    return true;
}

bool WriteCalibrationYaml(const std::string& yamlPath, const std::array<cv::Mat, kLayerCount>& matrices,
                          const std::string& mapImagePath, const std::string& maskImagePath, std::string* error) {
    std::error_code ec;
    const std::filesystem::path outPath(yamlPath);
    if (!outPath.parent_path().empty()) {
        std::filesystem::create_directories(outPath.parent_path(), ec);
    }

    cv::FileStorage fs(yamlPath, cv::FileStorage::WRITE);
    if (!fs.isOpened()) {
        if (error != nullptr) {
            *error = "无法打开 yaml 输出路径: " + yamlPath;
        }
        return false;
    }

    auto writeMat = [&](const char* key, const cv::Mat& src) -> bool {
        if (src.rows != 3 || src.cols != 3) {
            if (error != nullptr) {
                *error = std::string("invalid matrix shape for key: ") + key;
            }
            return false;
        }
        cv::Mat m32;
        src.convertTo(m32, CV_32F);
        fs.write(key, m32);
        return true;
    };

    if (!writeMat("M_ground", matrices[0]) || !writeMat("M_height_r", matrices[1]) ||
        !writeMat("M_height_g", matrices[2])) {
        return false;
    }

    fs.write("map_image", mapImagePath);
    fs.write("mask_image", maskImagePath);
    fs.release();

    return true;
}

enum class SourceMode {
    Test,
    VideoFile,
    Daheng,
};

struct CalibrationArgs {
    SourceMode mode = SourceMode::Daheng;
    std::string team = "R";
    std::string videoPath;
    std::string testImagePath;
    std::string mapPath;
    bool mapPathOverridden = false;
    int dahengIndex = 1;
    int dahengWidth = 1920;
    int dahengHeight = 1080;
    double dahengExposureUs = 18000.0;
    double dahengGain = 14.0;
    bool dahengAutoWhiteBalance = true;
    bool dahengFlipVertical = false;
    std::string outputNpyPath;
    bool outputPathOverridden = false;
    std::string outputYamlPath;
    bool outputYamlPathOverridden = false;
    int leftWidth = 1350;
    int leftHeight = 1000;
    int rightWidth = 550;
    int rightHeight = 900;
};

void PrintHelp() {
    std::cout << "Usage: Radar_26_calibration [options]\n"
              << "\n"
              << "参数说明:\n"
              << "  --mode test|video_file|daheng      输入模式（默认 daheng）\n"
              << "  --team R|B                     阵营（影响默认地图与输出文件）\n"
              << "  --video PATH                   video_file 模式视频路径\n"
              << "  --test-image PATH              test 模式图片路径\n"
              << "  --map PATH                     地图路径（默认按阵营）\n"
              << "  --daheng-index N               大恒相机索引（1 开始）\n"
              << "  --width N                      daheng 模式采集宽度\n"
              << "  --height N                     daheng 模式采集高度\n"
              << "  --exposure-us X                daheng 曝光时间（微秒）\n"
              << "  --gain X                       daheng 增益\n"
              << "  --daheng-auto-wb 0|1           大恒自动白平衡\n"
              << "  --daheng-flip-vertical 0|1     大恒图像垂直翻转\n"
              << "  --output-npy PATH              输出 npy 路径（默认按阵营）\n"
              << "  --output-yaml PATH             同步输出 yaml 路径（默认按阵营）\n"
              << "  -h, --help                     显示帮助\n"
              << "\n"
              << "快捷键:\n"
              << "  s : 开始/冻结标定画面\n"
              << "  h : 切换高度层（地面 -> R高地 -> 环高 -> 地面）\n"
              << "  t : 切换阵营（R <-> B）\n"
              << "  c : 清空当前层点位\n"
              << "  r : 清空全部层点位\n"
              << "  w : 保存 npy 并同步 yaml\n"
              << "  q : 不保存退出\n"
              << "\n"
              << "鼠标:\n"
              << "  左窗口点击  : 可点击顶部按钮，或在图像区域选择相机点\n"
              << "  右窗口点击  : 选择地图对应点\n";
}

bool ParseArgs(int argc, char** argv, CalibrationArgs* args, std::string* error) {
    if (args == nullptr) {
        if (error != nullptr) {
            *error = "args pointer is null";
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
        auto parseBool01 = [&](const std::string& name, bool* out) -> bool {
            const char* v = requireValue(name);
            if (v == nullptr) {
                return false;
            }
            const std::string raw(v);
            if (raw == "1" || raw == "true" || raw == "TRUE") {
                *out = true;
                return true;
            }
            if (raw == "0" || raw == "false" || raw == "FALSE") {
                *out = false;
                return true;
            }
            if (error != nullptr) {
                *error = "invalid bool for " + name + ": " + raw + " (expect 0/1/true/false)";
            }
            return false;
        };

        if (arg == "--mode") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            const std::string mode(v);
            if (mode == "test") {
                args->mode = SourceMode::Test;
            } else if (mode == "video_file") {
                args->mode = SourceMode::VideoFile;
            } else if (mode == "daheng") {
                args->mode = SourceMode::Daheng;
            } else {
                if (error != nullptr) {
                    *error = "invalid --mode: " + mode;
                }
                return false;
            }
        } else if (arg == "--team") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->team = NormalizeTeamToken(v);
            if (args->team != "R" && args->team != "B") {
                if (error != nullptr) {
                    *error = "--team must be R or B";
                }
                return false;
            }
        } else if (arg == "--video") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->videoPath = v;
        } else if (arg == "--test-image") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->testImagePath = v;
        } else if (arg == "--map") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->mapPath = v;
            args->mapPathOverridden = true;
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
            args->dahengWidth = std::max(1, std::stoi(v));
        } else if (arg == "--height") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->dahengHeight = std::max(1, std::stoi(v));
        } else if (arg == "--exposure-us") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->dahengExposureUs = std::stod(v);
        } else if (arg == "--gain") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->dahengGain = std::stod(v);
        } else if (arg == "--daheng-auto-wb") {
            if (!parseBool01(arg, &args->dahengAutoWhiteBalance)) {
                return false;
            }
        } else if (arg == "--daheng-flip-vertical") {
            if (!parseBool01(arg, &args->dahengFlipVertical)) {
                return false;
            }
        } else if (arg == "--output-npy") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->outputNpyPath = v;
            args->outputPathOverridden = true;
        } else if (arg == "--output-yaml") {
            const char* v = requireValue(arg);
            if (v == nullptr) {
                return false;
            }
            args->outputYamlPath = v;
            args->outputYamlPathOverridden = true;
        } else if (arg == "-h" || arg == "--help") {
            PrintHelp();
            std::exit(0);
        } else {
            if (error != nullptr) {
                *error = "unknown argument: " + arg;
            }
            return false;
        }
    }

    if (args->videoPath.empty()) {
        args->videoPath = ResolveFirstExisting({
            "../images/bench_min3_60s.mp4",
            "../../images/bench_min3_60s.mp4",
            "../images/robot_segment_2min.mp4",
            "../../images/robot_segment_2min.mp4",
        });
    }
    if (args->testImagePath.empty()) {
        args->testImagePath = ResolveFirstExisting({
            "../images/test_image.jpg",
            "../../images/test_image.jpg",
            "images/test_image.jpg",
        });
    }
    if (args->mapPath.empty()) {
        args->mapPath = DefaultMapPathForTeam(args->team);
    }
    if (args->outputNpyPath.empty()) {
        args->outputNpyPath = DefaultOutputPathForTeam(args->team);
    }
    if (args->outputYamlPath.empty()) {
        args->outputYamlPath = DefaultOutputYamlPathForTeam(args->team);
    }

    return true;
}

class CalibrationTool {
public:
    explicit CalibrationTool(CalibrationArgs args) : args_(std::move(args)) {}

    bool Initialize(std::string* error) {
        if (!LoadMapImage(args_.mapPath, error)) {
            return false;
        }
        if (!OpenSource(error)) {
            return false;
        }

        InitializeTextRenderer();
        UpdateStatus("点击开始标定冻结画面，然后按左图/右图顺序依次取点。标定程序模式请用 --mode 设置。");
        return true;
    }

    int Run() {
        cv::namedWindow(kLeftWindowName, cv::WINDOW_NORMAL);
        cv::namedWindow(kRightWindowName, cv::WINDOW_NORMAL);
        cv::resizeWindow(kLeftWindowName, args_.leftWidth, args_.leftHeight);
        cv::resizeWindow(kRightWindowName, args_.rightWidth, args_.rightHeight);

        cv::setMouseCallback(kLeftWindowName, &CalibrationTool::OnLeftMouse, this);
        cv::setMouseCallback(kRightWindowName, &CalibrationTool::OnRightMouse, this);

        while (!exitRequested_) {
            if (capturing_) {
                if (!ReadSourceFrame()) {
                    frameReadFailCount_++;
                    if (frameReadFailCount_ == 1 || frameReadFailCount_ % 60 == 0) {
                        std::ostringstream oss;
                        oss << "取帧失败(" << frameReadFailCount_ << ")，请检查相机连线和模式配置";
                        UpdateStatus(oss.str());
                    }
                } else {
                    frameReadFailCount_ = 0;
                }
            }

            DrawViews();
            cv::imshow(kLeftWindowName, leftDisplay_);
            cv::imshow(kRightWindowName, rightDisplay_);

            const int key = cv::waitKey(20) & 0xFF;
            if (key == 'q') {
                UpdateStatus("已退出（未保存）");
                break;
            }
            if (key == 's') {
                ToggleCapture();
            }
            if (key == 'h') {
                SwitchLayer();
            }
            if (key == 'c') {
                ClearCurrentLayer();
            }
            if (key == 'w') {
                std::string error;
                (void)SaveCalibration(&error);
            }
            if (key == 't') {
                ToggleTeam();
            }
            if (key == 'r') {
                ClearAllLayers();
            }
        }

        cv::destroyAllWindows();
        return 0;
    }

private:
    struct UiButton {
        std::string id;
        cv::Rect rect;
        std::string label;
    };

    static constexpr int kControlPanelHeight = 210;

    static void OnLeftMouse(int event, int x, int y, int, void* userdata) {
        if (event != cv::EVENT_LBUTTONDOWN || userdata == nullptr) {
            return;
        }
        auto* self = reinterpret_cast<CalibrationTool*>(userdata);
        self->HandleLeftClick(x, y);
    }

    static void OnRightMouse(int event, int x, int y, int, void* userdata) {
        if (event != cv::EVENT_LBUTTONDOWN || userdata == nullptr) {
            return;
        }
        auto* self = reinterpret_cast<CalibrationTool*>(userdata);
        self->HandleRightClick(x, y);
    }

    bool LoadMapImage(const std::string& path, std::string* error) {
        cv::Mat map = cv::imread(path, cv::IMREAD_COLOR);
        if (map.empty()) {
            if (error != nullptr) {
                *error = "无法打开地图图像: " + path;
            }
            return false;
        }
        mapRaw_ = map;
        return true;
    }

    bool OpenSource(std::string* error) {
        if (args_.mode == SourceMode::Test) {
            leftRaw_ = cv::imread(args_.testImagePath, cv::IMREAD_COLOR);
            if (leftRaw_.empty()) {
                if (error != nullptr) {
                    *error = "无法打开测试图像: " + args_.testImagePath;
                }
                return false;
            }
            return true;
        }

        if (args_.mode == SourceMode::VideoFile) {
            capture_.open(args_.videoPath);
            if (!capture_.isOpened()) {
                if (error != nullptr) {
                    *error = "无法打开视频文件: " + args_.videoPath;
                }
                return false;
            }
            if (!ReadSourceFrame()) {
                if (error != nullptr) {
                    *error = "无法读取视频首帧";
                }
                return false;
            }
            return true;
        }

        radar26::DahengCameraOptions options;
        options.deviceIndex = args_.dahengIndex;
        options.width = args_.dahengWidth;
        options.height = args_.dahengHeight;
        options.exposureTimeUs = args_.dahengExposureUs;
        options.gain = args_.dahengGain;
        options.autoWhiteBalance = args_.dahengAutoWhiteBalance;
        options.flipVertical = args_.dahengFlipVertical;

        std::string openError;
        if (!dahengCamera_.Open(options, &openError)) {
            if (error != nullptr) {
                *error = "无法打开大恒相机: " + openError;
            }
            return false;
        }

        if (!ReadSourceFrame()) {
            if (error != nullptr) {
                *error = "无法读取大恒相机首帧";
            }
            return false;
        }

        return true;
    }

    bool ReadSourceFrame() {
        if (args_.mode == SourceMode::Test) {
            return !leftRaw_.empty();
        }

        if (args_.mode == SourceMode::Daheng) {
            cv::Mat frame;
            std::string readError;
            if (!dahengCamera_.Read(&frame, &readError) || frame.empty()) {
                return false;
            }
            leftRaw_ = frame;
            return true;
        }

        cv::Mat frame;
        if (!capture_.read(frame)) {
            if (args_.mode == SourceMode::VideoFile) {
                capture_.set(cv::CAP_PROP_POS_FRAMES, 0);
                if (!capture_.read(frame)) {
                    return false;
                }
            } else {
                return false;
            }
        }

        if (frame.empty()) {
            return false;
        }
        leftRaw_ = frame;
        return true;
    }

    void UpdateStatus(const std::string& text) {
        statusText_ = text;
        std::cout << text << std::endl;
    }

    int UiFontPixelHeight(double fontScale) const {
        return std::max(15, static_cast<int>(std::lround(fontScale * 30.0)));
    }

    void InitializeTextRenderer() {
#if RADAR26_WITH_OPENCV_FREETYPE
        const std::vector<std::string> fontCandidates = {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Black.ttc",
            "/usr/share/fonts/truetype/arphic/uming.ttc",
            "/usr/share/fonts/truetype/arphic/ukai.ttc",
        };

        for (const auto& path : fontCandidates) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                continue;
            }

            try {
                chineseFont_ = cv::freetype::createFreeType2();
                chineseFont_->loadFontData(path, 0);
                chineseFontReady_ = true;
                chineseFontPath_ = path;
                std::cout << "已启用中文字体渲染: " << path << std::endl;
                return;
            } catch (const cv::Exception& e) {
                std::cerr << "加载字体失败(" << path << "): " << e.what() << std::endl;
            }
        }

        std::cerr << "未找到可用中文字体，界面文字可能显示为问号" << std::endl;
#else
        std::cerr << "当前未链接 opencv_freetype，界面中文可能显示为问号" << std::endl;
#endif
    }

    void DrawUiText(cv::Mat* img, const std::string& text, const cv::Point& org, double fontScale,
                    const cv::Scalar& color, int thickness = 1) {
        if (img == nullptr || img->empty()) {
            return;
        }

#if RADAR26_WITH_OPENCV_FREETYPE
        if (chineseFontReady_ && chineseFont_) {
            chineseFont_->putText(*img, text, org, UiFontPixelHeight(fontScale), color, -1, cv::LINE_AA, true);
            return;
        }
#endif

        cv::putText(*img, text, org, cv::FONT_HERSHEY_SIMPLEX, fontScale, color, thickness, cv::LINE_AA);
    }

    void BuildControlButtons() {
        buttons_.clear();
        const int x0 = 12;
        const int y0 = 14;
        const int bw = 208;
        const int bh = 36;
        const int gap = 10;

        buttons_.push_back(UiButton{"toggle_capture", cv::Rect(x0, y0, bw, bh),
                                    capturing_ ? "开始标定（冻结）" : "恢复实时画面"});
        buttons_.push_back(UiButton{"switch_layer", cv::Rect(x0 + (bw + gap), y0, bw, bh),
                                    "切换高度层: " + std::string(kLayerNames[currentLayer_])});
        buttons_.push_back(UiButton{"save", cv::Rect(x0 + 2 * (bw + gap), y0, bw, bh), "保存 NPY+YAML"});
        buttons_.push_back(UiButton{"toggle_team", cv::Rect(x0 + 3 * (bw + gap), y0, bw, bh),
                                    "阵营: " + args_.team + "（点击切换）"});

        buttons_.push_back(UiButton{"clear_layer", cv::Rect(x0, y0 + bh + gap, bw, bh), "清空当前层"});
        buttons_.push_back(UiButton{"clear_all", cv::Rect(x0 + (bw + gap), y0 + bh + gap, bw, bh),
                                    "清空全部层"});
        buttons_.push_back(UiButton{"quit", cv::Rect(x0 + 2 * (bw + gap), y0 + bh + gap, bw, bh),
                                    "不保存退出"});
    }

    std::string CurrentStepHint() const {
        if (capturing_) {
            return "步骤1：点击开始标定，先冻结当前画面";
        }

        const std::size_t imgCount = imagePoints_[currentLayer_].size();
        const std::size_t mapCount = mapPoints_[currentLayer_].size();
        if (imgCount < mapCount) {
            return "步骤2：先点左图，添加下一个相机点";
        }
        if (mapCount < imgCount) {
            return "步骤3：再点右图，添加对应地图点";
        }
        if (imgCount >= kPointsPerLayer && mapCount >= kPointsPerLayer) {
            return "当前层已完成：请切换高度层继续";
        }
        return "步骤2/3：左右点击顺序必须严格一致";
    }

    void DrawButtons(cv::Mat* img) {
        if (img == nullptr || img->empty()) {
            return;
        }

        BuildControlButtons();
        for (const auto& btn : buttons_) {
            cv::Scalar fill(55, 55, 55);
            if (btn.id == "toggle_capture") {
                fill = capturing_ ? cv::Scalar(20, 120, 20) : cv::Scalar(20, 90, 150);
            } else if (btn.id == "save") {
                fill = CanSave() ? cv::Scalar(20, 130, 20) : cv::Scalar(90, 90, 90);
            } else if (btn.id == "toggle_team") {
                fill = (args_.team == "R") ? cv::Scalar(40, 40, 190) : cv::Scalar(190, 70, 30);
            } else if (btn.id == "quit") {
                fill = cv::Scalar(30, 30, 150);
            }

            cv::rectangle(*img, btn.rect, fill, cv::FILLED);
            cv::rectangle(*img, btn.rect, cv::Scalar(255, 255, 255), 1);
            DrawUiText(img, btn.label, cv::Point(btn.rect.x + 8, btn.rect.y + 25), 0.57,
                       cv::Scalar(255, 255, 255), 1);
        }
    }

    void DrawProgressAndStatus(cv::Mat* img) {
        if (img == nullptr || img->empty()) {
            return;
        }

        cv::rectangle(*img, cv::Rect(0, 0, img->cols, kControlPanelHeight), cv::Scalar(20, 20, 20), 2);
        DrawButtons(img);

        int y = 108;
        for (int i = 0; i < kLayerCount; ++i) {
            const auto imgCount = imagePoints_[i].size();
            const auto mapCount = mapPoints_[i].size();
            std::ostringstream oss;
            oss << (i == currentLayer_ ? "> " : "  ") << "层[" << i << "] " << kLayerNames[i] << " 左图 "
                << imgCount << "/" << kPointsPerLayer << " 右图 " << mapCount << "/" << kPointsPerLayer;
            cv::Scalar c(180, 180, 180);
            if (imgCount == kPointsPerLayer && mapCount == kPointsPerLayer) {
                c = cv::Scalar(40, 210, 40);
            } else if (i == currentLayer_) {
                c = cv::Scalar(0, 255, 255);
            }
            DrawUiText(img, oss.str(), cv::Point(14, y), 0.56, c, 1);
            y += 22;
        }

        DrawUiText(img, "提示: " + CurrentStepHint(), cv::Point(14, y), 0.55, cv::Scalar(255, 255, 255), 1);
        y += 22;

        DrawUiText(img, "状态: " + statusText_, cv::Point(14, y), 0.54, cv::Scalar(0, 255, 255), 1);
    }

    bool HandleButtonClick(int x, int y) {
        for (const auto& btn : buttons_) {
            if (!btn.rect.contains(cv::Point(x, y))) {
                continue;
            }

            if (btn.id == "toggle_capture") {
                ToggleCapture();
            } else if (btn.id == "switch_layer") {
                SwitchLayer();
            } else if (btn.id == "save") {
                std::string error;
                (void)SaveCalibration(&error);
            } else if (btn.id == "toggle_team") {
                ToggleTeam();
            } else if (btn.id == "clear_layer") {
                ClearCurrentLayer();
            } else if (btn.id == "clear_all") {
                ClearAllLayers();
            } else if (btn.id == "quit") {
                UpdateStatus("已退出（未保存）");
                exitRequested_ = true;
            }
            return true;
        }
        return false;
    }

    void ToggleCapture() {
        if (leftRaw_.empty()) {
            UpdateStatus("当前无可用画面，无法开始标定");
            return;
        }
        capturing_ = !capturing_;
        UpdateStatus(capturing_ ? "已恢复实时画面" : "已冻结画面，请开始取点");
    }

    void SwitchLayer() {
        currentLayer_ = (currentLayer_ + 1) % kLayerCount;
        UpdateStatus("已切换到第 " + std::to_string(currentLayer_) + " 层（" + kLayerNames[currentLayer_] + "）");
    }

    void ClearCurrentLayer() {
        imagePoints_[currentLayer_].clear();
        mapPoints_[currentLayer_].clear();
        UpdateStatus("已清空当前层点位：第 " + std::to_string(currentLayer_) + " 层");
    }

    void ClearAllLayers() {
        for (int i = 0; i < kLayerCount; ++i) {
            imagePoints_[i].clear();
            mapPoints_[i].clear();
        }
        currentLayer_ = 0;
        hasSaved_ = false;
        UpdateStatus("已清空全部层点位，并重置到第 0 层");
    }

    void ToggleTeam() {
        const std::string newTeam = (args_.team == "R") ? "B" : "R";

        CalibrationArgs candidate = args_;
        candidate.team = newTeam;
        if (!candidate.mapPathOverridden) {
            candidate.mapPath = DefaultMapPathForTeam(newTeam);
        }
        if (!candidate.outputPathOverridden) {
            candidate.outputNpyPath = DefaultOutputPathForTeam(newTeam);
        }
        if (!candidate.outputYamlPathOverridden) {
            candidate.outputYamlPath = DefaultOutputYamlPathForTeam(newTeam);
        }

        std::string loadError;
        if (!LoadMapImage(candidate.mapPath, &loadError)) {
            UpdateStatus("阵营切换失败: " + loadError);
            return;
        }

        args_ = candidate;
        for (int i = 0; i < kLayerCount; ++i) {
            imagePoints_[i].clear();
            mapPoints_[i].clear();
        }
        currentLayer_ = 0;
        capturing_ = true;
        hasSaved_ = false;

        UpdateStatus("已切换到阵营 " + args_.team + "，地图/输出路径已更新并清空点位");
    }

    bool SaveCalibration(std::string* error) {
        if (!CanSave()) {
            if (error != nullptr) {
                *error = "cannot save: each layer needs 4 image points and 4 map points";
            }
            UpdateStatus("保存失败：每一层都需要 4 个左图点 + 4 个右图点");
            return false;
        }

        std::array<cv::Mat, kLayerCount> transforms;
        for (int i = 0; i < kLayerCount; ++i) {
            std::array<cv::Point2f, kPointsPerLayer> src{};
            std::array<cv::Point2f, kPointsPerLayer> dst{};
            for (int j = 0; j < kPointsPerLayer; ++j) {
                src[j] = imagePoints_[i][j];
                dst[j] = mapPoints_[i][j];
            }
            transforms[i] = cv::getPerspectiveTransform(src.data(), dst.data());
        }

        std::string saveError;
        if (!WriteNpy3x3x3Float32(args_.outputNpyPath, transforms, &saveError)) {
            if (error != nullptr) {
                *error = saveError;
            }
            UpdateStatus("保存失败: " + saveError);
            return false;
        }

        std::string mapImageForYaml;
        std::string maskImageForYaml;
        if (!LoadMapAndMaskPathsFromYaml(args_.outputYamlPath, &mapImageForYaml, &maskImageForYaml)) {
            mapImageForYaml = args_.mapPath;
            maskImageForYaml = DefaultMaskPathForTeam(args_.team);
        }

        if (!WriteCalibrationYaml(args_.outputYamlPath, transforms, mapImageForYaml, maskImageForYaml, &saveError)) {
            const std::string msg = "NPY 已保存，但 YAML 同步失败: " + saveError;
            if (error != nullptr) {
                *error = msg;
            }
            UpdateStatus(msg);
            return false;
        }

        hasSaved_ = true;
        lastSavedPath_ = args_.outputNpyPath;
        UpdateStatus("保存成功: " + args_.outputNpyPath + "；已同步 YAML: " + args_.outputYamlPath);
        return true;
    }

    void DrawViews() {
        const cv::Mat& baseLeft = leftRaw_;

        if (baseLeft.empty()) {
            leftDisplay_ = cv::Mat(args_.leftHeight, args_.leftWidth, CV_8UC3, cv::Scalar(0, 0, 0));
            leftScaleX_ = 1.0;
            leftScaleY_ = 1.0;
        } else {
            cv::resize(baseLeft, leftDisplay_, cv::Size(args_.leftWidth, args_.leftHeight));
            leftScaleX_ = static_cast<double>(baseLeft.cols) / static_cast<double>(args_.leftWidth);
            leftScaleY_ = static_cast<double>(baseLeft.rows) / static_cast<double>(args_.leftHeight);
        }

        cv::resize(mapRaw_, rightDisplay_, cv::Size(args_.rightWidth, args_.rightHeight));
        rightScaleX_ = static_cast<double>(mapRaw_.cols) / static_cast<double>(args_.rightWidth);
        rightScaleY_ = static_cast<double>(mapRaw_.rows) / static_cast<double>(args_.rightHeight);

        for (int layer = 0; layer < kLayerCount; ++layer) {
            const cv::Scalar color = kLayerColors[layer];
            for (std::size_t i = 0; i < imagePoints_[layer].size(); ++i) {
                const cv::Point2f& p = imagePoints_[layer][i];
                const cv::Point dp(static_cast<int>(std::lround(p.x / leftScaleX_)),
                                   static_cast<int>(std::lround(p.y / leftScaleY_)));
                const int radius = (layer == currentLayer_) ? 6 : 4;
                cv::circle(leftDisplay_, dp, radius, color, -1);
                cv::putText(leftDisplay_, std::to_string(i), dp + cv::Point(6, -6), cv::FONT_HERSHEY_SIMPLEX,
                            0.6, color, 2);
            }
            for (std::size_t i = 0; i < mapPoints_[layer].size(); ++i) {
                const cv::Point2f& p = mapPoints_[layer][i];
                const cv::Point dp(static_cast<int>(std::lround(p.x / rightScaleX_)),
                                   static_cast<int>(std::lround(p.y / rightScaleY_)));
                const int radius = (layer == currentLayer_) ? 6 : 4;
                cv::circle(rightDisplay_, dp, radius, color, -1);
                cv::putText(rightDisplay_, std::to_string(i), dp + cv::Point(6, -6), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                            color, 2);
            }
        }

        DrawProgressAndStatus(&leftDisplay_);

        DrawUiText(&rightDisplay_, "阵营=" + args_.team + " 层=" + std::to_string(currentLayer_) +
                           "(" + kLayerNames[currentLayer_] + ")",
               cv::Point(10, 28), 0.72, cv::Scalar(0, 255, 255), 2);
        DrawUiText(&rightDisplay_, "右图点选顺序必须与左图完全一致", cv::Point(10, 55), 0.60,
               cv::Scalar(0, 255, 255), 2);

        std::ostringstream rightStatus;
        rightStatus << "输出NPY: " << args_.outputNpyPath;
        DrawUiText(&rightDisplay_, rightStatus.str(), cv::Point(10, rightDisplay_.rows - 38), 0.50,
                   cv::Scalar(220, 220, 220), 1);

        std::ostringstream rightYamlStatus;
        rightYamlStatus << "输出YAML: " << args_.outputYamlPath;
        DrawUiText(&rightDisplay_, rightYamlStatus.str(), cv::Point(10, rightDisplay_.rows - 62), 0.48,
               cv::Scalar(200, 200, 200), 1);

        if (hasSaved_) {
            DrawUiText(&rightDisplay_, "已保存: " + lastSavedPath_, cv::Point(10, rightDisplay_.rows - 14),
                       0.50, cv::Scalar(0, 255, 0), 1);
        } else {
            DrawUiText(&rightDisplay_, "尚未保存", cv::Point(10, rightDisplay_.rows - 14), 0.50,
                       cv::Scalar(0, 160, 255), 1);
        }
    }

    void HandleLeftClick(int x, int y) {
        if (HandleButtonClick(x, y)) {
            return;
        }

        if (y <= kControlPanelHeight) {
            UpdateStatus("请点击顶部控制按钮，或在按钮下方图像区域进行取点");
            return;
        }

        if (capturing_) {
            UpdateStatus("请先点击开始标定冻结画面");
            return;
        }
        if (leftRaw_.empty()) {
            UpdateStatus("左图画面为空");
            return;
        }
        auto& points = imagePoints_[currentLayer_];
        if (points.size() >= kPointsPerLayer) {
            UpdateStatus("当前层左图点位已满");
            return;
        }

        const float rawX = static_cast<float>(x * leftScaleX_);
        const float rawY = static_cast<float>(y * leftScaleY_);
        points.emplace_back(rawX, rawY);

        std::ostringstream oss;
        oss << "左图已记录点: 层=" << currentLayer_ << " 序号=" << (points.size() - 1) << " 坐标(" << rawX
            << ", " << rawY << ")";
        UpdateStatus(oss.str());
    }

    void HandleRightClick(int x, int y) {
        if (capturing_) {
            UpdateStatus("请先点击开始标定冻结画面");
            return;
        }
        auto& points = mapPoints_[currentLayer_];
        if (points.size() >= kPointsPerLayer) {
            UpdateStatus("当前层右图点位已满");
            return;
        }

        const float rawX = static_cast<float>(x * rightScaleX_);
        const float rawY = static_cast<float>(y * rightScaleY_);
        points.emplace_back(rawX, rawY);

        std::ostringstream oss;
        oss << "右图已记录点: 层=" << currentLayer_ << " 序号=" << (points.size() - 1) << " 坐标(" << rawX
            << ", " << rawY << ")";
        UpdateStatus(oss.str());
    }

    bool CanSave() const {
        for (int i = 0; i < kLayerCount; ++i) {
            if (imagePoints_[i].size() != kPointsPerLayer || mapPoints_[i].size() != kPointsPerLayer) {
                return false;
            }
        }
        return true;
    }

private:
    CalibrationArgs args_;
    cv::VideoCapture capture_;
    radar26::DahengCamera dahengCamera_;

    cv::Mat leftRaw_;
    cv::Mat mapRaw_;

    cv::Mat leftDisplay_;
    cv::Mat rightDisplay_;

    std::array<std::vector<cv::Point2f>, kLayerCount> imagePoints_;
    std::array<std::vector<cv::Point2f>, kLayerCount> mapPoints_;
    std::vector<UiButton> buttons_;

    int currentLayer_ = 0;
    bool capturing_ = true;
    bool exitRequested_ = false;
    bool hasSaved_ = false;
    int frameReadFailCount_ = 0;
    std::string lastSavedPath_;
    std::string statusText_ = "ready";

#if RADAR26_WITH_OPENCV_FREETYPE
    cv::Ptr<cv::freetype::FreeType2> chineseFont_;
#endif
    bool chineseFontReady_ = false;
    std::string chineseFontPath_;

    double leftScaleX_ = 1.0;
    double leftScaleY_ = 1.0;
    double rightScaleX_ = 1.0;
    double rightScaleY_ = 1.0;
};

}  // namespace

int main(int argc, char** argv) {
    // load app config first (allow --config path)
    std::string configPath;
    bool userSpecifiedConfig = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
            userSpecifiedConfig = true;
            continue;
        }
    }
    if (!userSpecifiedConfig) configPath = "../config/app.yaml";

    radar26::AppConfig appCfg;
    std::string loadErr;
    if (!radar26::ConfigLoader::LoadAppConfig(configPath, &appCfg, &loadErr)) {
        std::cerr << "LoadAppConfig failed: " << loadErr << std::endl;
        return 1;
    }

    // initialize CalibrationArgs from AppConfig, then allow CLI overrides via ParseArgs
    CalibrationArgs args;
    args.mode = SourceMode::Daheng;
    if (appCfg.camera.mode == "video_file") args.mode = SourceMode::VideoFile;
    else if (appCfg.camera.mode == "test") args.mode = SourceMode::Test;

    args.team = (appCfg.team == radar26::Team::Red) ? "R" : "B";
    args.videoPath = appCfg.camera.videoPath;
    args.testImagePath = "";
    // keep map following team default (red/blue), unless user passes --map explicitly
    args.mapPath.clear();
    args.dahengIndex = appCfg.camera.dahengDeviceIndex;
    args.dahengWidth = appCfg.camera.width;
    args.dahengHeight = appCfg.camera.height;
    args.dahengExposureUs = appCfg.camera.exposureTime;
    args.dahengGain = appCfg.camera.gain;
    args.dahengAutoWhiteBalance = appCfg.camera.dahengAutoWhiteBalance;
    args.dahengFlipVertical = appCfg.camera.dahengFlipVertical;

    // default outputs: use calibration paths from appCfg for YAML, keep npy default logic
    args.outputYamlPath = (args.team == "R") ? appCfg.calibrationRedPath : appCfg.calibrationBluePath;

    std::string error;
    if (!ParseArgs(argc, argv, &args, &error)) {
        std::cerr << "Argument parse failed: " << error << std::endl;
        PrintHelp();
        return 1;
    }

    std::cout << "标定工具参数:" << std::endl;
    const char* modeName = "daheng";
    if (args.mode == SourceMode::Test) {
        modeName = "test";
    } else if (args.mode == SourceMode::Daheng) {
        modeName = "daheng";
    } else if (args.mode == SourceMode::VideoFile) {
        modeName = "video_file";
    }
    std::cout << "  模式       : " << modeName << std::endl;
    std::cout << "  阵营       : " << args.team << std::endl;
    std::cout << "  地图       : " << args.mapPath << std::endl;
    std::cout << "  输出 npy   : " << args.outputNpyPath << std::endl;
    std::cout << "  输出 yaml  : " << args.outputYamlPath << std::endl;
    std::cout << "  相机配置   : mode=daheng"
              << " index=" << args.dahengIndex
              << " size=" << args.dahengWidth << "x" << args.dahengHeight
              << " exposure=" << args.dahengExposureUs
              << " gain=" << args.dahengGain
              << " awb=" << (args.dahengAutoWhiteBalance ? 1 : 0)
              << " flip=" << (args.dahengFlipVertical ? 1 : 0) << std::endl;
    if (args.mode == SourceMode::VideoFile) {
        std::cout << "  视频       : " << args.videoPath << std::endl;
    } else if (args.mode == SourceMode::Test) {
        std::cout << "  测试图像   : " << args.testImagePath << std::endl;
    } else if (args.mode == SourceMode::Daheng) {
        std::cout << "  大恒索引   : " << args.dahengIndex << std::endl;
        std::cout << "  分辨率     : " << args.dahengWidth << "x" << args.dahengHeight << std::endl;
        std::cout << "  曝光       : " << args.dahengExposureUs << " us" << std::endl;
        std::cout << "  增益       : " << args.dahengGain << std::endl;
        std::cout << "  自动白平衡 : " << (args.dahengAutoWhiteBalance ? 1 : 0) << std::endl;
        std::cout << "  垂直翻转   : " << (args.dahengFlipVertical ? 1 : 0) << std::endl;
    }

    CalibrationTool tool(args);
    if (!tool.Initialize(&error)) {
        std::cerr << "标定初始化失败: " << error << std::endl;
        return 1;
    }

    return tool.Run();
}

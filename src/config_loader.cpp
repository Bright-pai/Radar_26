/**
 * @file config_loader.cpp
 * @brief YAML 配置文件加载器
 *
 * 负责读取应用程序的 YAML 配置文件（app.yaml 及标定文件），
 * 解析并装配为 AppConfig 和 CalibrationData 结构体。
 *
 * 主要功能：
 *   1. LoadAppConfig()  - 加载主配置文件 app.yaml
 *      包含：队伍颜色、调试开关、相机参数、串口参数、TCP参数、
 *            模型路径、检测阈值、标定文件路径、日志路径等
 *   2. LoadCalibration() - 根据队伍颜色加载标定数据
 *      包含：单应矩阵（M_ground, M_height_r, M_height_g）、
 *            地图图像、掩码图像
 *
 * 路径处理：
 *   - 配置文件中的相对路径会自动转换为基于配置文件所在目录的绝对路径
 *   - 绝对路径保持原样
 *
 * 依赖：OpenCV FileStorage（YAML 解析）、std::filesystem（路径处理）
 */

#include "config_loader.hpp"

#include <opencv2/core/persistence.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace radar26 {
namespace {

// ============================================================================
// 匿名命名空间内的辅助函数（仅本文件可见）
// ============================================================================

/**
 * @brief 将配置文件中的相对路径转换为基于配置目录的绝对路径。
 *
 * 处理逻辑：
 *   - 空路径：直接返回空字符串
 *   - 绝对路径：保持原样，仅做规范化处理（如消除 ".." 和 "."）
 *   - 相对路径：以 baseDir 为基准拼接后做规范化处理
 *
 * 目的：避免因工作目录变化导致找不到配置文件指定的资源文件。
 *
 * @param baseDir 配置文件所在目录的路径（作为相对路径的基准目录）
 * @param rawPath 配置文件中的原始路径字符串
 * @return 规范化后的绝对路径字符串；如果 rawPath 为空则返回空字符串
 */
std::string ResolvePath(const std::filesystem::path& baseDir, const std::string& rawPath) {
    if (rawPath.empty()) {
        return rawPath;
    }
    const std::filesystem::path p(rawPath);
    if (p.is_absolute()) {
        return std::filesystem::absolute(p).string();
    }
    return std::filesystem::absolute(baseDir / p).string();
}

/**
 * @brief 将配置中的 team 字符串解析为 Team 枚举值。
 *
 * 解析规则（大小写不敏感）：
 *   - "b" 或 "blue"   -> Team::Blue（蓝方）
 *   - "r" 或 "red"    -> Team::Red（红方）
 *   - 其他任何值      -> Team::Red（默认红方）
 *
 * @param value 配置文件中 team 字段的原始字符串值
 * @return Team::Red 或 Team::Blue
 */
Team ParseTeam(const std::string& value) {
    // 转换为小写以实现大小写不敏感的比较
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower == "b" || lower == "blue") {
        return Team::Blue;
    }
    // 默认红方（包括 "r", "red" 或其他任何无法识别的值）
    return Team::Red;
}

/**
 * @brief 从 OpenCV YAML 节点中读取序列（列表）并转换为字符串列表。
 *
 * 用于解析 YAML 中的序列类型字段，如模型类别名称列表。
 *
 * @param node OpenCV FileNode，预期为序列类型（cv::FileNode::SEQ）
 * @param out  输出参数，指向 std::vector<std::string>，成功时被清空并填充
 * @return true  读取成功
 * @return false node 不是序列类型（node.isSeq() 返回 false）
 *
 * 副作用：
 * - 成功时清空 out 指向的 vector 并重新填充
 */
bool ReadStringList(const cv::FileNode& node, std::vector<std::string>* out) {
    if (!node.isSeq()) {
        return false;
    }
    out->clear();
    for (const auto& item : node) {
        out->push_back(static_cast<std::string>(item));
    }
    return true;
}

/**
 * @brief 从已打开的 FileStorage 中读取一个 3x3 矩阵并进行合法性校验。
 *
 * 校验内容：
 *   - key 对应的节点存在且非空
 *   - 矩阵可以成功读取
 *   - 矩阵尺寸为 3x3
 *   读取成功后自动将数据类型转换为 CV_32F（32 位浮点数）。
 *
 * @param fs    已打开的 cv::FileStorage 对象
 * @param key   要读取的 YAML 键名
 * @param mat   输出参数，指向 cv::Mat，成功时被填充为 3x3 CV_32F 矩阵
 * @param error 输出参数，失败时写入错误描述字符串（可为 nullptr）
 * @return true  读取成功，mat 指向有效的 3x3 CV_32F 矩阵
 * @return false 读取失败或校验不通过，error 中写入原因
 *
 * 副作用：
 * - 成功时通过 mat->convertTo(*mat, CV_32F) 原地转换数据类型
 */
bool ReadMatOrFail(const cv::FileStorage& fs, const std::string& key, cv::Mat* mat, std::string* error) {
    cv::FileNode n = fs[key];
    if (n.empty()) {
        if (error != nullptr) {
            *error = "missing matrix key: " + key;
        }
        return false;
    }

    // OpenCV 自动将 YAML 矩阵数据反序列化到 cv::Mat
    n >> *mat;

    // 校验矩阵尺寸必须为 3x3
    if (mat->empty() || mat->rows != 3 || mat->cols != 3) {
        if (error != nullptr) {
            *error = "invalid matrix shape for key: " + key;
        }
        return false;
    }

    // 确保数据类型为 32 位浮点数（无论 YAML 中存储的是什么数值类型）
    mat->convertTo(*mat, CV_32F);
    return true;
}

}  // namespace

// ============================================================================
// LoadAppConfig - 加载主应用配置
// ============================================================================

/**
 * @brief 从 app.yaml 文件中加载完整的应用配置。
 *
 * 读取的配置项包括：
 *   - team: 队伍颜色（红/蓝方）
 *   - debug: 调试模式开关
 *   - debug_request_key_update / debug_request_enemy_key: 调试用按键请求
 *   - show_ui: 是否显示 UI 窗口
 *   - opencv_threads: OpenCV 并行线程数
 *   - guess_switch_interval_sec: 猜测切换间隔（秒）
 *
 *   相机配置 (camera):
 *   - mode: 相机模式（"daheng" / "video_file" / "test"）
 *   - video_path, snapshot_path: 视频文件和截图路径
 *   - device_id, daheng_device_index: 设备 ID
 *   - width, height: 分辨率
 *   - exposure_time, gain: 曝光时间和增益
 *   - daheng_auto_white_balance, daheng_flip_vertical: 大恒相机特有选项
 *
 *   串口配置 (serial):
 *   - enable: 是否启用串口
 *   - port: 串口设备路径
 *   - baudrate: 波特率
 *   - send_period_ms: 发送周期（毫秒）
 *
 *   TCP 配置 (tcp, 可选):
 *   - enable: 是否启用 TCP
 *   - ip: 目标 IP
 *   - port: 目标端口
 *
 *   模型配置 (model):
 *   - car_engine, armor_engine: TensorRT 引擎文件路径
 *   - input_width, input_height: 模型输入尺寸
 *   - car_class_names, armor_class_names: 类别名称列表
 *
 *   检测配置 (detection):
 *   - car_conf, car_iou, car_max_det: 车辆检测参数
 *   - armor_conf, armor_iou, armor_max_det: 装甲板检测参数
 *   - filter_timeout_sec: 滤波器超时
 *
 *   标定配置 (calibration):
 *   - red_path, blue_path: 红方/蓝方标定文件路径
 *   - map_image: 地图图像路径
 *   - red_mask_image, blue_mask_image: 红方/蓝方掩码图像路径
 *
 *   日志配置 (log, 可选):
 *   - referee_path: 裁判系统日志路径
 *
 * 所有路径在读取后都会通过 ResolvePath() 转换为绝对路径。
 * 关键路径（模型、标定文件等）在最后通过 validateNonEmpty 校验非空。
 *
 * @param path   app.yaml 文件的路径
 * @param config 输出参数，指向 AppConfig 结构体，成功时被填充
 * @param error  输出参数，失败时写入错误描述字符串（可为 nullptr）
 * @return true  加载成功
 * @return false 加载失败（文件打不开、缺少必要字段、格式错误等），error 中写入原因
 *
 * 副作用：
 * - 填充 config 指向的 AppConfig 结构体的所有字段
 * - 通过 ResolvePath() 将所有相对路径转换为绝对路径
 */
bool ConfigLoader::LoadAppConfig(const std::string& path, AppConfig* config, std::string* error) {
    // 前置校验：输出指针不能为空
    if (config == nullptr) {
        if (error != nullptr) {
            *error = "config output pointer is null";
        }
        return false;
    }

    // 打开 YAML 配置文件
    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        if (error != nullptr) {
            *error = "failed to open app config: " + path;
        }
        return false;
    }

    // --- 读取顶层基本配置 ---
    config->team = ParseTeam(static_cast<std::string>(fs["team"]));
    config->debug = static_cast<int>(fs["debug"]) != 0;
    config->debugRequestKeyUpdate = static_cast<int>(fs["debug_request_key_update"]) != 0;
    config->debugRequestEnemyKey = static_cast<int>(fs["debug_request_enemy_key"]) != 0;
    config->showUi = static_cast<int>(fs["show_ui"]) != 0;

    // opencv_threads 为可选字段，未配置时使用 OpenCV 默认值
    if (!fs["opencv_threads"].empty()) {
        config->opencvThreads = static_cast<int>(fs["opencv_threads"]);
    }

    // --- 读取相机子配置 ---
    cv::FileNode camera = fs["camera"];
    config->camera.mode = static_cast<std::string>(camera["mode"]);
    if (config->camera.mode.empty()) {
        config->camera.mode = "daheng";  // 默认使用大恒相机
    }
    // 校验相机模式值的合法性
    if (config->camera.mode != "daheng" && config->camera.mode != "video_file" &&
        config->camera.mode != "test") {
        if (error != nullptr) {
            *error = "camera.mode must be one of: daheng, video_file, test";
        }
        return false;
    }

    config->camera.videoPath = static_cast<std::string>(camera["video_path"]);
    config->camera.snapshotPath = static_cast<std::string>(camera["snapshot_path"]);
    config->camera.deviceId = static_cast<int>(camera["device_id"]);

    // daheng_device_index 为可选字段
    if (!camera["daheng_device_index"].empty()) {
        config->camera.dahengDeviceIndex = static_cast<int>(camera["daheng_device_index"]);
    }

    config->camera.width = static_cast<int>(camera["width"]);
    config->camera.height = static_cast<int>(camera["height"]);
    config->camera.exposureTime = static_cast<double>(camera["exposure_time"]);
    config->camera.gain = static_cast<double>(camera["gain"]);

    // 大恒相机特有选项（可选字段）
    if (!camera["daheng_auto_white_balance"].empty()) {
        config->camera.dahengAutoWhiteBalance = static_cast<int>(camera["daheng_auto_white_balance"]) != 0;
    }
    if (!camera["daheng_flip_vertical"].empty()) {
        config->camera.dahengFlipVertical = static_cast<int>(camera["daheng_flip_vertical"]) != 0;
    }

    // --- 读取串口子配置 ---
    cv::FileNode serial = fs["serial"];
    config->serial.enable = static_cast<int>(serial["enable"]) != 0;
    config->serial.port = static_cast<std::string>(serial["port"]);
    config->serial.baudrate = static_cast<int>(serial["baudrate"]);
    config->serial.sendPeriodMs = static_cast<int>(serial["send_period_ms"]);

    // --- 读取 TCP 子配置（可选） ---
    cv::FileNode tcp = fs["tcp"];
    if (!tcp.empty()) {
        config->tcp.enable = static_cast<int>(tcp["enable"]) != 0;
        config->tcp.ip = static_cast<std::string>(tcp["ip"]);
        config->tcp.port = static_cast<int>(tcp["port"]);
    }

    // --- 读取模型子配置 ---
    cv::FileNode model = fs["model"];
    config->model.carEnginePath = static_cast<std::string>(model["car_engine"]);
    config->model.armorEnginePath = static_cast<std::string>(model["armor_engine"]);
    config->model.inputWidth = static_cast<int>(model["input_width"]);
    config->model.inputHeight = static_cast<int>(model["input_height"]);

    // 类别名称列表必须为 YAML 序列类型
    if (!ReadStringList(model["car_class_names"], &config->model.carClassNames)) {
        if (error != nullptr) {
            *error = "model.car_class_names must be a sequence";
        }
        return false;
    }
    if (!ReadStringList(model["armor_class_names"], &config->model.armorClassNames)) {
        if (error != nullptr) {
            *error = "model.armor_class_names must be a sequence";
        }
        return false;
    }

    // --- 读取检测子配置 ---
    cv::FileNode detection = fs["detection"];
    config->detection.carConf = static_cast<float>(detection["car_conf"]);
    config->detection.carIou = static_cast<float>(detection["car_iou"]);
    config->detection.carMaxDet = static_cast<int>(detection["car_max_det"]);
    config->detection.armorConf = static_cast<float>(detection["armor_conf"]);
    config->detection.armorIou = static_cast<float>(detection["armor_iou"]);
    config->detection.armorMaxDet = static_cast<int>(detection["armor_max_det"]);
    config->detection.filterTimeoutSec = static_cast<double>(detection["filter_timeout_sec"]);

    // --- 读取标定子配置 ---
    cv::FileNode calibration = fs["calibration"];
    config->calibrationRedPath = static_cast<std::string>(calibration["red_path"]);
    config->calibrationBluePath = static_cast<std::string>(calibration["blue_path"]);
    config->calibrationMapImagePath = static_cast<std::string>(calibration["map_image"]);
    config->calibrationRedMaskPath = static_cast<std::string>(calibration["red_mask_image"]);
    config->calibrationBlueMaskPath = static_cast<std::string>(calibration["blue_mask_image"]);

    // --- 读取日志子配置（可选） ---
    cv::FileNode log = fs["log"];
    if (!log.empty()) {
        config->refereeLogPath = static_cast<std::string>(log["referee_path"]);
    }

    // --- 路径解析：将相对路径转换为绝对路径 ---
    // 基准目录 = 配置文件所在目录
    const std::filesystem::path configDir = std::filesystem::path(path).parent_path();

    config->camera.videoPath = ResolvePath(configDir, config->camera.videoPath);
    config->camera.snapshotPath = ResolvePath(configDir, config->camera.snapshotPath);
    config->model.carEnginePath = ResolvePath(configDir, config->model.carEnginePath);
    config->model.armorEnginePath = ResolvePath(configDir, config->model.armorEnginePath);
    config->calibrationRedPath = ResolvePath(configDir, config->calibrationRedPath);
    config->calibrationBluePath = ResolvePath(configDir, config->calibrationBluePath);
    config->calibrationMapImagePath = ResolvePath(configDir, config->calibrationMapImagePath);
    config->calibrationRedMaskPath = ResolvePath(configDir, config->calibrationRedMaskPath);
    config->calibrationBlueMaskPath = ResolvePath(configDir, config->calibrationBlueMaskPath);
    config->refereeLogPath = ResolvePath(configDir, config->refereeLogPath);

    // --- 必填字段校验 ---
    // 标注模型和标定文件等关键路径不能为空
    auto validateNonEmpty = [&](const std::string& label, const std::string& value) {
        if (value.empty()) {
            if (error != nullptr) {
                *error = "missing required field: " + label;
            }
            return false;
        }
        return true;
    };

    if (!validateNonEmpty("model.car_engine", config->model.carEnginePath) ||
        !validateNonEmpty("model.armor_engine", config->model.armorEnginePath) ||
        !validateNonEmpty("calibration.red_path", config->calibrationRedPath) ||
        !validateNonEmpty("calibration.blue_path", config->calibrationBluePath) ||
        !validateNonEmpty("calibration.map_image", config->calibrationMapImagePath) ||
        !validateNonEmpty("calibration.red_mask_image", config->calibrationRedMaskPath) ||
        !validateNonEmpty("calibration.blue_mask_image", config->calibrationBlueMaskPath)) {
        return false;
    }

    return true;
}

// ============================================================================
// LoadCalibration - 加载标定数据
// ============================================================================

/**
 * @brief 根据队伍颜色（Team）加载对应的标定数据和地图资源。
 *
 * 根据 AppConfig 中的 team 字段自动选择：
 *   - 红方：使用 calibrationRedPath 和 calibrationRedMaskPath
 *   - 蓝方：使用 calibrationBluePath 和 calibrationBlueMaskPath
 *
 * 从标定 YAML 文件中读取以下 3x3 单应矩阵：
 *   - M_ground  : 图像平面到地平面的单应矩阵
 *   - M_height_r: 图像平面到高度参考平面（红色通道）的单应矩阵
 *   - M_height_g: 图像平面到高度参考平面（绿色通道）的单应矩阵
 *
 * 从图像文件加载：
 *   - mapImage : 比赛地图图像（用于可视化）
 *   - maskImage: 掩码图像（用于区域过滤，如限定检测范围）
 *
 * @param config      已加载的应用配置（用于获取队伍颜色和文件路径）
 * @param calibration 输出参数，指向 CalibrationData 结构体，成功时被填充
 * @param error       输出参数，失败时写入错误描述字符串（可为 nullptr）
 * @return true  加载成功
 * @return false 加载失败（文件打不开、矩阵格式错误、图像加载失败等），error 中写入原因
 *
 * 副作用：
 * - 填充 calibration 指向的 CalibrationData 结构体的所有字段：
 *   MGround, MHeightR, MHeightG（3x3 CV_32F 矩阵）
 *   mapImage, maskImage（cv::Mat 图像）
 *   maskWidth, maskHeight（掩码图像尺寸 - 1，用于坐标边界检查）
 */
bool ConfigLoader::LoadCalibration(const AppConfig& config, CalibrationData* calibration, std::string* error) {
    // 前置校验：输出指针不能为空
    if (calibration == nullptr) {
        if (error != nullptr) {
            *error = "calibration output pointer is null";
        }
        return false;
    }

    // 根据队伍颜色选择对应的标定文件路径
    const std::string calibrationPath =
        (config.team == Team::Red) ? config.calibrationRedPath : config.calibrationBluePath;

    // 打开标定 YAML 文件
    cv::FileStorage fs(calibrationPath, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        if (error != nullptr) {
            *error = "failed to open calibration file: " + calibrationPath;
        }
        return false;
    }

    // 读取三个 3x3 单应矩阵（任一个读取失败则整体失败）
    if (!ReadMatOrFail(fs, "M_ground", &calibration->MGround, error) ||
        !ReadMatOrFail(fs, "M_height_r", &calibration->MHeightR, error) ||
        !ReadMatOrFail(fs, "M_height_g", &calibration->MHeightG, error)) {
        return false;
    }

    // 地图图像：双方共用同一张
    const std::string mapImagePath = config.calibrationMapImagePath;

    // 掩码图像：红蓝方各用各的
    const std::string maskImagePath =
        (config.team == Team::Red) ? config.calibrationRedMaskPath : config.calibrationBlueMaskPath;

    // 使用 OpenCV 加载图像（彩色模式）
    calibration->mapImage = cv::imread(mapImagePath, cv::IMREAD_COLOR);
    calibration->maskImage = cv::imread(maskImagePath, cv::IMREAD_COLOR);

    // 校验图像是否加载成功
    if (calibration->mapImage.empty()) {
        if (error != nullptr) {
            *error = "failed to load map image: " + mapImagePath;
        }
        return false;
    }
    if (calibration->maskImage.empty()) {
        if (error != nullptr) {
            *error = "failed to load mask image: " + maskImagePath;
        }
        return false;
    }

    // 掩码尺寸减 1 作为坐标边界上限（0-based 索引的最大有效值）
    calibration->maskWidth = calibration->maskImage.cols - 1;
    calibration->maskHeight = calibration->maskImage.rows - 1;
    return true;
}

}  // namespace radar26

#include "config_loader.hpp"

#include <opencv2/core/persistence.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace radar26 {
namespace {

std::string ResolvePath(const std::filesystem::path& baseDir, const std::string& rawPath) {
    if (rawPath.empty()) {
        return rawPath;
    }
    const std::filesystem::path p(rawPath);
    if (p.is_absolute()) {
        return p.lexically_normal().string();
    }
    return (baseDir / p).lexically_normal().string();
}

Team ParseTeam(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == "b" || lower == "blue") {
        return Team::Blue;
    }
    return Team::Red;
}

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

bool ReadMatOrFail(const cv::FileStorage& fs, const std::string& key, cv::Mat* mat, std::string* error) {
    cv::FileNode n = fs[key];
    if (n.empty()) {
        if (error != nullptr) {
            *error = "missing matrix key: " + key;
        }
        return false;
    }
    n >> *mat;
    if (mat->empty() || mat->rows != 3 || mat->cols != 3) {
        if (error != nullptr) {
            *error = "invalid matrix shape for key: " + key;
        }
        return false;
    }
    mat->convertTo(*mat, CV_32F);
    return true;
}

}  // namespace

bool ConfigLoader::LoadAppConfig(const std::string& path, AppConfig* config, std::string* error) {
    if (config == nullptr) {
        if (error != nullptr) {
            *error = "config output pointer is null";
        }
        return false;
    }

    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        if (error != nullptr) {
            *error = "failed to open app config: " + path;
        }
        return false;
    }

    config->team = ParseTeam(static_cast<std::string>(fs["team"]));
    config->debug = static_cast<int>(fs["debug"]) != 0;
    config->debugRequestKeyUpdate = static_cast<int>(fs["debug_request_key_update"]) != 0;
    config->debugRequestEnemyKey = static_cast<int>(fs["debug_request_enemy_key"]) != 0;
    config->showUi = static_cast<int>(fs["show_ui"]) != 0;
    if (!fs["opencv_threads"].empty()) {
        config->opencvThreads = static_cast<int>(fs["opencv_threads"]);
    }
    config->guessSwitchIntervalSec = static_cast<double>(fs["guess_switch_interval_sec"]);

    cv::FileNode camera = fs["camera"];
    config->camera.mode = static_cast<std::string>(camera["mode"]);
    if (config->camera.mode.empty()) {
        config->camera.mode = "daheng";
    }
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
    if (!camera["daheng_device_index"].empty()) {
        config->camera.dahengDeviceIndex = static_cast<int>(camera["daheng_device_index"]);
    }
    config->camera.width = static_cast<int>(camera["width"]);
    config->camera.height = static_cast<int>(camera["height"]);
    config->camera.exposureTime = static_cast<double>(camera["exposure_time"]);
    config->camera.gain = static_cast<double>(camera["gain"]);
    if (!camera["daheng_auto_white_balance"].empty()) {
        config->camera.dahengAutoWhiteBalance = static_cast<int>(camera["daheng_auto_white_balance"]) != 0;
    }
    if (!camera["daheng_flip_vertical"].empty()) {
        config->camera.dahengFlipVertical = static_cast<int>(camera["daheng_flip_vertical"]) != 0;
    }

    cv::FileNode serial = fs["serial"];
    config->serial.enable = static_cast<int>(serial["enable"]) != 0;
    config->serial.port = static_cast<std::string>(serial["port"]);
    config->serial.baudrate = static_cast<int>(serial["baudrate"]);
    config->serial.sendPeriodMs = static_cast<int>(serial["send_period_ms"]);

    cv::FileNode model = fs["model"];
    config->model.carEnginePath = static_cast<std::string>(model["car_engine"]);
    config->model.armorEnginePath = static_cast<std::string>(model["armor_engine"]);
    config->model.inputWidth = static_cast<int>(model["input_width"]);
    config->model.inputHeight = static_cast<int>(model["input_height"]);
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

    cv::FileNode detection = fs["detection"];
    config->detection.carConf = static_cast<float>(detection["car_conf"]);
    config->detection.carIou = static_cast<float>(detection["car_iou"]);
    config->detection.carMaxDet = static_cast<int>(detection["car_max_det"]);
    config->detection.armorConf = static_cast<float>(detection["armor_conf"]);
    config->detection.armorIou = static_cast<float>(detection["armor_iou"]);
    config->detection.armorMaxDet = static_cast<int>(detection["armor_max_det"]);
    config->detection.filterTimeoutSec = static_cast<double>(detection["filter_timeout_sec"]);


    cv::FileNode calibration = fs["calibration"];
    config->calibrationRedPath = static_cast<std::string>(calibration["red_path"]);
    config->calibrationBluePath = static_cast<std::string>(calibration["blue_path"]);
    config->calibrationMapImagePath = static_cast<std::string>(calibration["map_image"]);
    config->calibrationRedMaskPath = static_cast<std::string>(calibration["red_mask_image"]);
    config->calibrationBlueMaskPath = static_cast<std::string>(calibration["blue_mask_image"]);

    cv::FileNode log = fs["log"];
    if (!log.empty()) {
        config->refereeLogPath = static_cast<std::string>(log["referee_path"]);
    }

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

bool ConfigLoader::LoadCalibration(const AppConfig& config, CalibrationData* calibration, std::string* error) {
    if (calibration == nullptr) {
        if (error != nullptr) {
            *error = "calibration output pointer is null";
        }
        return false;
    }

    const std::string calibrationPath =
        (config.team == Team::Red) ? config.calibrationRedPath : config.calibrationBluePath;

    cv::FileStorage fs(calibrationPath, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        if (error != nullptr) {
            *error = "failed to open calibration file: " + calibrationPath;
        }
        return false;
    }

    if (!ReadMatOrFail(fs, "M_ground", &calibration->MGround, error) ||
        !ReadMatOrFail(fs, "M_height_r", &calibration->MHeightR, error) ||
        !ReadMatOrFail(fs, "M_height_g", &calibration->MHeightG, error)) {
        return false;
    }

    const std::string mapImagePath = config.calibrationMapImagePath;
    const std::string maskImagePath =
        (config.team == Team::Red) ? config.calibrationRedMaskPath : config.calibrationBlueMaskPath;

    calibration->mapImage = cv::imread(mapImagePath, cv::IMREAD_COLOR);
    calibration->maskImage = cv::imread(maskImagePath, cv::IMREAD_COLOR);

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

    calibration->maskWidth = calibration->maskImage.cols - 1;
    calibration->maskHeight = calibration->maskImage.rows - 1;
    return true;
}

}  // namespace radar26

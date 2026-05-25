#pragma once

#include "radar_types.hpp"

#include <string>

namespace radar26 {

// 配置加载器，负责把 YAML 文件映射成工程内部结构体。
class ConfigLoader {
public:
    // 读取总配置 app.yaml。
    static bool LoadAppConfig(const std::string& path, AppConfig* config, std::string* error);
    // 读取阵营对应的标定文件和地图资源。
    static bool LoadCalibration(const AppConfig& config, CalibrationData* calibration, std::string* error);
};

}  // namespace radar26

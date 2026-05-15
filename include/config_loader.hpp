#pragma once

#include "radar_types.hpp"

#include <string>

namespace radar26 {

class ConfigLoader {
public:
    static bool LoadAppConfig(const std::string& path, AppConfig* config, std::string* error);
    static bool LoadCalibration(const AppConfig& config, CalibrationData* calibration, std::string* error);
};

}  // namespace radar26

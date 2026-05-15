#include "config_loader.hpp"
#include "radar_app.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string configPath;
    bool userSpecifiedConfig = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
            userSpecifiedConfig = true;
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: Radar_26 [--config path/to/app.yaml]" << std::endl;
            return 0;
        }
    }

    if (!userSpecifiedConfig) {
        configPath = "../config/app.yaml";
    }

    radar26::AppConfig config;
    std::string error;
    if (!radar26::ConfigLoader::LoadAppConfig(configPath, &config, &error)) {
        std::cerr << "LoadAppConfig failed: " << error << std::endl;
        return 1;
    }

    radar26::RadarApp app(config);
    if (!app.Initialize(&error)) {
        std::cerr << "RadarApp initialize failed: " << error << std::endl;
        return 1;
    }

    app.Run();
    return 0;
}

/**
 * @file main.cpp
 * @brief 雷达系统程序入口
 *
 * 本文件是 Radar_26 雷达应用程序的启动入口。主要负责以下工作流程：
 *   1. 解析命令行参数，确定配置文件路径
 *   2. 加载 YAML 格式的配置文件，解析为 AppConfig 结构体
 *   3. 使用配置初始化 RadarApp 实例（包括摄像头、串口、检测器、跟踪器等模块）
 *   4. 进入主循环 Run()，持续接收雷达数据并进行目标检测与跟踪
 *
 * 命令行用法：
 *   Radar_26 [--config path/to/app.yaml]
 *   Radar_26 -h | --help     // 输出帮助信息
 *
 * 如果未通过 --config 指定配置文件路径，默认使用 "../config/app.yaml"。
 *
 * 返回值规范：
 *   0 - 程序正常退出
 *   1 - 配置加载失败或 RadarApp 初始化失败
 */

#include "config_loader.hpp"
#include "radar_app.hpp"

#include <iostream>
#include <string>

/**
 * @brief 程序主入口函数
 *
 * 执行流程：
 *   1. 遍历命令行参数，支持以下选项：
 *      - --config <path> : 指定 YAML 配置文件的路径
 *      - -h / --help     : 打印使用说明并退出
 *   2. 若未指定配置文件，则使用默认路径 "../config/app.yaml"
 *   3. 调用 ConfigLoader::LoadAppConfig() 解析配置文件，得到 AppConfig 结构体
 *   4. 构造 RadarApp 对象，传入已加载的配置
 *   5. 调用 RadarApp::Initialize() 完成各子模块的初始化
 *      （包括摄像头驱动、串口通信、TensorRT 检测器、目标跟踪滤波器等）
 *   6. 调用 RadarApp::Run() 进入主循环，持续运行雷达采集与处理流程
 *
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组，argv[0] 为程序名称
 * @return int  0 表示正常退出，1 表示配置加载或初始化阶段发生错误
 */
int main(int argc, char** argv) {
    // 配置文件路径，可通过 --config 命令行参数覆盖
    std::string configPath;
    // 标记用户是否通过命令行显式指定了配置文件
    bool userSpecifiedConfig = false;

    // 遍历命令行参数，解析 --config 和 --help 选项
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        // --config 选项：读取紧随其后的参数作为配置文件路径
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
            userSpecifiedConfig = true;
            continue;
        }
        // -h / --help 选项：打印使用说明后正常退出
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: Radar_26 [--config path/to/app.yaml]" << std::endl;
            return 0;
        }
    }

    // 未通过命令行指定配置文件时，使用工程内置的默认配置路径
    if (!userSpecifiedConfig) {
        configPath = "../config/app.yaml";
    }

    // 从 YAML 文件加载应用配置，填充 AppConfig 结构体
    radar26::AppConfig config;
    std::string error;
    if (!radar26::ConfigLoader::LoadAppConfig(configPath, &config, &error)) {
        std::cerr << "LoadAppConfig failed: " << error << std::endl;
        return 1;
    }

    // 使用已加载的配置构造 RadarApp 实例，并初始化各子模块
    radar26::RadarApp app(config);
    if (!app.Initialize(&error)) {
        std::cerr << "RadarApp initialize failed: " << error << std::endl;
        return 1;
    }

    // 进入主运行循环：持续采集雷达数据、执行目标检测与跟踪
    app.Run();
    return 0;
}

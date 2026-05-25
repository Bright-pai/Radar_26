#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <map>

namespace radar26 {

// 目标滤波器负责把检测结果按目标名做缓存和超时失效处理。
class TargetFilter {
public:
    static constexpr int kMaxTargets = 12;
    // 每个槽位保存一个目标的最新坐标、有效标记和更新时间。
    struct Slot {
        std::atomic<float> x{0.0f};
        std::atomic<float> y{0.0f};
        std::atomic<bool> valid{false};
        std::atomic<long long> lastUpdateMs{0};
    };

    // windowSize 预留给平滑逻辑使用，maxInactiveSeconds 控制多久后判定为失效。
    TargetFilter(std::size_t windowSize, double maxInactiveSeconds);
    // 按机器人名称写入一组最新坐标。
    void AddData(const std::string& name, float x, float y);
    // 取出当前仍然有效的所有目标坐标。
    std::map<std::string, cv::Point2f> GetAllData();

private:
    std::size_t windowSize_;
    std::chrono::duration<double> maxInactive_;
    std::array<Slot, kMaxTargets> slots_;
    std::map<std::string, int> nameToSlot_;
};

}  // namespace radar26
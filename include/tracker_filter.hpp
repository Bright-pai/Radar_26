#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <map>

namespace radar26 {

class TargetFilter {
public:
    static constexpr int kMaxTargets = 12;
    struct Slot {
        std::atomic<float> x{0.0f};
        std::atomic<float> y{0.0f};
        std::atomic<bool> valid{false};
        std::atomic<long long> lastUpdateMs{0};
    };

    TargetFilter(std::size_t windowSize, double maxInactiveSeconds);
    void AddData(const std::string& name, float x, float y);
    std::map<std::string, cv::Point2f> GetAllData();

private:
    std::size_t windowSize_;
    std::chrono::duration<double> maxInactive_;
    std::array<Slot, kMaxTargets> slots_;
    std::map<std::string, int> nameToSlot_;
};

}  // namespace radar26
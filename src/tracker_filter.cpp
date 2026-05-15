#include "tracker_filter.hpp"

#include <vector>

namespace radar26 {

TargetFilter::TargetFilter(std::size_t windowSize, double maxInactiveSeconds)
    : windowSize_(windowSize), maxInactive_(maxInactiveSeconds) {
    const std::vector<std::string> names = {
        "R1", "R2", "R3", "R4", "R6", "R7",
        "B1", "B2", "B3", "B4", "B6", "B7"
    };
    for (std::size_t i = 0; i < names.size(); ++i)
        nameToSlot_[names[i]] = static_cast<int>(i);
}

void TargetFilter::AddData(const std::string& name, float x, float y) {
    auto it = nameToSlot_.find(name);
    if (it == nameToSlot_.end()) return;
    int idx = it->second;
    slots_[idx].x.store(x, std::memory_order_relaxed);
    slots_[idx].y.store(y, std::memory_order_relaxed);
    slots_[idx].valid.store(true, std::memory_order_release);
    const auto now = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    slots_[idx].lastUpdateMs.store(static_cast<long long>(ms), std::memory_order_release);
}

std::map<std::string, cv::Point2f> TargetFilter::GetAllData() {
    std::map<std::string, cv::Point2f> result;
    const auto now = std::chrono::steady_clock::now();
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    for (const auto& [name, idx] : nameToSlot_) {
        const long long lu = slots_[idx].lastUpdateMs.load(std::memory_order_acquire);
        const double ageSec = static_cast<double>(nowMs - lu) / 1000.0;
        if (lu > 0 && ageSec <= maxInactive_.count() && slots_[idx].valid.load(std::memory_order_acquire)) {
            float x = slots_[idx].x.load(std::memory_order_relaxed);
            float y = slots_[idx].y.load(std::memory_order_relaxed);
            result[name] = cv::Point2f(x, y);
        } else {
            slots_[idx].valid.store(false, std::memory_order_relaxed);
        }
    }
    return result;
}

}  // namespace radar26
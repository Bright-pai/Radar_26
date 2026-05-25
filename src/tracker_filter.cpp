// tracker_filter.cpp —— 目标位置滤波器实现。
// 本文件实现了一个基于时间失效机制的简单目标缓存器，用于管理多个机器人
// 的实时坐标。每个机器人对应一个固定槽位（Slot），定期从检测模块接收
// 最新坐标，在查询时自动剔除超出有效期（maxInactiveSeconds）的过时数据。
//
// 设计要点：
//   1. 使用 std::atomic 保证多线程读写安全，无需额外加锁；
//   2. 每个槽位的 x、y、valid、lastUpdateMs 均为原子变量，支持松弛/
//      释放/获取内存序以控制可见性；
//   3. 超时判断在 GetAllData() 中执行，而非后台线程，保证查询时的数据
//      一致性和即时性；
//   4. 当前仅缓存最新坐标，不做卡尔曼滤波或滑动平均等平滑处理。

#include "tracker_filter.hpp"

#include <vector>

namespace radar26 {

// ============================================================================
// TargetFilter::TargetFilter —— 构造函数，初始化目标槽位和名称映射表。
//
// 创建 12 个预定义机器人的名称到槽位索引的映射：
//   R1~R4, R6, R7  → 红方机器人（slot 0~5）
//   B1~B4, B6, B7  → 蓝方机器人（slot 6~11）
// 注意：R5 和 B5 不在列表中（裁判系统协议中哨兵/空中机器人使用其他编号）。
//
// 参数：
//   windowSize        - 预留的滑动窗口大小，供未来可能引入的平滑逻辑使用
//                       （当前未使用，仅保存到成员变量）
//   maxInactiveSeconds - 最大不活跃时间（秒）。当某个目标超过此时间未被
//                        更新时，GetAllData() 将其标记为失效并排除出结果集
// 返回：无（构造函数）
// 副作用：
//   - 初始化 nameToSlot_ 映射表（12 个条目）
//   - 所有 slot_ 槽位保持默认原子值（x=0, y=0, valid=false, lastUpdateMs=0）
// ============================================================================
TargetFilter::TargetFilter(std::size_t windowSize, double maxInactiveSeconds)
    : windowSize_(windowSize), maxInactive_(maxInactiveSeconds) {
    // 预定义的 12 个机器人名称列表
    // R1-R4, R6-R7: 红方机器人；B1-B4, B6-B7: 蓝方机器人
    const std::vector<std::string> names = {
        "R1", "R2", "R3", "R4", "R6", "R7",
        "B1", "B2", "B3", "B4", "B6", "B7"
    };
    // 建立名称到槽位索引的映射，后续 AddData/GetAllData 通过名称快速定位槽位
    for (std::size_t i = 0; i < names.size(); ++i)
        nameToSlot_[names[i]] = static_cast<int>(i);
}

// ============================================================================
// TargetFilter::AddData —— 将一帧检测到的目标坐标写入对应机器人的槽位。
//
// 处理流程：
//   1. 在 nameToSlot_ 映射表中查找机器人名称；
//   2. 如果名称不在预定义列表中（无效目标），直接返回（静默忽略）；
//   3. 更新对应槽位的 x、y 坐标（relaxed 内存序，因为单个值的写入不需要同步）；
//   4. 设置 valid 标记为 true（release 内存序，确保坐标写入先于 valid 标记可见）；
//   5. 记录当前时间戳作为 lastUpdateMs（release 内存序，供 GetAllData 做超时判断）。
//
// 参数：
//   name - 机器人名称（如 "R1", "B3"），必须是构造函数中预定义的 12 个名称之一
//   x    - 目标在图像/地图坐标系中的 X 坐标（像素或映射后的坐标）
//   y    - 目标在图像/地图坐标系中的 Y 坐标（像素或映射后的坐标）
//
// 返回：无
// 副作用：
//   - 修改对应 slot_ 槽位的原子变量值（x, y, valid, lastUpdateMs）
//   - 如果 name 不在预定义列表中，无任何副作用（静默忽略）
//   - 线程安全：使用原子操作，可从任意线程调用
// ============================================================================
void TargetFilter::AddData(const std::string& name, float x, float y) {
    // 查找名称对应的槽位索引
    auto it = nameToSlot_.find(name);
    if (it == nameToSlot_.end()) return;  // 无效名称，静默忽略
    int idx = it->second;

    // 写入坐标（relaxed：不需要与其他原子变量建立 happen-before 关系，
    // 因为 valid 和 lastUpdateMs 的 release/acquire 语义会保证整体可见性）
    slots_[idx].x.store(x, std::memory_order_relaxed);
    slots_[idx].y.store(y, std::memory_order_relaxed);

    // 设置有效标记（release：确保 x、y 的写入在此标记之前对后续 acquire 可见）
    slots_[idx].valid.store(true, std::memory_order_release);

    // 记录更新时间戳（毫秒级，自 steady_clock 纪元起）
    // 使用 steady_clock 保证单调递增，不受系统时间调整影响
    const auto now = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    slots_[idx].lastUpdateMs.store(static_cast<long long>(ms), std::memory_order_release);
}

// ============================================================================
// TargetFilter::GetAllData —— 获取所有当前仍然有效的目标坐标。
//
// 处理流程：
//   1. 获取当前时间戳（毫秒）；
//   2. 遍历所有 12 个预定义槽位；
//   3. 对每个槽位：
//      a. 读取 lastUpdateMs（acquire 内存序，确保读取到最新的更新时间）；
//      b. 计算距离上次更新的时间差（秒）；
//      c. 如果 lastUpdateMs > 0（曾被写入过）且 timeSinceUpdate <= maxInactive_（未超时）
//         且 valid 为 true，则将该目标加入结果集；
//      d. 否则，将 valid 标记为 false（relaxed，标记失效），不加入结果集。
//
// 参数：无
//
// 返回：std::map<std::string, cv::Point2f>
//   key   - 机器人名称（如 "R1", "B3"）
//   value - 目标在图像/地图坐标系中的 (x, y) 坐标
//   注意：过时或从未被更新的目标不会出现在返回的 map 中。
//
// 副作用：
//   - 将超时槽位的 valid 标记设为 false（防止下一次查询仍然读到过期数据）
//   - 线程安全：使用原子 load 读取，可从任意线程调用
// ============================================================================
std::map<std::string, cv::Point2f> TargetFilter::GetAllData() {
    std::map<std::string, cv::Point2f> result;

    // 获取当前时间戳（毫秒），用于判断每个槽位是否超时
    const auto now = std::chrono::steady_clock::now();
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // 遍历所有预定义的 12 个机器人槽位
    for (const auto& [name, idx] : nameToSlot_) {
        // acquire 内存序：确保读取到 AddData 中 release 写入的最新 lastUpdateMs
        const long long lu = slots_[idx].lastUpdateMs.load(std::memory_order_acquire);

        // 计算自上次更新以来的时间差（秒）
        const double ageSec = static_cast<double>(nowMs - lu) / 1000.0;

        // 有效性判断条件：
        //   lu > 0            — 至少被 AddData 写入过一次
        //   ageSec <= maxInactive_ — 未超过最大不活跃时长
        //   valid == true      — 标记为有效
        if (lu > 0 && ageSec <= maxInactive_.count() && slots_[idx].valid.load(std::memory_order_acquire)) {
            // 读取坐标（relaxed：此时已通过 acquire 获得足够的内存可见性保证）
            float x = slots_[idx].x.load(std::memory_order_relaxed);
            float y = slots_[idx].y.load(std::memory_order_relaxed);
            result[name] = cv::Point2f(x, y);
        } else {
            // 超时或无效：将 valid 标记为 false，后续查询不再试图读取该槽位
            // relaxed：不需要强同步语义，仅标记失效
            slots_[idx].valid.store(false, std::memory_order_relaxed);
        }
    }
    return result;
}

}  // namespace radar26

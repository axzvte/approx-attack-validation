#pragma once

#include "analysis/brute_force_search.hpp"
#include "applications/dct.hpp"

#include <opencv2/core.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>


namespace analysis
{

// 一个已经确定“节点 + monitor input”的区间搜索目标。
//
// 注意：这里不包含 approximate unit。
// 模块 1 只描述“有哪些合法触发区间”，
// 不评价某个 unit 在区间中的攻击效果。
struct DctIntervalSearchTarget
{
    int nodeId = -1;

    core::MonitorInput monitorInput =
        core::MonitorInput::Input1;
};


// 一张图片上，一个节点对应的完整区间搜索空间。
struct DctIntervalSearchSpace
{
    int nodeId = -1;

    core::MonitorInput monitorInput =
        core::MonitorInput::Input1;


    // 当前节点在正常 5RP Baseline 下的动态调用次数。
    std::size_t sampleCount = 0;


    // 当前 monitor input 在这张图片上真正出现过的不同整数值。
    //
    // 已按从小到大排序且去重。
    std::vector<int> observedValues;


    // 使用 observedValues 中的真实取值作为区间边界，
    // 枚举全部连续闭区间 [lower, upper]。
    //
    // 这些区间对应不同的 Baseline 动态触发集合，
    // 因此不会保留仅因为边界落在“没有样本的空档”
    // 而造成的重复触发行为。
    std::vector<TriggerInterval> intervals;
};


class DctIntervalSearchSpaceGenerator
{
public:

    // n 个不同 monitor value 能形成：
    //
    // n * (n + 1) / 2
    //
    // 个非空连续闭区间。
    static std::uint64_t countIntervals(
        std::size_t distinctValueCount
    );


    // 从任意一组观测值直接生成完整区间集合。
    //
    // 输入会先排序、去重。
    // 该接口主要用于测试和后续模块复用。
    static std::vector<TriggerInterval>
    buildIntervals(
        const std::vector<int>& observedValues
    );


    // 模块 1：
    //
    // 1. 在正常 5RP Baseline 下采集节点动态输入；
    // 2. 对每个已经确定 monitor input 的目标节点，
    //    提取该输入在当前图片上的真实取值；
    // 3. 排序、去重；
    // 4. 使用真实取值作为上下界，生成全部不重复的
    //    区间搜索空间。
    //
    // 本模块不做：
    // - ROI / Non-ROI 效果筛选；
    // - approximate unit 选择；
    // - PSNR 评价；
    // - “最佳区间”选择。
    //
    // 这些都留给后续区间优化模块。
    static std::vector<DctIntervalSearchSpace>
    generate(
        const applications::DctApplication& application,
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const std::vector<DctIntervalSearchTarget>& targets
    );
};

}

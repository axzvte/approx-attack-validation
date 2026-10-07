#pragma once

#include "analysis/brute_force_search.hpp"
#include "applications/dct.hpp"
#include "core/add_sample.hpp"

#include <opencv2/core.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>


namespace analysis
{

// 一个已经确定“节点 + monitor signal”的区间搜索目标。
//
// 不包含 approximate unit。
// 模块 1 只负责描述当前真实运行状态下“有哪些合法区间”，
// 不负责评价区间好坏。
struct DctIntervalSearchTarget
{
    int nodeId = -1;

    core::MonitorSignal monitorInput =
        core::MonitorSignal::Input1;
};


// 一张图片、一次实际电路运行状态下，
// 一个节点对应的完整区间搜索空间。
struct DctIntervalSearchSpace
{
    int nodeId = -1;

    core::MonitorSignal monitorInput =
        core::MonitorSignal::Input1;


    // 当前实际运行状态下，该节点的动态调用次数。
    std::size_t sampleCount = 0;


    // 当前 monitor signal 在这次实际运行中真正出现过的不同整数值。
    // 已按从小到大排序并去重。
    std::vector<int> observedValues;


    // 使用 observedValues 中的真实取值作为边界，
    // 枚举全部连续闭区间 [lower, upper]。
    //
    // 对当前动态样本而言，这避免了仅在“无样本空档”
    // 移动边界而形成的重复触发行为。
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
    // 输入会先排序、去重。
    static std::vector<TriggerInterval>
    buildIntervals(
        const std::vector<int>& observedValues
    );


    // 模块 1 的核心接口。
    //
    // 输入是“一次真实电路运行已经采集到的动态样本”。
    // 样本可以来自：
    // - Baseline；
    // - 单节点攻击；
    // - 多节点攻击。
    //
    // 因此后续 Module 2 每次改变当前攻击配置后，
    // 都可以重新采样并复用同一套区间生成逻辑。
    static std::vector<DctIntervalSearchSpace>
    generateFromSamples(
        const std::vector<core::AddSample>& samples,
        const std::vector<DctIntervalSearchTarget>& targets
    );


    // 根据“当前完整攻击配置”真实运行 DCT，
    // 然后使用实际动态输入生成区间搜索空间。
    //
    // 这是后续多节点区间优化应使用的接口。
    static std::vector<DctIntervalSearchSpace>
    generateForCurrentConfiguration(
        const applications::DctApplication& application,
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const std::vector<core::AttackConfig>& currentConfiguration,
        const std::vector<DctIntervalSearchTarget>& targets
    );


    // Baseline 初始化接口。
    //
    // 空配置意味着全部节点使用正常 Baseline（当前默认 5RP）。
    // Baseline 只作为首次搜索的初始状态，
    // 不再被视为多节点搜索全过程唯一的区间来源。
    static std::vector<DctIntervalSearchSpace>
    generateBaseline(
        const applications::DctApplication& application,
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const std::vector<DctIntervalSearchTarget>& targets
    );


    // 保留旧接口，避免已有调用方失效。
    // 语义与 generateBaseline 完全相同。
    static std::vector<DctIntervalSearchSpace>
    generate(
        const applications::DctApplication& application,
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const std::vector<DctIntervalSearchTarget>& targets
    );
};

}

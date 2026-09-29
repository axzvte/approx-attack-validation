#pragma once

#include "applications/dct.hpp"
#include "core/attack_config.hpp"

#include <opencv2/core.hpp>

#include <cstddef>
#include <vector>


namespace analysis
{

enum class RegionBias
{
    Roi,
    NonRoi
};


struct DctBestIntervalResult
{
    int nodeId = -1;

    core::MonitorInput monitorInput =
        core::MonitorInput::Input1;

    RegionBias bias =
        RegionBias::Roi;

    std::size_t imageIndex = 0;

    int lower = 0;
    int upper = 0;

    double roiTriggerRate = 0.0;
    double nonRoiTriggerRate = 0.0;

    // ROI 偏向：
    //     gap = roiTriggerRate - nonRoiTriggerRate
    //
    // Non-ROI 偏向：
    //     gap = nonRoiTriggerRate - roiTriggerRate
    //
    // 始终取非负值，越大说明当前 monitor input
    // 越容易通过某个区间把两个区域区分开。
    double gap = 0.0;
};


struct DctMonitorInputSummary
{
    int nodeId = -1;

    core::MonitorInput monitorInput =
        core::MonitorInput::Input1;

    RegionBias bias =
        RegionBias::Roi;

    double meanGap = 0.0;
    double stdGap = 0.0;
    double cvGap = 0.0;

    double meanRoiTriggerRate = 0.0;
    double meanNonRoiTriggerRate = 0.0;
};


struct DctMonitorInputReport
{
    std::vector<DctBestIntervalResult>
        perImageResults;

    std::vector<DctMonitorInputSummary>
        summaries;
};


class DctMonitorInputAnalyzer
{
public:

    // 对候选节点的 input1 / input2 分别分析。
    //
    // 对每张图片、每个 monitor input：
    // 1. 使用 Baseline 5RP 的真实动态样本；
    // 2. 不分箱，直接使用真实整数值作为区间边界；
    // 3. 穷举所有有效连续区间；
    // 4. 分别寻找：
    //      ROI 触发率 - Non-ROI 触发率 最大的区间；
    //      Non-ROI 触发率 - ROI 触发率 最大的区间。
    //
    // 区间这里只用于评价 monitor input 的“区域区分能力”，
    // 不作为最终硬件区间固定下来。
    static DctMonitorInputReport analyze(
        const applications::DctApplication& application,
        const std::vector<cv::Mat>& inputImages,
        const std::vector<cv::Mat>& roiMasks,
        const std::vector<int>& candidateNodeIds
    );
};

}

#pragma once

#include "applications/dct.hpp"
#include "core/attack_config.hpp"

#include <opencv2/core.hpp>

#include <cstddef>
#include <vector>


namespace analysis
{

// 第一阶段监控信号分析与最终 AttackConfig 使用同一个类型。
// 这样实验结论可以直接进入后续静态硬件结构搜索。
using DctMonitorSignal =
    core::MonitorSignal;


enum class DctMonitorSignalBias
{
    Roi,
    NonRoi
};


struct DctMonitorSignalBestInterval
{
    int nodeId = -1;

    DctMonitorSignal signal =
        DctMonitorSignal::Input1;

    DctMonitorSignalBias bias =
        DctMonitorSignalBias::Roi;

    std::size_t imageIndex = 0;

    int lower = 0;
    int upper = 0;

    double roiTriggerRate = 0.0;
    double nonRoiTriggerRate = 0.0;

    // ROI bias:
    //   roiTriggerRate - nonRoiTriggerRate
    //
    // Non-ROI bias:
    //   nonRoiTriggerRate - roiTriggerRate
    //
    // 始终保存非负值。
    double gap = 0.0;
};


struct DctMonitorSignalSummary
{
    int nodeId = -1;

    DctMonitorSignal signal =
        DctMonitorSignal::Input1;

    DctMonitorSignalBias bias =
        DctMonitorSignalBias::Roi;

    double meanGap = 0.0;
    double stdGap = 0.0;
    double cvGap = 0.0;

    double meanRoiTriggerRate = 0.0;
    double meanNonRoiTriggerRate = 0.0;
};


struct DctMonitorSignalReport
{
    std::vector<DctMonitorSignalBestInterval>
        perImageResults;

    std::vector<DctMonitorSignalSummary>
        summaries;
};


class DctMonitorSignalAnalyzer
{
public:

    // 对候选节点公平比较：
    //
    // 1. input1
    // 2. input2
    // 3. Baseline output
    //
    // 三种信号完全使用相同的：
    // - 10 张 Stage 1 图片；
    // - ROI 权重；
    // - 实际出现整数值作为区间边界；
    // - 全部连续区间枚举；
    // - ROI / Non-ROI trigger-rate gap。
    //
    // 因而结果只反映“监控信号自身的区域区分能力”，
    // 不混入 attack unit 或最终 DCT PSNR。
    static DctMonitorSignalReport analyze(
        const applications::DctApplication& application,
        const std::vector<cv::Mat>& inputImages,
        const std::vector<cv::Mat>& roiMasks,
        const std::vector<int>& candidateNodeIds
    );
};

}

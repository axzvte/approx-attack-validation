#pragma once

#include "applications/dct.hpp"

#include <opencv2/core.hpp>

#include <cstddef>
#include <vector>


namespace analysis
{

// 仅用于第一阶段“监控信号选择”实验。
//
// BaselineOutput 指当前正常 5RP 路径在 MUX 之前的输出。
// 它不是最终 MUX 输出，因此不会形成组合反馈。
enum class DctMonitorSignal
{
    Input1,
    Input2,
    BaselineOutput
};


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
    // 3. Baseline 5RP output
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

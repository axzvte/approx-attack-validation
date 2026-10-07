#pragma once

#include "analysis/brute_force_search.hpp"
#include "applications/dct.hpp"

#include <opencv2/core.hpp>

#include <vector>


namespace analysis
{

struct DctImageQualityMetrics
{
    double globalPsnr = 0.0;
    double roiPsnr = 0.0;
    double nonRoiPsnr = 0.0;
};


struct DctIntervalFullValidationResult
{
    TriggerInterval interval;

    DctImageQualityMetrics metrics;


    // 相对于 currentConfiguration 的变化。
    //
    // PSNR delta < 0 : 质量下降
    // PSNR delta > 0 : 质量改善
    double globalPsnrDelta = 0.0;
    double roiPsnrDelta = 0.0;
    double nonRoiPsnrDelta = 0.0;
};


struct DctIntervalFullValidationReport
{
    // 当前完整配置（尚未替换目标节点区间）的真实图像质量。
    //
    // 第一轮 currentConfiguration 为空时，
    // 这里就是正常 5RP Baseline。
    DctImageQualityMetrics currentMetrics;


    std::vector<DctIntervalFullValidationResult>
        candidates;
};


class DctIntervalFullValidator
{
public:

    // 对任意当前配置计算统一的完整图像指标。
    //
    // 参考始终为精确 DCT 重建结果。
    static DctImageQualityMetrics evaluateConfiguration(
        const applications::DctApplication& application,
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const std::vector<core::AttackConfig>& configuration
    );


    // Module 2-B：
    //
    // 对 Module 2-A 留下的少量区间真正运行完整 DCT。
    //
    // currentConfiguration 表示其它节点以及目标节点当前正在使用的配置。
    // 对每个 candidate interval：
    // 1. 复制 currentConfiguration；
    // 2. 将目标 nodeId 的配置替换为
    //      nodeId + monitorInput + attackUnit + candidate interval；
    // 3. 运行完整近似 DCT；
    // 4. 统一相对“精确 DCT 重建结果”计算
    //      Global / ROI / Non-ROI PSNR；
    // 5. 同时给出相对 currentConfiguration 的 PSNR 变化。
    //
    // 因此同一接口既能用于：
    // - 第一轮 Baseline 单节点验证；
    // - 后续多节点逐节点区间优化。
    static DctIntervalFullValidationReport validate(
        const applications::DctApplication& application,
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const std::vector<core::AttackConfig>& currentConfiguration,
        int nodeId,
        core::MonitorInput monitorInput,
        approximate::ApproxUnitId attackUnit,
        const std::vector<TriggerInterval>& candidateIntervals
    );
};

}

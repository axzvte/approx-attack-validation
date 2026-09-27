#pragma once

#include "analysis/two_stage_search.hpp"

#include <cstddef>
#include <functional>
#include <vector>


namespace analysis
{

enum class MetricDirection
{
    // 指标越大，质量越好。
    // 例如 PSNR。
    HigherIsBetter,

    // 指标越小，质量越好。
    // 例如 MED。
    LowerIsBetter
};


struct Stage1ImageMetrics
{
    // 当前配置在一张图上的全图指标。
    double globalMetric = 0.0;

    // 当前配置在一张图上的重要区域指标。
    double roiMetric = 0.0;
};


using Stage1Evaluator =
    std::function<
        Stage1ImageMetrics(
            const AttackConfiguration& configuration,
            std::size_t imageIndex,
            const ImageCase& imageCase
        )
    >;


struct Stage1ScreeningOptions
{
    // “全图指标正常”的阈值。
    //
    // HigherIsBetter:
    //     meanGlobalMetric >= threshold
    //
    // LowerIsBetter:
    //     meanGlobalMetric <= threshold
    double globalMetricThreshold = 0.0;


    MetricDirection globalMetricDirection =
        MetricDirection::HigherIsBetter;


    // 重要区域指标的质量方向。
    //
    // HigherIsBetter:
    //     ROI 指标越低，攻击越严重。
    //
    // LowerIsBetter:
    //     ROI 指标越高，攻击越严重。
    MetricDirection roiMetricDirection =
        MetricDirection::HigherIsBetter;


    // 全图指标正常的配置中，
    // 保留重要区域破坏最严重的前多少比例。
    double keepWorstFraction = 0.20;
};


struct Stage1SelectedCandidate
{
    AttackConfiguration
        configuration;


    // 10 张 Stage 1 图像上的平均全图指标。
    double meanGlobalMetric = 0.0;


    // 10 张 Stage 1 图像上的平均重要区域指标。
    double meanRoiMetric = 0.0;


    // 额外记录最差一张图，便于后续分析。
    double worstGlobalMetric = 0.0;

    double worstRoiMetric = 0.0;
};


class Stage1Screening
{
public:

    // Stage 1 筛选流程：
    //
    // 1. 枚举所有完整配置
    // 2. 每个配置都跑完 Stage 1 的 10 张图
    // 3. 汇总 10 张图的全图指标和重要区域指标
    // 4. 先过滤“平均全图指标不正常”的配置
    // 5. 在剩余配置中按重要区域破坏程度排序
    // 6. 保留最差的前 keepWorstFraction（默认 20%）
    //
    // 注意：
    // 这里保留的是完整配置，
    // 不按攻击节点数量分组，也不先按硬件结构去重。
    static std::vector<Stage1SelectedCandidate>
    screen(
        const TwoStageDataset& dataset,
        const BruteForceSearchSpace& searchSpace,
        const Stage1Evaluator& evaluator,
        const Stage1ScreeningOptions& options
    );
};

}

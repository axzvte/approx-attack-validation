#include "analysis/stage1_screening.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>


namespace analysis
{

namespace
{

bool globalMetricIsNormal(
    double value,
    double threshold,
    MetricDirection direction
)
{
    if (
        direction
        ==
        MetricDirection::HigherIsBetter
    )
    {
        return
            value
            >=
            threshold;
    }


    return
        value
        <=
        threshold;
}


bool roiDamageIsWorse(
    const Stage1SelectedCandidate& first,
    const Stage1SelectedCandidate& second,
    MetricDirection direction
)
{
    // 如果指标越大越好（例如 PSNR），
    // 那么 ROI 指标越小，说明攻击越严重。
    if (
        direction
        ==
        MetricDirection::HigherIsBetter
    )
    {
        if (
            first.meanRoiMetric
            !=
            second.meanRoiMetric
        )
        {
            return
                first.meanRoiMetric
                <
                second.meanRoiMetric;
        }


        // 平均值相同时，
        // 最差一张图越低越靠前。
        return
            first.worstRoiMetric
            <
            second.worstRoiMetric;
    }


    // 如果指标越小越好（例如 MED），
    // 那么 ROI 指标越大，说明攻击越严重。
    if (
        first.meanRoiMetric
        !=
        second.meanRoiMetric
    )
    {
        return
            first.meanRoiMetric
            >
            second.meanRoiMetric;
    }


    return
        first.worstRoiMetric
        >
        second.worstRoiMetric;
}

}


// =========================================================
// Stage 1 筛选
// =========================================================

std::vector<Stage1SelectedCandidate>
Stage1Screening::screen(
    const TwoStageDataset& dataset,
    const BruteForceSearchSpace& searchSpace,
    const Stage1Evaluator& evaluator,
    const Stage1ScreeningOptions& options
)
{
    TwoStageSearch::validateDataset(
        dataset
    );


    if (!evaluator)
    {
        throw std::runtime_error(
            "Stage 1 evaluator is empty."
        );
    }


    if (
        !std::isfinite(
            options.globalMetricThreshold
        )
    )
    {
        throw std::runtime_error(
            "Global metric threshold must be finite."
        );
    }


    if (
        options.keepWorstFraction
        <=
        0.0
        ||
        options.keepWorstFraction
        >
        1.0
    )
    {
        throw std::runtime_error(
            "keepWorstFraction must be in (0, 1]."
        );
    }


    std::vector<Stage1SelectedCandidate>
        globallyNormalCandidates;


    BruteForceSearch::enumerate(
        searchSpace,

        [&](
            const AttackConfiguration&
                configuration
        )
        {
            double globalMetricSum =
                0.0;


            double roiMetricSum =
                0.0;


            double worstGlobalMetric =
                (
                    options.globalMetricDirection
                    ==
                    MetricDirection::HigherIsBetter
                )
                ?
                std::numeric_limits<double>::
                    infinity()
                :
                -std::numeric_limits<double>::
                    infinity();


            double worstRoiMetric =
                (
                    options.roiMetricDirection
                    ==
                    MetricDirection::HigherIsBetter
                )
                ?
                std::numeric_limits<double>::
                    infinity()
                :
                -std::numeric_limits<double>::
                    infinity();


            // 关键：
            // 无论某一张图表现如何，
            // 当前配置都会把 10 张图全部跑完。
            for (
                std::size_t imageIndex = 0;
                imageIndex
                    <
                    dataset.stage1Images.size();
                ++imageIndex
            )
            {
                const Stage1ImageMetrics
                    metrics =
                        evaluator(
                            configuration,
                            imageIndex,
                            dataset.stage1Images[
                                imageIndex
                            ]
                        );


                if (
                    !std::isfinite(
                        metrics.globalMetric
                    )
                    ||
                    !std::isfinite(
                        metrics.roiMetric
                    )
                )
                {
                    throw std::runtime_error(
                        "Stage 1 evaluator returned a non-finite metric."
                    );
                }


                globalMetricSum +=
                    metrics.globalMetric;


                roiMetricSum +=
                    metrics.roiMetric;


                if (
                    options.globalMetricDirection
                    ==
                    MetricDirection::HigherIsBetter
                )
                {
                    worstGlobalMetric =
                        std::min(
                            worstGlobalMetric,
                            metrics.globalMetric
                        );
                }
                else
                {
                    worstGlobalMetric =
                        std::max(
                            worstGlobalMetric,
                            metrics.globalMetric
                        );
                }


                if (
                    options.roiMetricDirection
                    ==
                    MetricDirection::HigherIsBetter
                )
                {
                    worstRoiMetric =
                        std::min(
                            worstRoiMetric,
                            metrics.roiMetric
                        );
                }
                else
                {
                    worstRoiMetric =
                        std::max(
                            worstRoiMetric,
                            metrics.roiMetric
                        );
                }
            }


            const double imageCount =
                static_cast<double>(
                    dataset.stage1Images.size()
                );


            const double meanGlobalMetric =
                globalMetricSum
                /
                imageCount;


            const double meanRoiMetric =
                roiMetricSum
                /
                imageCount;


            // 第一层筛选：
            // 只看 10 张图汇总后的全图指标是否正常。
            if (
                !globalMetricIsNormal(
                    meanGlobalMetric,
                    options.globalMetricThreshold,
                    options.globalMetricDirection
                )
            )
            {
                return;
            }


            Stage1SelectedCandidate
                candidate;


            candidate.configuration =
                configuration;


            candidate.meanGlobalMetric =
                meanGlobalMetric;


            candidate.meanRoiMetric =
                meanRoiMetric;


            candidate.worstGlobalMetric =
                worstGlobalMetric;


            candidate.worstRoiMetric =
                worstRoiMetric;


            globallyNormalCandidates.push_back(
                std::move(
                    candidate
                )
            );
        }
    );


    if (
        globallyNormalCandidates.empty()
    )
    {
        return {};
    }


    // 第二层筛选：
    // 在全图指标正常的配置中，
    // 按重要区域破坏程度从强到弱排序。
    std::sort(
        globallyNormalCandidates.begin(),
        globallyNormalCandidates.end(),

        [&options](
            const Stage1SelectedCandidate& first,
            const Stage1SelectedCandidate& second
        )
        {
            return
                roiDamageIsWorse(
                    first,
                    second,
                    options.roiMetricDirection
                );
        }
    );


    // 前 20%。
    //
    // 使用 ceil，确保只要存在合格配置，
    // 至少保留 1 个。
    const std::size_t keepCount =
        std::max<std::size_t>(
            1,
            static_cast<std::size_t>(
                std::ceil(
                    static_cast<double>(
                        globallyNormalCandidates.size()
                    )
                    *
                    options.keepWorstFraction
                )
            )
        );


    globallyNormalCandidates.resize(
        std::min(
            keepCount,
            globallyNormalCandidates.size()
        )
    );


    return
        globallyNormalCandidates;
}

}

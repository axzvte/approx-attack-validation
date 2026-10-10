#include "analysis/dct_interval_fast_evaluator.hpp"

#include "analysis/dct_interval_search_space_generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>


namespace analysis
{

namespace
{

struct AggregatedValue
{
    double roiWeight = 0.0;
    double nonRoiWeight = 0.0;

    long double roiErrorChange = 0.0L;
    long double nonRoiErrorChange = 0.0L;
};


int monitoredValue(
    const core::AddSample& sample,
    core::MonitorSignal monitorSignal
)
{
    switch (monitorSignal)
    {
        case core::MonitorSignal::Input1:
            return sample.input1;

        case core::MonitorSignal::Input2:
            return sample.input2;

        case core::MonitorSignal::BaselineOutput:
            return sample.baselineOutput;
    }

    throw std::runtime_error(
        "Unknown DCT monitor signal."
    );
}


long double squared(
    long double value
)
{
    return
        value
        *
        value;
}


std::size_t weightedQuantileIndex(
    const std::vector<double>& weights,
    double totalWeight,
    double quantile
)
{
    if (
        weights.empty()
        ||
        totalWeight <= 0.0
    )
    {
        return 0;
    }


    if (quantile <= 0.0)
    {
        for (std::size_t index = 0;
             index < weights.size();
             ++index)
        {
            if (weights[index] > 0.0)
            {
                return index;
            }
        }


        return 0;
    }


    if (quantile >= 1.0)
    {
        for (std::size_t offset = 0;
             offset < weights.size();
             ++offset)
        {
            const std::size_t index =
                weights.size()
                -
                1
                -
                offset;


            if (weights[index] > 0.0)
            {
                return index;
            }
        }


        return
            weights.size()
            -
            1;
    }


    const double target =
        quantile
        *
        totalWeight;


    double cumulative =
        0.0;


    for (std::size_t index = 0;
         index < weights.size();
         ++index)
    {
        cumulative +=
            weights[index];


        if (cumulative >= target)
        {
            return index;
        }
    }


    return
        weights.size()
        -
        1;
}


void addQuantileBoundaries(
    const std::vector<double>& weights,
    double totalWeight,
    std::size_t quantileCount,
    std::set<std::size_t>& selected
)
{
    if (
        weights.empty()
        ||
        totalWeight <= 0.0
        ||
        quantileCount == 0
    )
    {
        return;
    }


    if (quantileCount == 1)
    {
        selected.insert(
            weightedQuantileIndex(
                weights,
                totalWeight,
                0.5
            )
        );

        return;
    }


    for (std::size_t index = 0;
         index < quantileCount;
         ++index)
    {
        const double quantile =
            static_cast<double>(
                index
            )
            /
            static_cast<double>(
                quantileCount
                -
                1
            );


        selected.insert(
            weightedQuantileIndex(
                weights,
                totalWeight,
                quantile
            )
        );
    }
}


std::vector<std::size_t>
selectCandidateBoundaryIndices(
    const std::vector<double>& roiWeights,
    const std::vector<double>& globalWeights,
    const std::vector<long double>& roiErrorContributions,
    double totalRoiWeight,
    double totalGlobalWeight,
    std::size_t maxCandidateBoundaryCount
)
{
    const std::size_t valueCount =
        roiWeights.size();


    if (
        maxCandidateBoundaryCount == 0
        ||
        valueCount
            <=
            maxCandidateBoundaryCount
    )
    {
        std::vector<std::size_t>
            result;


        result.reserve(
            valueCount
        );


        for (std::size_t index = 0;
             index < valueCount;
             ++index)
        {
            result.push_back(
                index
            );
        }


        return result;
    }


    const std::size_t boundaryBudget =
        std::min(
            valueCount,
            std::max<std::size_t>(
                2,
                maxCandidateBoundaryCount
            )
        );


    // 候选边界由三类当前图片信息共同产生：
    // 1. ROI 分布分位点：重点覆盖 ROI 高频取值范围；
    // 2. 整图分布分位点：避免完全丢失整体输入范围；
    // 3. ROI 局部误差贡献最大的取值：补充可能很窄但破坏强的区间。
    std::size_t roiQuantileCount =
        std::max<std::size_t>(
            2,
            boundaryBudget
            /
            2
        );


    std::size_t globalQuantileCount =
        std::max<std::size_t>(
            2,
            boundaryBudget
            /
            4
        );


    if (
        roiQuantileCount
        +
        globalQuantileCount
        >
        boundaryBudget
    )
    {
        globalQuantileCount =
            boundaryBudget
            -
            roiQuantileCount;
    }


    std::set<std::size_t>
        selected;


    addQuantileBoundaries(
        roiWeights,
        totalRoiWeight,
        roiQuantileCount,
        selected
    );


    addQuantileBoundaries(
        globalWeights,
        totalGlobalWeight,
        globalQuantileCount,
        selected
    );


    std::vector<std::size_t>
        impactOrder;


    impactOrder.reserve(
        valueCount
    );


    for (std::size_t index = 0;
         index < valueCount;
         ++index)
    {
        impactOrder.push_back(
            index
        );
    }


    std::sort(
        impactOrder.begin(),
        impactOrder.end(),

        [&roiErrorContributions](
            std::size_t first,
            std::size_t second
        )
        {
            if (
                roiErrorContributions[first]
                !=
                roiErrorContributions[second]
            )
            {
                return
                    roiErrorContributions[first]
                    >
                    roiErrorContributions[second];
            }


            return
                first
                <
                second;
        }
    );


    for (const auto index : impactOrder)
    {
        if (
            selected.size()
            >=
            boundaryBudget
        )
        {
            break;
        }


        selected.insert(
            index
        );
    }


    // 分位点和误差峰值可能高度重合。
    // 如果仍未填满预算，用等距索引补足，保证整个观测范围都有覆盖。
    if (
        selected.size()
        <
        boundaryBudget
    )
    {
        for (std::size_t slot = 0;
             slot < boundaryBudget;
             ++slot)
        {
            if (
                selected.size()
                >=
                boundaryBudget
            )
            {
                break;
            }


            const std::size_t index =
                boundaryBudget == 1
                ?
                0
                :
                slot
                *
                (
                    valueCount
                    -
                    1
                )
                /
                (
                    boundaryBudget
                    -
                    1
                );


            selected.insert(
                index
            );
        }
    }


    return
        std::vector<std::size_t>(
            selected.begin(),
            selected.end()
        );
}

}


// =========================================================
// Module 2-A：快速区间局部误差评价
// =========================================================

DctIntervalFastEvaluation
DctIntervalFastEvaluator::evaluateFromSamples(
    const std::vector<core::AddSample>& samples,
    int nodeId,
    core::MonitorSignal monitorInput,
    approximate::ApproxUnitId attackUnit,
    std::size_t maxCandidateBoundaryCount
)
{
    if (samples.empty())
    {
        throw std::runtime_error(
            "DCT interval fast evaluator received no samples."
        );
    }


    if (nodeId < 0)
    {
        throw std::runtime_error(
            "DCT interval fast evaluator received a negative node ID."
        );
    }


    std::map<int, AggregatedValue>
        aggregatedByMonitorValue;


    std::size_t targetSampleCount =
        0;


    double totalRoiWeight =
        0.0;


    double totalNonRoiWeight =
        0.0;


    for (const auto& sample : samples)
    {
        if (sample.nodeId != nodeId)
        {
            continue;
        }


        if (
            sample.roiWeight < 0.0
            ||
            sample.roiWeight > 1.0
        )
        {
            throw std::runtime_error(
                "DCT interval fast evaluator received an invalid ROI weight."
            );
        }


        const int monitorValue =
            monitoredValue(
                sample,
                monitorInput
            );


        // 使用 trace 中保存的原 Baseline 路径输出。
        // 其它上游节点造成的输入变化已经体现在这个值里。
        const int baselineOutput =
            sample.baselineOutput;


        const int attackedOutput =
            approximate::addSigned12(
                sample.input1,
                sample.input2,
                attackUnit
            );


        const long double exactOutput =
            static_cast<long double>(
                sample.input1
            )
            +
            static_cast<long double>(
                sample.input2
            );


        const long double baselineError =
            static_cast<long double>(
                baselineOutput
            )
            -
            exactOutput;


        const long double attackedError =
            static_cast<long double>(
                attackedOutput
            )
            -
            exactOutput;


        const long double localSquaredErrorChange =
            squared(
                attackedError
            )
            -
            squared(
                baselineError
            );


        const double roiWeight =
            sample.roiWeight;


        const double nonRoiWeight =
            1.0
            -
            roiWeight;


        auto& aggregated =
            aggregatedByMonitorValue[
                monitorValue
            ];


        aggregated.roiWeight +=
            roiWeight;


        aggregated.nonRoiWeight +=
            nonRoiWeight;


        aggregated.roiErrorChange +=
            static_cast<long double>(
                roiWeight
            )
            *
            localSquaredErrorChange;


        aggregated.nonRoiErrorChange +=
            static_cast<long double>(
                nonRoiWeight
            )
            *
            localSquaredErrorChange;


        totalRoiWeight +=
            roiWeight;


        totalNonRoiWeight +=
            nonRoiWeight;


        ++targetSampleCount;
    }


    if (targetSampleCount == 0)
    {
        throw std::runtime_error(
            "DCT interval fast evaluator found no samples for the target node."
        );
    }


    if (
        totalRoiWeight <= 0.0
        ||
        totalNonRoiWeight <= 0.0
    )
    {
        throw std::runtime_error(
            "DCT interval fast evaluator requires both ROI and Non-ROI weights."
        );
    }


    const std::size_t valueCount =
        aggregatedByMonitorValue.size();


    std::vector<int>
        values;


    values.reserve(
        valueCount
    );


    std::vector<double>
        roiWeightPrefix(
            valueCount + 1,
            0.0
        );


    std::vector<double>
        nonRoiWeightPrefix(
            valueCount + 1,
            0.0
        );


    std::vector<long double>
        roiErrorChangePrefix(
            valueCount + 1,
            0.0L
        );


    std::vector<long double>
        nonRoiErrorChangePrefix(
            valueCount + 1,
            0.0L
        );


    std::vector<double>
        roiWeightsByValue;


    roiWeightsByValue.reserve(
        valueCount
    );


    std::vector<double>
        globalWeightsByValue;


    globalWeightsByValue.reserve(
        valueCount
    );


    std::vector<long double>
        roiErrorContributionByValue;


    roiErrorContributionByValue.reserve(
        valueCount
    );


    std::size_t valueIndex =
        0;


    for (
        const auto& item :
        aggregatedByMonitorValue
    )
    {
        values.push_back(
            item.first
        );


        roiWeightPrefix[
            valueIndex + 1
        ] =
            roiWeightPrefix[
                valueIndex
            ]
            +
            item.second.roiWeight;


        nonRoiWeightPrefix[
            valueIndex + 1
        ] =
            nonRoiWeightPrefix[
                valueIndex
            ]
            +
            item.second.nonRoiWeight;


        roiErrorChangePrefix[
            valueIndex + 1
        ] =
            roiErrorChangePrefix[
                valueIndex
            ]
            +
            item.second.roiErrorChange;


        nonRoiErrorChangePrefix[
            valueIndex + 1
        ] =
            nonRoiErrorChangePrefix[
                valueIndex
            ]
            +
            item.second.nonRoiErrorChange;


        roiWeightsByValue.push_back(
            item.second.roiWeight
        );


        globalWeightsByValue.push_back(
            item.second.roiWeight
            +
            item.second.nonRoiWeight
        );


        roiErrorContributionByValue.push_back(
            item.second.roiErrorChange
        );


        ++valueIndex;
    }


    DctIntervalFastEvaluation
        result;


    result.nodeId =
        nodeId;


    result.monitorInput =
        monitorInput;


    result.attackUnit =
        attackUnit;


    result.sampleCount =
        targetSampleCount;


    result.distinctMonitorValueCount =
        valueCount;


    result.totalRoiWeight =
        totalRoiWeight;


    result.totalNonRoiWeight =
        totalNonRoiWeight;


    const auto candidateBoundaryIndices =
        selectCandidateBoundaryIndices(
            roiWeightsByValue,
            globalWeightsByValue,
            roiErrorContributionByValue,
            totalRoiWeight,
            totalRoiWeight
                +
                totalNonRoiWeight,
            maxCandidateBoundaryCount
        );


    result.candidateBoundaryValueCount =
        candidateBoundaryIndices.size();


    result.metrics.reserve(
        static_cast<std::size_t>(
            DctIntervalSearchSpaceGenerator::
                countIntervals(
                    candidateBoundaryIndices.size()
                )
        )
    );


    // 只在当前图片自动挑选出的候选边界之间组合连续闭区间。
    // 前缀和仍基于完整 observed values，因此每个候选区间的
    // ROI / Global 局部误差统计仍使用全部动态样本。
    for (std::size_t lowerPosition = 0;
         lowerPosition < candidateBoundaryIndices.size();
         ++lowerPosition)
    {
        const std::size_t lowerIndex =
            candidateBoundaryIndices[
                lowerPosition
            ];


        for (std::size_t upperPosition = lowerPosition;
             upperPosition < candidateBoundaryIndices.size();
             ++upperPosition)
        {
            const std::size_t upperIndex =
                candidateBoundaryIndices[
                    upperPosition
                ];
            const double roiTriggeredWeight =
                roiWeightPrefix[
                    upperIndex + 1
                ]
                -
                roiWeightPrefix[
                    lowerIndex
                ];


            const double nonRoiTriggeredWeight =
                nonRoiWeightPrefix[
                    upperIndex + 1
                ]
                -
                nonRoiWeightPrefix[
                    lowerIndex
                ];


            const long double roiDelta =
                roiErrorChangePrefix[
                    upperIndex + 1
                ]
                -
                roiErrorChangePrefix[
                    lowerIndex
                ];


            const long double nonRoiDelta =
                nonRoiErrorChangePrefix[
                    upperIndex + 1
                ]
                -
                nonRoiErrorChangePrefix[
                    lowerIndex
                ];


            DctIntervalFastMetric
                metric;


            metric.interval =
            {
                values[
                    lowerIndex
                ],
                values[
                    upperIndex
                ]
            };


            metric.roiTriggerRate =
                roiTriggeredWeight
                /
                totalRoiWeight;


            metric.nonRoiTriggerRate =
                nonRoiTriggeredWeight
                /
                totalNonRoiWeight;


            metric.roiErrorChange =
                static_cast<double>(
                    roiDelta
                    /
                    static_cast<long double>(
                        totalRoiWeight
                    )
                );


            metric.nonRoiErrorChange =
                static_cast<double>(
                    nonRoiDelta
                    /
                    static_cast<long double>(
                        totalNonRoiWeight
                    )
                );


            metric.redistributionScore =
                metric.roiErrorChange
                -
                metric.nonRoiErrorChange;


            result.metrics.push_back(
                metric
            );
        }
    }


    return result;
}


namespace
{

bool fastMetricComesFirst(
    const DctIntervalFastMetric& first,
    const DctIntervalFastMetric& second,
    DctIntervalFastRanking ranking
)
{
    if (
        ranking
        ==
        DctIntervalFastRanking::RoiAttack
    )
    {
        if (
            first.roiErrorChange
            !=
            second.roiErrorChange
        )
        {
            return
                first.roiErrorChange
                >
                second.roiErrorChange;
        }


        if (
            first.nonRoiErrorChange
            !=
            second.nonRoiErrorChange
        )
        {
            return
                first.nonRoiErrorChange
                <
                second.nonRoiErrorChange;
        }


        return
            first.redistributionScore
            >
            second.redistributionScore;
    }


    if (
        ranking
        ==
        DctIntervalFastRanking::NonRoiCompensation
    )
    {
        if (
            first.nonRoiErrorChange
            !=
            second.nonRoiErrorChange
        )
        {
            return
                first.nonRoiErrorChange
                <
                second.nonRoiErrorChange;
        }


        if (
            first.roiErrorChange
            !=
            second.roiErrorChange
        )
        {
            return
                first.roiErrorChange
                >
                second.roiErrorChange;
        }


        return
            first.redistributionScore
            >
            second.redistributionScore;
    }


    if (
        first.redistributionScore
        !=
        second.redistributionScore
    )
    {
        return
            first.redistributionScore
            >
            second.redistributionScore;
    }


    if (
        first.roiErrorChange
        !=
        second.roiErrorChange
    )
    {
        return
            first.roiErrorChange
            >
            second.roiErrorChange;
    }


    return
        first.nonRoiErrorChange
        <
        second.nonRoiErrorChange;
}

}


// =========================================================
// Module 2-A：三类候选区间排序
// =========================================================

std::vector<DctIntervalFastMetric>
DctIntervalFastEvaluator::selectTopMetrics(
    const DctIntervalFastEvaluation& evaluation,
    DctIntervalFastRanking ranking,
    std::size_t count
)
{
    if (
        count == 0
        ||
        evaluation.metrics.empty()
    )
    {
        return {};
    }


    std::vector<DctIntervalFastMetric>
        ranked =
            evaluation.metrics;


    std::sort(
        ranked.begin(),
        ranked.end(),

        [ranking](
            const DctIntervalFastMetric& first,
            const DctIntervalFastMetric& second
        )
        {
            return
                fastMetricComesFirst(
                    first,
                    second,
                    ranking
                );
        }
    );


    if (ranked.size() > count)
    {
        ranked.resize(
            count
        );
    }


    return ranked;
}


namespace
{

double globalErrorChange(
    const DctIntervalFastEvaluation& evaluation,
    const DctIntervalFastMetric& metric
)
{
    const double totalWeight =
        evaluation.totalRoiWeight
        +
        evaluation.totalNonRoiWeight;


    if (totalWeight <= 0.0)
    {
        throw std::runtime_error(
            "Fast interval evaluator has zero total weight."
        );
    }


    return
        (
            metric.roiErrorChange
            *
            evaluation.totalRoiWeight
            +
            metric.nonRoiErrorChange
            *
            evaluation.totalNonRoiWeight
        )
        /
        totalWeight;
}


std::vector<DctIntervalFastMetric>
buildParetoFront(
    const DctIntervalFastEvaluation& evaluation
)
{
    std::vector<DctIntervalFastMetric>
        front;


    front.reserve(
        evaluation.metrics.size()
    );


    // 三维 Pareto：
    // 1. ROIErrorChange 越大越好；
    // 2. GlobalErrorChange 越小越好；
    // 3. redistributionScore = ROIErrorChange - NonROIErrorChange 越大越好。
    //
    // 第 3 维用于优先保留“新增误差更倾向 ROI”的区间，
    // 但这里只是快速阶段，不做最终硬判定。
    for (std::size_t index = 0;
         index < evaluation.metrics.size();
         ++index)
    {
        const auto& candidate =
            evaluation.metrics[index];


        const double candidateGlobal =
            globalErrorChange(
                evaluation,
                candidate
            );


        bool dominated =
            false;


        for (std::size_t otherIndex = 0;
             otherIndex < evaluation.metrics.size();
             ++otherIndex)
        {
            if (index == otherIndex)
            {
                continue;
            }


            const auto& other =
                evaluation.metrics[otherIndex];


            const double otherGlobal =
                globalErrorChange(
                    evaluation,
                    other
                );


            const bool roiNoWorse =
                other.roiErrorChange
                >=
                candidate.roiErrorChange;


            const bool globalNoWorse =
                otherGlobal
                <=
                candidateGlobal;


            const bool redistributionNoWorse =
                other.redistributionScore
                >=
                candidate.redistributionScore;


            const bool strictlyBetter =
                other.roiErrorChange
                    >
                    candidate.roiErrorChange
                ||
                otherGlobal
                    <
                    candidateGlobal
                ||
                other.redistributionScore
                    >
                    candidate.redistributionScore;


            if (
                roiNoWorse
                &&
                globalNoWorse
                &&
                redistributionNoWorse
                &&
                strictlyBetter
            )
            {
                dominated =
                    true;

                break;
            }
        }


        if (!dominated)
        {
            front.push_back(
                candidate
            );
        }
    }


    std::sort(
        front.begin(),
        front.end(),

        [&evaluation](
            const DctIntervalFastMetric& first,
            const DctIntervalFastMetric& second
        )
        {
            if (
                first.roiErrorChange
                !=
                second.roiErrorChange
            )
            {
                return
                    first.roiErrorChange
                    >
                    second.roiErrorChange;
            }


            if (
                first.redistributionScore
                !=
                second.redistributionScore
            )
            {
                return
                    first.redistributionScore
                    >
                    second.redistributionScore;
            }


            return
                globalErrorChange(
                    evaluation,
                    first
                )
                <
                globalErrorChange(
                    evaluation,
                    second
                );
        }
    );


    return front;
}


double normalizedDistanceSquared(
    const DctIntervalFastEvaluation& evaluation,
    const DctIntervalFastMetric& first,
    const DctIntervalFastMetric& second,
    double minRoi,
    double maxRoi,
    double minGlobal,
    double maxGlobal,
    double minRedistribution,
    double maxRedistribution
)
{
    const double roiRange =
        maxRoi
        -
        minRoi;


    const double globalRange =
        maxGlobal
        -
        minGlobal;


    const double redistributionRange =
        maxRedistribution
        -
        minRedistribution;


    const double firstRoi =
        roiRange > 0.0
        ?
        (
            first.roiErrorChange
            -
            minRoi
        )
        /
        roiRange
        :
        0.0;


    const double secondRoi =
        roiRange > 0.0
        ?
        (
            second.roiErrorChange
            -
            minRoi
        )
        /
        roiRange
        :
        0.0;


    const double firstGlobal =
        globalRange > 0.0
        ?
        (
            globalErrorChange(
                evaluation,
                first
            )
            -
            minGlobal
        )
        /
        globalRange
        :
        0.0;


    const double secondGlobal =
        globalRange > 0.0
        ?
        (
            globalErrorChange(
                evaluation,
                second
            )
            -
            minGlobal
        )
        /
        globalRange
        :
        0.0;


    const double firstRedistribution =
        redistributionRange > 0.0
        ?
        (
            first.redistributionScore
            -
            minRedistribution
        )
        /
        redistributionRange
        :
        0.0;


    const double secondRedistribution =
        redistributionRange > 0.0
        ?
        (
            second.redistributionScore
            -
            minRedistribution
        )
        /
        redistributionRange
        :
        0.0;


    const double roiDifference =
        firstRoi
        -
        secondRoi;


    const double globalDifference =
        firstGlobal
        -
        secondGlobal;


    const double redistributionDifference =
        firstRedistribution
        -
        secondRedistribution;


    return
        roiDifference
        *
        roiDifference
        +
        globalDifference
        *
        globalDifference
        +
        redistributionDifference
        *
        redistributionDifference;
}


// =========================================================
// Module 3-A：非支配前沿 + 代表区间
// =========================================================

DctIntervalRepresentativeSelection
DctIntervalFastEvaluator::selectRepresentativeMetrics(
    const DctIntervalFastEvaluation& evaluation,
    std::size_t maxCount
)
{
    DctIntervalRepresentativeSelection
        result;


    result.totalMetricCount =
        evaluation.metrics.size();


    if (
        maxCount == 0
        ||
        evaluation.metrics.empty()
    )
    {
        return result;
    }


    const auto front =
        buildParetoFront(
            evaluation
        );


    result.paretoMetricCount =
        front.size();


    if (
        front.size()
        <=
        maxCount
    )
    {
        result.representatives =
            front;


        return result;
    }


    double minRoi =
        front.front().roiErrorChange;


    double maxRoi =
        front.front().roiErrorChange;


    double minGlobal =
        globalErrorChange(
            evaluation,
            front.front()
        );


    double maxGlobal =
        minGlobal;


    double minRedistribution =
        front.front().redistributionScore;


    double maxRedistribution =
        front.front().redistributionScore;


    std::size_t maxRedistributionIndex =
        0;


    std::size_t minGlobalIndex =
        0;


    for (std::size_t index = 0;
         index < front.size();
         ++index)
    {
        const auto& metric =
            front[index];
    {
        minRoi =
            std::min(
                minRoi,
                metric.roiErrorChange
            );


        maxRoi =
            std::max(
                maxRoi,
                metric.roiErrorChange
            );


        const double currentGlobal =
            globalErrorChange(
                evaluation,
                metric
            );


        minGlobal =
            std::min(
                minGlobal,
                currentGlobal
            );


        if (
            currentGlobal
            <
            minGlobal
        )
        {
            minGlobal =
                currentGlobal;

            minGlobalIndex =
                index;
        }


        maxGlobal =
            std::max(
                maxGlobal,
                currentGlobal
            );


        if (
            metric.redistributionScore
            <
            minRedistribution
        )
        {
            minRedistribution =
                metric.redistributionScore;
        }


        if (
            metric.redistributionScore
            >
            maxRedistribution
        )
        {
            maxRedistribution =
                metric.redistributionScore;

            maxRedistributionIndex =
                index;
        }
    }


    std::vector<bool>
        selected(
            front.size(),
            false
        );


    std::vector<std::size_t>
        selectedIndices;


    selectedIndices.reserve(
        maxCount
    );


    // 显式保留三个方向的极值：
    // 1. ROI 破坏最强；
    // 2. Global 代价最小；
    // 3. ROI 相对 Non-ROI 的误差重分布最强。
    selected[0] =
        true;


    selectedIndices.push_back(
        0
    );


    if (
        selectedIndices.size()
        <
        maxCount
        &&
        !selected[minGlobalIndex]
    )
    {
        selected[minGlobalIndex] =
            true;


        selectedIndices.push_back(
            minGlobalIndex
        );
    }


    if (
        selectedIndices.size()
        <
        maxCount
        &&
        !selected[maxRedistributionIndex]
    )
    {
        selected[maxRedistributionIndex] =
            true;


        selectedIndices.push_back(
            maxRedistributionIndex
        );
    }


    // 在归一化三维误差空间中做最远点补充。
    //
    // 这样不会只保留某一个极端附近的大量相似区间，
    // 而是尽量覆盖整条“ROI破坏 - Global代价”前沿。
    while (
        selectedIndices.size()
        <
        maxCount
    )
    {
        std::size_t bestIndex =
            front.size();


        double bestMinimumDistance =
            -1.0;


        for (std::size_t candidateIndex = 0;
             candidateIndex < front.size();
             ++candidateIndex)
        {
            if (selected[candidateIndex])
            {
                continue;
            }


            double minimumDistance =
                std::numeric_limits<double>::infinity();


            for (const auto selectedIndex : selectedIndices)
            {
                minimumDistance =
                    std::min(
                        minimumDistance,
                        normalizedDistanceSquared(
                            evaluation,
                            front[candidateIndex],
                            front[selectedIndex],
                            minRoi,
                            maxRoi,
                            minGlobal,
                            maxGlobal,
                            minRedistribution,
                            maxRedistribution
                        )
                    );
            }


            if (
                minimumDistance
                >
                bestMinimumDistance
            )
            {
                bestMinimumDistance =
                    minimumDistance;


                bestIndex =
                    candidateIndex;
            }
        }


        if (bestIndex == front.size())
        {
            break;
        }


        selected[bestIndex] =
            true;


        selectedIndices.push_back(
            bestIndex
        );
    }


    result.representatives.reserve(
        selectedIndices.size()
    );


    for (const auto index : selectedIndices)
    {
        result.representatives.push_back(
            front[index]
        );
    }


    // 仅为了输出和后续调试更直观：
    // 从“ROI攻击端”排到“整体质量保护端”。
    std::sort(
        result.representatives.begin(),
        result.representatives.end(),

        [&evaluation](
            const DctIntervalFastMetric& first,
            const DctIntervalFastMetric& second
        )
        {
            if (
                first.roiErrorChange
                !=
                second.roiErrorChange
            )
            {
                return
                    first.roiErrorChange
                    >
                    second.roiErrorChange;
            }


            return
                globalErrorChange(
                    evaluation,
                    first
                )
                <
                globalErrorChange(
                    evaluation,
                    second
                );
        }
    );


    return result;
}

}

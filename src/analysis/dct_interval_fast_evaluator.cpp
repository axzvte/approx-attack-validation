#include "analysis/dct_interval_fast_evaluator.hpp"

#include "analysis/dct_interval_search_space_generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
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

}


// =========================================================
// Module 2-A：快速区间局部误差评价
// =========================================================

DctIntervalFastEvaluation
DctIntervalFastEvaluator::evaluateFromSamples(
    const std::vector<core::AddSample>& samples,
    int nodeId,
    core::MonitorSignal monitorInput,
    approximate::ApproxUnitId attackUnit
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


    result.metrics.reserve(
        static_cast<std::size_t>(
            DctIntervalSearchSpaceGenerator::
                countIntervals(
                    valueCount
                )
        )
    );


    // 与 Module 1 完全相同的区间顺序：
    // lowerIndex 外层，upperIndex 内层。
    //
    // 每个区间只做常数次前缀和相减。
    for (std::size_t lowerIndex = 0;
         lowerIndex < valueCount;
         ++lowerIndex)
    {
        for (std::size_t upperIndex = lowerIndex;
             upperIndex < valueCount;
             ++upperIndex)
        {
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
        ranked =
            evaluation.metrics;


    // 当前搜索目标只有两个方向：
    // 1. ROIErrorChange 越大越好；
    // 2. GlobalErrorChange 越小越好。
    //
    // Non-ROI 只参与 GlobalErrorChange 的组成，
    // 不再作为独立筛选目标。
    std::sort(
        ranked.begin(),
        ranked.end(),

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


            const double firstGlobal =
                globalErrorChange(
                    evaluation,
                    first
                );


            const double secondGlobal =
                globalErrorChange(
                    evaluation,
                    second
                );


            if (firstGlobal != secondGlobal)
            {
                return
                    firstGlobal
                    <
                    secondGlobal;
            }


            if (
                first.interval.lower
                !=
                second.interval.lower
            )
            {
                return
                    first.interval.lower
                    <
                    second.interval.lower;
            }


            return
                first.interval.upper
                <
                second.interval.upper;
        }
    );


    std::vector<DctIntervalFastMetric>
        front;


    front.reserve(
        ranked.size()
    );


    double bestGlobalErrorChange =
        std::numeric_limits<double>::infinity();


    bool hasFrontPoint =
        false;


    for (const auto& metric : ranked)
    {
        const double currentGlobal =
            globalErrorChange(
                evaluation,
                metric
            );


        if (
            !hasFrontPoint
            ||
            currentGlobal
                <
                bestGlobalErrorChange
        )
        {
            front.push_back(
                metric
            );


            bestGlobalErrorChange =
                currentGlobal;


            hasFrontPoint =
                true;
        }
    }


    return front;
}


double normalizedDistanceSquared(
    const DctIntervalFastEvaluation& evaluation,
    const DctIntervalFastMetric& first,
    const DctIntervalFastMetric& second,
    double minRoi,
    double maxRoi,
    double minGlobal,
    double maxGlobal
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


    const double roiDifference =
        firstRoi
        -
        secondRoi;


    const double globalDifference =
        firstGlobal
        -
        secondGlobal;


    return
        roiDifference
        *
        roiDifference
        +
        globalDifference
        *
        globalDifference;
}

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


    for (const auto& metric : front)
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


        maxGlobal =
            std::max(
                maxGlobal,
                currentGlobal
            );
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


    // 前沿第一端：
    // ROIErrorChange 最大，偏“攻击能力”。
    selected[0] =
        true;


    selectedIndices.push_back(
        0
    );


    if (maxCount > 1)
    {
        // 前沿另一端：
        // GlobalErrorChange 最小，偏“整体质量保护”。
        const std::size_t lastIndex =
            front.size()
            -
            1;


        if (!selected[lastIndex])
        {
            selected[lastIndex] =
                true;


            selectedIndices.push_back(
                lastIndex
            );
        }
    }


    // 在归一化二维误差空间中做最远点补充。
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
                            maxGlobal
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

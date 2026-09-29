#include "analysis/dct_monitor_input_analyzer.hpp"

#include "applications/dct8_fixed_graph.hpp"
#include "core/add_sample.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>


namespace analysis
{

namespace
{

struct WeightedCount
{
    double roi = 0.0;
    double nonRoi = 0.0;
};


struct BestIntervalPair
{
    DctBestIntervalResult roiBiased;
    DctBestIntervalResult nonRoiBiased;
};


int monitoredValue(
    const core::AddSample& sample,
    core::MonitorInput monitorInput
)
{
    return
        monitorInput
            ==
            core::MonitorInput::Input1
        ?
        sample.input1
        :
        sample.input2;
}


void validateCandidateNodes(
    const applications::DctApplication& application,
    const std::vector<int>& candidateNodeIds
)
{
    if (candidateNodeIds.empty())
    {
        throw std::runtime_error(
            "DCT monitor-input candidate node list is empty."
        );
    }


    const std::size_t nodeCount =
        application.addNodes().size();


    std::vector<bool>
        seen(
            nodeCount,
            false
        );


    for (const int nodeId : candidateNodeIds)
    {
        if (
            nodeId < 0
            ||
            static_cast<std::size_t>(
                nodeId
            )
                >=
                nodeCount
        )
        {
            throw std::runtime_error(
                "DCT monitor-input candidate node ID is invalid."
            );
        }


        if (seen[nodeId])
        {
            throw std::runtime_error(
                "DCT monitor-input candidate node list contains duplicates."
            );
        }


        seen[nodeId] =
            true;
    }
}


BestIntervalPair findBestIntervals(
    const std::vector<core::AddSample>& nodeSamples,
    int nodeId,
    core::MonitorInput monitorInput,
    std::size_t imageIndex
)
{
    if (nodeSamples.empty())
    {
        throw std::runtime_error(
            "DCT monitor-input node has no samples."
        );
    }


    std::map<int, WeightedCount>
        histogram;


    double totalRoi =
        0.0;


    double totalNonRoi =
        0.0;


    for (const auto& sample : nodeSamples)
    {
        const int value =
            monitoredValue(
                sample,
                monitorInput
            );


        const double roiWeight =
            sample.roiWeight;


        const double nonRoiWeight =
            1.0
            -
            roiWeight;


        histogram[value].roi +=
            roiWeight;


        histogram[value].nonRoi +=
            nonRoiWeight;


        totalRoi +=
            roiWeight;


        totalNonRoi +=
            nonRoiWeight;
    }


    if (
        totalRoi <= 0.0
        ||
        totalNonRoi <= 0.0
    )
    {
        throw std::runtime_error(
            "DCT monitor-input analysis requires both ROI and Non-ROI samples."
        );
    }


    const std::size_t valueCount =
        histogram.size();


    std::vector<int>
        values;


    std::vector<double>
        roiPrefix(
            valueCount + 1,
            0.0
        );


    std::vector<double>
        nonRoiPrefix(
            valueCount + 1,
            0.0
        );


    values.reserve(
        valueCount
    );


    std::size_t index =
        0;


    for (const auto& item : histogram)
    {
        values.push_back(
            item.first
        );


        roiPrefix[index + 1] =
            roiPrefix[index]
            +
            item.second.roi;


        nonRoiPrefix[index + 1] =
            nonRoiPrefix[index]
            +
            item.second.nonRoi;


        ++index;
    }


    BestIntervalPair
        result;


    result.roiBiased.nodeId =
        nodeId;


    result.roiBiased.monitorInput =
        monitorInput;


    result.roiBiased.bias =
        RegionBias::Roi;


    result.roiBiased.imageIndex =
        imageIndex;


    result.nonRoiBiased =
        result.roiBiased;


    result.nonRoiBiased.bias =
        RegionBias::NonRoi;


    double bestRoiGap =
        -std::numeric_limits<double>::infinity();


    double bestNonRoiGap =
        -std::numeric_limits<double>::infinity();


    // 只需要使用“实际出现过的整数值”作为边界。
    // 对于中间没有样本的整数，扩大/缩小区间不会改变触发率。
    for (std::size_t lowerIndex = 0;
         lowerIndex < valueCount;
         ++lowerIndex)
    {
        for (std::size_t upperIndex = lowerIndex;
             upperIndex < valueCount;
             ++upperIndex)
        {
            const double roiWeight =
                roiPrefix[
                    upperIndex + 1
                ]
                -
                roiPrefix[
                    lowerIndex
                ];


            const double nonRoiWeight =
                nonRoiPrefix[
                    upperIndex + 1
                ]
                -
                nonRoiPrefix[
                    lowerIndex
                ];


            const double roiTriggerRate =
                roiWeight
                /
                totalRoi;


            const double nonRoiTriggerRate =
                nonRoiWeight
                /
                totalNonRoi;


            const double roiGap =
                roiTriggerRate
                -
                nonRoiTriggerRate;


            const double nonRoiGap =
                nonRoiTriggerRate
                -
                roiTriggerRate;


            if (roiGap > bestRoiGap)
            {
                bestRoiGap =
                    roiGap;


                result.roiBiased.lower =
                    values[
                        lowerIndex
                    ];


                result.roiBiased.upper =
                    values[
                        upperIndex
                    ];


                result.roiBiased.roiTriggerRate =
                    roiTriggerRate;


                result.roiBiased.nonRoiTriggerRate =
                    nonRoiTriggerRate;


                result.roiBiased.gap =
                    std::max(
                        0.0,
                        roiGap
                    );
            }


            if (nonRoiGap > bestNonRoiGap)
            {
                bestNonRoiGap =
                    nonRoiGap;


                result.nonRoiBiased.lower =
                    values[
                        lowerIndex
                    ];


                result.nonRoiBiased.upper =
                    values[
                        upperIndex
                    ];


                result.nonRoiBiased.roiTriggerRate =
                    roiTriggerRate;


                result.nonRoiBiased.nonRoiTriggerRate =
                    nonRoiTriggerRate;


                result.nonRoiBiased.gap =
                    std::max(
                        0.0,
                        nonRoiGap
                    );
            }
        }
    }


    return result;
}


DctMonitorInputSummary summarize(
    const std::vector<DctBestIntervalResult>& results,
    int nodeId,
    core::MonitorInput monitorInput,
    RegionBias bias
)
{
    std::vector<const DctBestIntervalResult*>
        selected;


    for (const auto& result : results)
    {
        if (
            result.nodeId == nodeId
            &&
            result.monitorInput == monitorInput
            &&
            result.bias == bias
        )
        {
            selected.push_back(
                &result
            );
        }
    }


    if (selected.empty())
    {
        throw std::runtime_error(
            "DCT monitor-input summary has no per-image results."
        );
    }


    DctMonitorInputSummary
        summary;


    summary.nodeId =
        nodeId;


    summary.monitorInput =
        monitorInput;


    summary.bias =
        bias;


    for (const auto* result : selected)
    {
        summary.meanGap +=
            result->gap;


        summary.meanRoiTriggerRate +=
            result->roiTriggerRate;


        summary.meanNonRoiTriggerRate +=
            result->nonRoiTriggerRate;
    }


    const double count =
        static_cast<double>(
            selected.size()
        );


    summary.meanGap /=
        count;


    summary.meanRoiTriggerRate /=
        count;


    summary.meanNonRoiTriggerRate /=
        count;


    double squaredDifferenceSum =
        0.0;


    for (const auto* result : selected)
    {
        const double difference =
            result->gap
            -
            summary.meanGap;


        squaredDifferenceSum +=
            difference
            *
            difference;
    }


    summary.stdGap =
        std::sqrt(
            squaredDifferenceSum
            /
            count
        );


    if (summary.meanGap > 0.0)
    {
        summary.cvGap =
            summary.stdGap
            /
            summary.meanGap;
    }
    else
    {
        summary.cvGap =
            std::numeric_limits<double>::infinity();
    }


    return summary;
}

}


// =========================================================
// Monitor input 区域区分能力分析
// =========================================================

DctMonitorInputReport
DctMonitorInputAnalyzer::analyze(
    const applications::DctApplication& application,
    const std::vector<cv::Mat>& inputImages,
    const std::vector<cv::Mat>& roiMasks,
    const std::vector<int>& candidateNodeIds
)
{
    if (inputImages.empty())
    {
        throw std::runtime_error(
            "DCT monitor-input image set is empty."
        );
    }


    if (
        inputImages.size()
        !=
        roiMasks.size()
    )
    {
        throw std::runtime_error(
            "DCT monitor-input image and ROI-mask counts do not match."
        );
    }


    validateCandidateNodes(
        application,
        candidateNodeIds
    );


    DctMonitorInputReport
        report;


    const std::vector<core::MonitorInput>
        monitorInputs =
    {
        core::MonitorInput::Input1,
        core::MonitorInput::Input2
    };


    for (std::size_t imageIndex = 0;
         imageIndex < inputImages.size();
         ++imageIndex)
    {
        std::vector<core::AddSample>
            allSamples;


        application.collectBaselineAddSamples(
            inputImages[imageIndex],
            roiMasks[imageIndex],
            allSamples
        );


        std::vector<
            std::vector<core::AddSample>
        >
            samplesByNode(
                application.addNodes().size()
            );


        for (const auto& sample : allSamples)
        {
            if (
                sample.nodeId >= 0
                &&
                static_cast<std::size_t>(
                    sample.nodeId
                )
                    <
                    samplesByNode.size()
            )
            {
                samplesByNode[
                    sample.nodeId
                ].push_back(
                    sample
                );
            }
        }


        for (const int nodeId : candidateNodeIds)
        {
            for (const auto monitorInput : monitorInputs)
            {
                const BestIntervalPair best =
                    findBestIntervals(
                        samplesByNode[
                            nodeId
                        ],
                        nodeId,
                        monitorInput,
                        imageIndex
                    );


                report.perImageResults.push_back(
                    best.roiBiased
                );


                report.perImageResults.push_back(
                    best.nonRoiBiased
                );
            }
        }
    }


    report.summaries.reserve(
        candidateNodeIds.size()
        *
        2
        *
        2
    );


    for (const int nodeId : candidateNodeIds)
    {
        for (const auto monitorInput : monitorInputs)
        {
            report.summaries.push_back(
                summarize(
                    report.perImageResults,
                    nodeId,
                    monitorInput,
                    RegionBias::Roi
                )
            );


            report.summaries.push_back(
                summarize(
                    report.perImageResults,
                    nodeId,
                    monitorInput,
                    RegionBias::NonRoi
                )
            );
        }
    }


    return report;
}

}

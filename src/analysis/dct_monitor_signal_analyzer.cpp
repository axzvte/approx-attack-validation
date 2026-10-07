#include "analysis/dct_monitor_signal_analyzer.hpp"

#include "core/add_sample.hpp"

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

struct WeightedCount
{
    double roi = 0.0;
    double nonRoi = 0.0;
};


struct BestIntervalPair
{
    DctMonitorSignalBestInterval roiBiased;
    DctMonitorSignalBestInterval nonRoiBiased;
};


int signalValue(
    const core::AddSample& sample,
    DctMonitorSignal signal
)
{
    switch (signal)
    {
        case DctMonitorSignal::Input1:
            return sample.input1;

        case DctMonitorSignal::Input2:
            return sample.input2;

        case DctMonitorSignal::BaselineOutput:
            // collectBaselineAddSamples(...) 运行的是正常 5RP 路径，
            // 因此 sample.output 就是当前节点 5RP 的实际动态输出。
            return sample.output;
    }


    throw std::runtime_error(
        "Unknown DCT monitor signal."
    );
}


void validateCandidateNodes(
    const applications::DctApplication& application,
    const std::vector<int>& candidateNodeIds
)
{
    if (candidateNodeIds.empty())
    {
        throw std::runtime_error(
            "DCT monitor-signal candidate node list is empty."
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
                "DCT monitor-signal candidate node ID is invalid."
            );
        }


        if (seen[nodeId])
        {
            throw std::runtime_error(
                "DCT monitor-signal candidate node list contains duplicates."
            );
        }


        seen[nodeId] =
            true;
    }
}


BestIntervalPair findBestIntervals(
    const std::vector<core::AddSample>& nodeSamples,
    int nodeId,
    DctMonitorSignal signal,
    std::size_t imageIndex
)
{
    if (nodeSamples.empty())
    {
        throw std::runtime_error(
            "DCT monitor-signal node has no samples."
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
        if (
            sample.roiWeight < 0.0
            ||
            sample.roiWeight > 1.0
        )
        {
            throw std::runtime_error(
                "DCT monitor-signal sample contains an invalid ROI weight."
            );
        }


        const int value =
            signalValue(
                sample,
                signal
            );


        const double roiWeight =
            sample.roiWeight;


        const double nonRoiWeight =
            1.0
            -
            roiWeight;


        histogram[
            value
        ].roi +=
            roiWeight;


        histogram[
            value
        ].nonRoi +=
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
            "DCT monitor-signal analysis requires both ROI and Non-ROI samples."
        );
    }


    const std::size_t valueCount =
        histogram.size();


    std::vector<int>
        values;


    values.reserve(
        valueCount
    );


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


    std::size_t index =
        0;


    for (const auto& item : histogram)
    {
        values.push_back(
            item.first
        );


        roiPrefix[
            index + 1
        ] =
            roiPrefix[
                index
            ]
            +
            item.second.roi;


        nonRoiPrefix[
            index + 1
        ] =
            nonRoiPrefix[
                index
            ]
            +
            item.second.nonRoi;


        ++index;
    }


    BestIntervalPair result;


    result.roiBiased.nodeId =
        nodeId;


    result.roiBiased.signal =
        signal;


    result.roiBiased.bias =
        DctMonitorSignalBias::Roi;


    result.roiBiased.imageIndex =
        imageIndex;


    result.nonRoiBiased =
        result.roiBiased;


    result.nonRoiBiased.bias =
        DctMonitorSignalBias::NonRoi;


    double bestRoiGap =
        -std::numeric_limits<double>::infinity();


    double bestNonRoiGap =
        -std::numeric_limits<double>::infinity();


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


                auto& best =
                    result.roiBiased;


                best.lower =
                    values[
                        lowerIndex
                    ];


                best.upper =
                    values[
                        upperIndex
                    ];


                best.roiTriggerRate =
                    roiTriggerRate;


                best.nonRoiTriggerRate =
                    nonRoiTriggerRate;


                best.gap =
                    std::max(
                        0.0,
                        roiGap
                    );
            }


            if (nonRoiGap > bestNonRoiGap)
            {
                bestNonRoiGap =
                    nonRoiGap;


                auto& best =
                    result.nonRoiBiased;


                best.lower =
                    values[
                        lowerIndex
                    ];


                best.upper =
                    values[
                        upperIndex
                    ];


                best.roiTriggerRate =
                    roiTriggerRate;


                best.nonRoiTriggerRate =
                    nonRoiTriggerRate;


                best.gap =
                    std::max(
                        0.0,
                        nonRoiGap
                    );
            }
        }
    }


    return result;
}


DctMonitorSignalSummary summarize(
    const std::vector<DctMonitorSignalBestInterval>& results,
    int nodeId,
    DctMonitorSignal signal,
    DctMonitorSignalBias bias
)
{
    std::vector<const DctMonitorSignalBestInterval*>
        selected;


    for (const auto& result : results)
    {
        if (
            result.nodeId == nodeId
            &&
            result.signal == signal
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
            "DCT monitor-signal summary has no per-image results."
        );
    }


    DctMonitorSignalSummary summary;


    summary.nodeId =
        nodeId;


    summary.signal =
        signal;


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
// Input1 / Input2 / Baseline Output 公平比较
// =========================================================

DctMonitorSignalReport
DctMonitorSignalAnalyzer::analyze(
    const applications::DctApplication& application,
    const std::vector<cv::Mat>& inputImages,
    const std::vector<cv::Mat>& roiMasks,
    const std::vector<int>& candidateNodeIds
)
{
    if (inputImages.empty())
    {
        throw std::runtime_error(
            "DCT monitor-signal image set is empty."
        );
    }


    if (
        inputImages.size()
        !=
        roiMasks.size()
    )
    {
        throw std::runtime_error(
            "DCT monitor-signal image/mask counts do not match."
        );
    }


    validateCandidateNodes(
        application,
        candidateNodeIds
    );


    const std::vector<DctMonitorSignal>
        signals =
    {
        DctMonitorSignal::Input1,
        DctMonitorSignal::Input2,
        DctMonitorSignal::BaselineOutput
    };


    DctMonitorSignalReport report;


    for (std::size_t imageIndex = 0;
         imageIndex < inputImages.size();
         ++imageIndex)
    {
        std::vector<core::AddSample>
            allSamples;


        application.collectBaselineAddSamples(
            inputImages[
                imageIndex
            ],
            roiMasks[
                imageIndex
            ],
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
            for (const auto signal : signals)
            {
                const BestIntervalPair best =
                    findBestIntervals(
                        samplesByNode[
                            nodeId
                        ],
                        nodeId,
                        signal,
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
        signals.size()
        *
        2
    );


    for (const int nodeId : candidateNodeIds)
    {
        for (const auto signal : signals)
        {
            report.summaries.push_back(
                summarize(
                    report.perImageResults,
                    nodeId,
                    signal,
                    DctMonitorSignalBias::Roi
                )
            );


            report.summaries.push_back(
                summarize(
                    report.perImageResults,
                    nodeId,
                    signal,
                    DctMonitorSignalBias::NonRoi
                )
            );
        }
    }


    return report;
}

}

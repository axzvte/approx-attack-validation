#include "analysis/dct_multi_state_joint_search.hpp"

#include "analysis/dct_interval_fast_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace analysis
{

namespace
{

struct BeamReductionResult
{
    std::vector<DctMultiStateSearchState>
        states;

    std::size_t paretoStateCount = 0;
};


void validateStructure(
    const AttackStructure& structure
)
{
    if (structure.empty())
    {
        throw std::runtime_error(
            "Multi-state joint search received an empty structure."
        );
    }


    std::set<int>
        nodeIds;


    for (const auto& node : structure)
    {
        if (node.nodeId < 0)
        {
            throw std::runtime_error(
                "Multi-state joint search received a negative node ID."
            );
        }


        if (
            !nodeIds.insert(
                node.nodeId
            ).second
        )
        {
            throw std::runtime_error(
                "Multi-state joint search received duplicate node IDs."
            );
        }
    }
}


void validateOptions(
    const DctMultiStateJointSearchOptions& options
)
{
    if (
        options.candidateBoundaryCount == 1
    )
    {
        throw std::runtime_error(
            "candidateBoundaryCount must be 0 or at least 2."
        );
    }


    if (options.representativeIntervalCount == 0)
    {
        throw std::runtime_error(
            "representativeIntervalCount must be greater than zero."
        );
    }


    if (options.fullValidationCandidateCount == 0)
    {
        throw std::runtime_error(
            "fullValidationCandidateCount must be greater than zero."
        );
    }


    if (options.beamWidth == 0)
    {
        throw std::runtime_error(
            "beamWidth must be greater than zero."
        );
    }


    {
        std::set<int>
            seenUnits;


        for (const auto unit : options.redistributionUnits)
        {
            const int key =
                static_cast<int>(
                    unit
                );


            if (!seenUnits.insert(key).second)
            {
                throw std::runtime_error(
                    "redistributionUnits contains duplicate implementations."
                );
            }
        }
    }


    {
        std::set<int>
            seenSignals;


        for (const auto signal : options.monitorSignals)
        {
            const int key =
                static_cast<int>(
                    signal
                );


            if (!seenSignals.insert(key).second)
            {
                throw std::runtime_error(
                    "monitorSignals contains duplicate signals."
                );
            }
        }
    }


    if (
        !std::isfinite(
            options.globalPsnrThreshold
        )
    )
    {
        throw std::runtime_error(
            "globalPsnrThreshold must be finite."
        );
    }


    if (
        !std::isfinite(
            options.comparisonEpsilon
        )
        ||
        options.comparisonEpsilon < 0.0
    )
    {
        throw std::runtime_error(
            "comparisonEpsilon is invalid."
        );
    }
}


AttackConfiguration removeTarget(
    const AttackConfiguration& configuration,
    int nodeId
)
{
    AttackConfiguration
        result;


    result.reserve(
        configuration.size()
    );


    for (const auto& config : configuration)
    {
        if (config.nodeId != nodeId)
        {
            result.push_back(
                config
            );
        }
    }


    return result;
}


void setTarget(
    AttackConfiguration& configuration,
    const AttackStructureNode& node,
    const TriggerInterval& interval
)
{
    bool replaced =
        false;


    for (auto& config : configuration)
    {
        if (config.nodeId != node.nodeId)
        {
            continue;
        }


        if (replaced)
        {
            throw std::runtime_error(
                "Multi-state joint search found duplicate target configs."
            );
        }


        config.unit =
            node.unit;


        config.monitorInput =
            node.monitorInput;


        config.lower =
            interval.lower;


        config.upper =
            interval.upper;


        replaced =
            true;
    }


    if (!replaced)
    {
        configuration.push_back(
            core::AttackConfig{
                node.nodeId,
                node.unit,
                node.monitorInput,
                interval.lower,
                interval.upper
            }
        );
    }


    std::sort(
        configuration.begin(),
        configuration.end(),

        [](
            const core::AttackConfig& first,
            const core::AttackConfig& second
        )
        {
            return
                first.nodeId
                <
                second.nodeId;
        }
    );
}


std::string configurationKey(
    const AttackConfiguration& configuration
)
{
    AttackConfiguration
        canonical =
            configuration;


    std::sort(
        canonical.begin(),
        canonical.end(),

        [](
            const core::AttackConfig& first,
            const core::AttackConfig& second
        )
        {
            return
                first.nodeId
                <
                second.nodeId;
        }
    );


    std::ostringstream stream;


    for (const auto& config : canonical)
    {
        stream
            << config.nodeId
            << ":"
            << static_cast<int>(
                config.unit
            )
            << ":"
            << static_cast<int>(
                config.monitorInput
            )
            << ":"
            << config.lower
            << ":"
            << config.upper
            << ";";
    }


    return stream.str();
}


struct JointFastCandidate
{
    core::MonitorSignal signal =
        core::MonitorSignal::Input1;

    approximate::ApproxUnitId unit =
        approximate::ApproxUnitId::Add12se5RP;

    TriggerInterval interval;

    double roiErrorChange = 0.0;
    double globalErrorChange = 0.0;

    // 快速阶段的区域误差重分布倾向：
    // ROIErrorChange - NonROIErrorChange。
    // 越大表示新增局部误差越倾向 ROI。
    double redistributionScore = 0.0;
};


struct JointValidationGroup
{
    core::MonitorSignal signal =
        core::MonitorSignal::Input1;

    approximate::ApproxUnitId unit =
        approximate::ApproxUnitId::Add12se5RP;

    std::vector<TriggerInterval>
        intervals;
};


double fastGlobalErrorChange(
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
            "Joint fast candidate has zero total weight."
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


bool jointFastCandidateComesFirst(
    const JointFastCandidate& first,
    const JointFastCandidate& second
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


    if (
        first.globalErrorChange
        !=
        second.globalErrorChange
    )
    {
        return
            first.globalErrorChange
            <
            second.globalErrorChange;
    }


    if (
        first.signal
        !=
        second.signal
    )
    {
        return
            static_cast<int>(
                first.signal
            )
            <
            static_cast<int>(
                second.signal
            );
    }


    if (
        first.unit
        !=
        second.unit
    )
    {
        return
            static_cast<int>(
                first.unit
            )
            <
            static_cast<int>(
                second.unit
            );
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


double jointFastDistanceSquared(
    const JointFastCandidate& first,
    const JointFastCandidate& second,
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
            first.globalErrorChange
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
            second.globalErrorChange
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


std::vector<JointFastCandidate>
selectJointFastCandidates(
    const std::vector<JointFastCandidate>& candidates,
    std::size_t maxCount
)
{
    if (
        candidates.empty()
        ||
        maxCount == 0
    )
    {
        return {};
    }


    std::vector<JointFastCandidate>
        front;


    front.reserve(
        candidates.size()
    );


    // 三维 Pareto：
    // ROIErrorChange 越大越好；
    // GlobalErrorChange 越小越好；
    // RedistributionScore 越大越好。
    for (std::size_t index = 0;
         index < candidates.size();
         ++index)
    {
        const auto& candidate =
            candidates[index];


        bool dominated =
            false;


        for (std::size_t otherIndex = 0;
             otherIndex < candidates.size();
             ++otherIndex)
        {
            if (index == otherIndex)
            {
                continue;
            }


            const auto& other =
                candidates[otherIndex];


            const bool roiNoWorse =
                other.roiErrorChange
                >=
                candidate.roiErrorChange;


            const bool globalNoWorse =
                other.globalErrorChange
                <=
                candidate.globalErrorChange;


            const bool redistributionNoWorse =
                other.redistributionScore
                >=
                candidate.redistributionScore;


            const bool strictlyBetter =
                other.roiErrorChange
                    >
                    candidate.roiErrorChange
                ||
                other.globalErrorChange
                    <
                    candidate.globalErrorChange
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
        jointFastCandidateComesFirst
    );


    if (
        front.size()
        <=
        maxCount
    )
    {
        return front;
    }


    double minRoi =
        front.front().roiErrorChange;

    double maxRoi =
        front.front().roiErrorChange;

    double minGlobal =
        front.front().globalErrorChange;

    double maxGlobal =
        front.front().globalErrorChange;

    double minRedistribution =
        front.front().redistributionScore;

    double maxRedistribution =
        front.front().redistributionScore;


    std::size_t minGlobalIndex =
        0;

    std::size_t maxRedistributionIndex =
        0;


    for (std::size_t index = 0;
         index < front.size();
         ++index)
    {
        const auto& candidate =
            front[index];


        minRoi =
            std::min(
                minRoi,
                candidate.roiErrorChange
            );


        maxRoi =
            std::max(
                maxRoi,
                candidate.roiErrorChange
            );


        if (
            candidate.globalErrorChange
            <
            minGlobal
        )
        {
            minGlobal =
                candidate.globalErrorChange;

            minGlobalIndex =
                index;
        }


        maxGlobal =
            std::max(
                maxGlobal,
                candidate.globalErrorChange
            );


        minRedistribution =
            std::min(
                minRedistribution,
                candidate.redistributionScore
            );


        if (
            candidate.redistributionScore
            >
            maxRedistribution
        )
        {
            maxRedistribution =
                candidate.redistributionScore;

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


    const auto addIndex =
        [&](
            std::size_t index
        )
        {
            if (
                index < selected.size()
                &&
                !selected[index]
                &&
                selectedIndices.size() < maxCount
            )
            {
                selected[index] =
                    true;

                selectedIndices.push_back(
                    index
                );
            }
        };


    // 三个端点：ROI破坏最强、Global代价最小、区域重分布最强。
    addIndex(
        0
    );


    addIndex(
        minGlobalIndex
    );


    addIndex(
        maxRedistributionIndex
    );


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
                        jointFastDistanceSquared(
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


        addIndex(
            bestIndex
        );
    }


    std::vector<JointFastCandidate>
        result;


    result.reserve(
        selectedIndices.size()
    );


    for (const auto index : selectedIndices)
    {
        result.push_back(
            front[index]
        );
    }


    std::sort(
        result.begin(),
        result.end(),
        jointFastCandidateComesFirst
    );


    return result;
}


std::vector<JointValidationGroup>
groupJointValidationCandidates(
    const std::vector<JointFastCandidate>& candidates
)
{
    std::vector<JointValidationGroup>
        groups;


    for (const auto& candidate : candidates)
    {
        auto iterator =
            std::find_if(
                groups.begin(),
                groups.end(),

                [&candidate](
                    const JointValidationGroup& group
                )
                {
                    return
                        group.signal
                            ==
                            candidate.signal
                        &&
                        group.unit
                            ==
                            candidate.unit;
                }
            );


        if (iterator == groups.end())
        {
            JointValidationGroup
                group;


            group.signal =
                candidate.signal;


            group.unit =
                candidate.unit;


            group.intervals.push_back(
                candidate.interval
            );


            groups.push_back(
                std::move(
                    group
                )
            );
        }
        else
        {
            iterator->intervals.push_back(
                candidate.interval
            );
        }
    }


    return groups;
}


std::vector<DctMultiStateSearchState>
expandStateOnNode(
    const core::Application& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const DctMultiStateSearchState& currentState,
    const AttackStructureNode& node,
    const DctMultiStateJointSearchOptions& options
)
{
    std::vector<DctMultiStateSearchState>
        expanded;


    // 保留“不修改当前目标节点”的原状态。
    expanded.push_back(
        currentState
    );


    const AttackConfiguration
        withoutTarget =
            removeTarget(
                currentState.configuration,
                node.nodeId
            );


    std::vector<core::AddSample>
        samples;


    application.collectConfiguredAddSamples(
        inputImage,
        roiMask,
        withoutTarget,
        samples
    );


    std::vector<approximate::ApproxUnitId>
        unitsToTry;


    if (options.redistributionUnits.empty())
    {
        unitsToTry.push_back(
            node.unit
        );
    }
    else
    {
        unitsToTry =
            options.redistributionUnits;
    }


    std::vector<core::MonitorSignal>
        signalsToTry;


    if (options.monitorSignals.empty())
    {
        signalsToTry.push_back(
            node.monitorInput
        );
    }
    else
    {
        signalsToTry =
            options.monitorSignals;
    }


    // 第一层：每个 monitor x implementation 先做廉价快速筛选。
    // 第二层：把所有组合的局部代表区间合并，让它们统一竞争
    // fullValidationCandidateCount 个完整图像验证名额。
    std::vector<JointFastCandidate>
        fastPool;


    fastPool.reserve(
        signalsToTry.size()
        *
        unitsToTry.size()
        *
        options.representativeIntervalCount
    );


    for (const auto signal : signalsToTry)
    {
        for (const auto unit : unitsToTry)
        {
            const auto fastEvaluation =
                DctIntervalFastEvaluator::
                    evaluateFromSamples(
                        samples,
                        node.nodeId,
                        signal,
                        unit,
                        options.candidateBoundaryCount
                    );


            const auto localSelection =
                DctIntervalFastEvaluator::
                    selectRepresentativeMetrics(
                        fastEvaluation,
                        options.representativeIntervalCount
                    );


            for (
                const auto& metric :
                localSelection.representatives
            )
            {
                JointFastCandidate
                    candidate;


                candidate.signal =
                    signal;


                candidate.unit =
                    unit;


                candidate.interval =
                    metric.interval;


                candidate.roiErrorChange =
                    metric.roiErrorChange;


                candidate.globalErrorChange =
                    fastGlobalErrorChange(
                        fastEvaluation,
                        metric
                    );


                candidate.redistributionScore =
                    metric.redistributionScore;


                fastPool.push_back(
                    candidate
                );
            }
        }
    }


    const auto selectedFastCandidates =
        selectJointFastCandidates(
            fastPool,
            options.fullValidationCandidateCount
        );


    if (selectedFastCandidates.empty())
    {
        return expanded;
    }


    const auto validationGroups =
        groupJointValidationCandidates(
            selectedFastCandidates
        );


    expanded.reserve(
        expanded.size()
        +
        selectedFastCandidates.size()
    );


    for (const auto& group : validationGroups)
    {
        const auto fullReport =
            DctIntervalFullValidator::
                validate(
                    application,
                    inputImage,
                    roiMask,
                    withoutTarget,
                    node.nodeId,
                    group.signal,
                    group.unit,
                    group.intervals
                );


        AttackStructureNode
            implementationNode =
                node;


        implementationNode.unit =
            group.unit;


        implementationNode.monitorInput =
            group.signal;


        for (const auto& candidate : fullReport.candidates)
        {
            DctMultiStateSearchState
                state;


            state.configuration =
                withoutTarget;


            setTarget(
                state.configuration,
                implementationNode,
                candidate.interval
            );


            state.metrics =
                candidate.metrics;


            expanded.push_back(
                std::move(
                    state
                )
            );
        }
    }


    return expanded;
}

std::vector<DctMultiStateSearchState>
deduplicateStates(
    const std::vector<DctMultiStateSearchState>& states
)
{
    std::vector<DctMultiStateSearchState>
        result;


    std::set<std::string>
        seen;


    result.reserve(
        states.size()
    );


    for (const auto& state : states)
    {
        const std::string key =
            configurationKey(
                state.configuration
            );


        if (
            seen.insert(
                key
            ).second
        )
        {
            result.push_back(
                state
            );
        }
    }


    return result;
}


double redistributionAdvantage(
    const DctMultiStateSearchState& state,
    const DctImageQualityMetrics& baselineMetrics
)
{
    const double roiMseIncrease =
        state.metrics.roiMse
        -
        baselineMetrics.roiMse;


    const double nonRoiMseIncrease =
        state.metrics.nonRoiMse
        -
        baselineMetrics.nonRoiMse;


    return
        roiMseIncrease
        -
        nonRoiMseIncrease;
}


bool satisfiesRegionalRedistribution(
    const DctMultiStateSearchState& state,
    const DctImageQualityMetrics& baselineMetrics,
    double epsilon
)
{
    return
        redistributionAdvantage(
            state,
            baselineMetrics
        )
        >
        epsilon;
}


bool dominates(
    const DctMultiStateSearchState& first,
    const DctMultiStateSearchState& second,
    const DctImageQualityMetrics& baselineMetrics,
    double epsilon
)
{
    const bool globalNoWorse =
        first.metrics.globalPsnr
        >=
        second.metrics.globalPsnr
        -
        epsilon;


    const bool roiNoWorse =
        first.metrics.roiPsnr
        <=
        second.metrics.roiPsnr
        +
        epsilon;


    const double firstRedistribution =
        redistributionAdvantage(
            first,
            baselineMetrics
        );


    const double secondRedistribution =
        redistributionAdvantage(
            second,
            baselineMetrics
        );


    const bool redistributionNoWorse =
        firstRedistribution
        >=
        secondRedistribution
        -
        epsilon;


    const bool strictlyBetter =
        first.metrics.globalPsnr
            >
            second.metrics.globalPsnr
            +
            epsilon
        ||
        first.metrics.roiPsnr
            <
            second.metrics.roiPsnr
            -
            epsilon
        ||
        firstRedistribution
            >
            secondRedistribution
            +
            epsilon;


    return
        globalNoWorse
        &&
        roiNoWorse
        &&
        redistributionNoWorse
        &&
        strictlyBetter;
}


std::vector<DctMultiStateSearchState>
buildParetoFront(
    const std::vector<DctMultiStateSearchState>& states,
    const DctImageQualityMetrics& baselineMetrics,
    double epsilon
)
{
    std::vector<DctMultiStateSearchState>
        front;


    front.reserve(
        states.size()
    );


    for (std::size_t index = 0;
         index < states.size();
         ++index)
    {
        bool dominated =
            false;


        for (std::size_t otherIndex = 0;
             otherIndex < states.size();
             ++otherIndex)
        {
            if (index == otherIndex)
            {
                continue;
            }


            if (
                dominates(
                    states[otherIndex],
                    states[index],
                    baselineMetrics,
                    epsilon
                )
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
                states[index]
            );
        }
    }


    return front;
}


bool satisfiesQualityThresholds(
    const DctMultiStateSearchState& state,
    const DctMultiStateJointSearchOptions& options
)
{
    return
        state.metrics.globalPsnr
        >=
        options.globalPsnrThreshold;
}


bool satisfiesSearchConstraints(
    const DctMultiStateSearchState& state,
    const DctMultiStateJointSearchOptions& options,
    const DctImageQualityMetrics& baselineMetrics
)
{
    return
        satisfiesQualityThresholds(
            state,
            options
        )
        &&
        satisfiesRegionalRedistribution(
            state,
            baselineMetrics,
            options.comparisonEpsilon
        );
}


double qualityThresholdDeficit(
    const DctMultiStateSearchState& state,
    const DctMultiStateJointSearchOptions& options
)
{
    return
        std::max(
            0.0,
            options.globalPsnrThreshold
            -
            state.metrics.globalPsnr
        );
}


double redistributionDeficit(
    const DctMultiStateSearchState& state,
    const DctImageQualityMetrics& baselineMetrics
)
{
    return
        std::max(
            0.0,
            -
            redistributionAdvantage(
                state,
                baselineMetrics
            )
        );
}


bool betterFeasibleState(
    const DctMultiStateSearchState& first,
    const DctMultiStateSearchState& second,
    const DctImageQualityMetrics& baselineMetrics,
    double epsilon
)
{
    // 满足 Global 和区域重分布约束后，
    // 首要目标仍是让 ROI PSNR 尽可能低。
    if (
        first.metrics.roiPsnr
        <
        second.metrics.roiPsnr
        -
        epsilon
    )
    {
        return true;
    }


    if (
        std::abs(
            first.metrics.roiPsnr
            -
            second.metrics.roiPsnr
        )
        >
        epsilon
    )
    {
        return false;
    }


    // ROI 基本相同时，优先新增误差更倾向 ROI 的状态。
    const double firstRedistribution =
        redistributionAdvantage(
            first,
            baselineMetrics
        );


    const double secondRedistribution =
        redistributionAdvantage(
            second,
            baselineMetrics
        );


    if (
        firstRedistribution
        >
        secondRedistribution
        +
        epsilon
    )
    {
        return true;
    }


    if (
        std::abs(
            firstRedistribution
            -
            secondRedistribution
        )
        >
        epsilon
    )
    {
        return false;
    }


    return
        first.metrics.globalPsnr
        >
        second.metrics.globalPsnr
        +
        epsilon;
}


bool beamStateComesFirst(
    const DctMultiStateSearchState& first,
    const DctMultiStateSearchState& second,
    const DctMultiStateJointSearchOptions& options,
    const DctImageQualityMetrics& baselineMetrics
)
{
    const bool firstFeasible =
        satisfiesSearchConstraints(
            first,
            options,
            baselineMetrics
        );


    const bool secondFeasible =
        satisfiesSearchConstraints(
            second,
            options,
            baselineMetrics
        );


    if (firstFeasible != secondFeasible)
    {
        return firstFeasible;
    }


    if (firstFeasible)
    {
        return
            betterFeasibleState(
                first,
                second,
                baselineMetrics,
                options.comparisonEpsilon
            );
    }


    // 中间状态不硬删除。
    // 先看 Global 距阈值的距离，再看区域重分布缺口；
    // 最后才按 ROI / Global 排序。
    const double firstGlobalDeficit =
        qualityThresholdDeficit(
            first,
            options
        );


    const double secondGlobalDeficit =
        qualityThresholdDeficit(
            second,
            options
        );


    if (
        firstGlobalDeficit
        !=
        secondGlobalDeficit
    )
    {
        return
            firstGlobalDeficit
            <
            secondGlobalDeficit;
    }


    const double firstRedistributionDeficit =
        redistributionDeficit(
            first,
            baselineMetrics
        );


    const double secondRedistributionDeficit =
        redistributionDeficit(
            second,
            baselineMetrics
        );


    if (
        firstRedistributionDeficit
        !=
        secondRedistributionDeficit
    )
    {
        return
            firstRedistributionDeficit
            <
            secondRedistributionDeficit;
    }


    if (
        first.metrics.roiPsnr
        !=
        second.metrics.roiPsnr
    )
    {
        return
            first.metrics.roiPsnr
            <
            second.metrics.roiPsnr;
    }


    return
        first.metrics.globalPsnr
        >
        second.metrics.globalPsnr;
}


double normalizedDistanceSquared(
    const DctMultiStateSearchState& first,
    const DctMultiStateSearchState& second,
    const DctImageQualityMetrics& baselineMetrics,
    double minGlobal,
    double maxGlobal,
    double minRoi,
    double maxRoi,
    double minRedistribution,
    double maxRedistribution
)
{
    const double globalRange =
        maxGlobal
        -
        minGlobal;


    const double roiRange =
        maxRoi
        -
        minRoi;


    const double redistributionRange =
        maxRedistribution
        -
        minRedistribution;


    const double firstGlobal =
        globalRange > 0.0
        ?
        (
            first.metrics.globalPsnr
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
            second.metrics.globalPsnr
            -
            minGlobal
        )
        /
        globalRange
        :
        0.0;


    const double firstRoi =
        roiRange > 0.0
        ?
        (
            first.metrics.roiPsnr
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
            second.metrics.roiPsnr
            -
            minRoi
        )
        /
        roiRange
        :
        0.0;


    const double firstRedistribution =
        redistributionRange > 0.0
        ?
        (
            redistributionAdvantage(
                first,
                baselineMetrics
            )
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
            redistributionAdvantage(
                second,
                baselineMetrics
            )
            -
            minRedistribution
        )
        /
        redistributionRange
        :
        0.0;


    const double globalDifference =
        firstGlobal
        -
        secondGlobal;


    const double roiDifference =
        firstRoi
        -
        secondRoi;


    const double redistributionDifference =
        firstRedistribution
        -
        secondRedistribution;


    return
        globalDifference
        *
        globalDifference
        +
        roiDifference
        *
        roiDifference
        +
        redistributionDifference
        *
        redistributionDifference;
}

void addSeedIndex(
    std::size_t index,
    std::vector<bool>& selected,
    std::vector<std::size_t>& selectedIndices
)
{
    if (
        index >= selected.size()
        ||
        selected[index]
    )
    {
        return;
    }


    selected[index] =
        true;


    selectedIndices.push_back(
        index
    );
}


BeamReductionResult reduceBeam(
    const std::vector<DctMultiStateSearchState>& inputStates,
    const DctMultiStateJointSearchOptions& options,
    const DctImageQualityMetrics& baselineMetrics
)
{
    BeamReductionResult
        result;


    if (inputStates.empty())
    {
        return result;
    }


    const auto uniqueStates =
        deduplicateStates(
            inputStates
        );


    auto front =
        buildParetoFront(
            uniqueStates,
            baselineMetrics,
            options.comparisonEpsilon
        );


    result.paretoStateCount =
        front.size();


    if (
        front.size()
        <=
        options.beamWidth
    )
    {
        std::sort(
            front.begin(),
            front.end(),

            [&options, &baselineMetrics](
                const auto& first,
                const auto& second
            )
            {
                return
                    beamStateComesFirst(
                        first,
                        second,
                        options,
                        baselineMetrics
                    );
            }
        );


        result.states =
            std::move(
                front
            );


        return result;
    }


    double minGlobal =
        front.front().metrics.globalPsnr;


    double maxGlobal =
        front.front().metrics.globalPsnr;


    double minRoi =
        front.front().metrics.roiPsnr;


    double maxRoi =
        front.front().metrics.roiPsnr;


    double minRedistribution =
        redistributionAdvantage(
            front.front(),
            baselineMetrics
        );


    double maxRedistribution =
        minRedistribution;


    std::size_t maxGlobalIndex =
        0;


    std::size_t minRoiIndex =
        0;


    std::size_t maxRedistributionIndex =
        0;


    std::size_t bestFeasibleIndex =
        front.size();


    std::size_t closestToFeasibleIndex =
        front.size();


    double closestGlobalDeficit =
        std::numeric_limits<double>::infinity();


    double closestRedistributionDeficit =
        std::numeric_limits<double>::infinity();


    for (std::size_t index = 0;
         index < front.size();
         ++index)
    {
        const auto& state =
            front[index];


        const auto& metrics =
            state.metrics;


        minGlobal =
            std::min(
                minGlobal,
                metrics.globalPsnr
            );


        maxGlobal =
            std::max(
                maxGlobal,
                metrics.globalPsnr
            );


        minRoi =
            std::min(
                minRoi,
                metrics.roiPsnr
            );


        maxRoi =
            std::max(
                maxRoi,
                metrics.roiPsnr
            );


        const double currentRedistribution =
            redistributionAdvantage(
                state,
                baselineMetrics
            );


        minRedistribution =
            std::min(
                minRedistribution,
                currentRedistribution
            );


        if (
            currentRedistribution
            >
            maxRedistribution
        )
        {
            maxRedistribution =
                currentRedistribution;


            maxRedistributionIndex =
                index;
        }


        if (
            metrics.globalPsnr
            >
            front[
                maxGlobalIndex
            ].metrics.globalPsnr
        )
        {
            maxGlobalIndex =
                index;
        }


        if (
            metrics.roiPsnr
            <
            front[
                minRoiIndex
            ].metrics.roiPsnr
        )
        {
            minRoiIndex =
                index;
        }


        const bool feasible =
            satisfiesSearchConstraints(
                state,
                options,
                baselineMetrics
            );


        if (feasible)
        {
            if (
                bestFeasibleIndex == front.size()
                ||
                betterFeasibleState(
                    state,
                    front[
                        bestFeasibleIndex
                    ],
                    baselineMetrics,
                    options.comparisonEpsilon
                )
            )
            {
                bestFeasibleIndex =
                    index;
            }
        }
        else
        {
            const double currentGlobalDeficit =
                qualityThresholdDeficit(
                    state,
                    options
                );


            const double currentRedistributionDeficit =
                redistributionDeficit(
                    state,
                    baselineMetrics
                );


            if (
                currentGlobalDeficit
                    <
                    closestGlobalDeficit
                ||
                (
                    currentGlobalDeficit
                        ==
                        closestGlobalDeficit
                    &&
                    currentRedistributionDeficit
                        <
                        closestRedistributionDeficit
                )
            )
            {
                closestGlobalDeficit =
                    currentGlobalDeficit;


                closestRedistributionDeficit =
                    currentRedistributionDeficit;


                closestToFeasibleIndex =
                    index;
            }
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
        options.beamWidth
    );


    // 显式保护五类状态：
    // 1. Global 最高；
    // 2. ROI 最低；
    // 3. 区域误差重分布优势最大；
    // 4. 当前已同时满足 Global + 区域选择性约束的最佳状态；
    // 5. 距离约束最近、后续节点仍可能补偿回来的状态。
    addSeedIndex(
        maxGlobalIndex,
        selected,
        selectedIndices
    );


    addSeedIndex(
        minRoiIndex,
        selected,
        selectedIndices
    );


    addSeedIndex(
        maxRedistributionIndex,
        selected,
        selectedIndices
    );


    addSeedIndex(
        bestFeasibleIndex,
        selected,
        selectedIndices
    );


    addSeedIndex(
        closestToFeasibleIndex,
        selected,
        selectedIndices
    );


    if (
        selectedIndices.size()
        >
        options.beamWidth
    )
    {
        selectedIndices.resize(
            options.beamWidth
        );
    }


    while (
        selectedIndices.size()
        <
        options.beamWidth
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
                            front[candidateIndex],
                            front[selectedIndex],
                            baselineMetrics,
                            minGlobal,
                            maxGlobal,
                            minRoi,
                            maxRoi,
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


        addSeedIndex(
            bestIndex,
            selected,
            selectedIndices
        );
    }


    result.states.reserve(
        selectedIndices.size()
    );


    for (const auto index : selectedIndices)
    {
        result.states.push_back(
            front[index]
        );
    }


    std::sort(
        result.states.begin(),
        result.states.end(),

        [&options, &baselineMetrics](
            const auto& first,
            const auto& second
        )
        {
            return
                beamStateComesFirst(
                    first,
                    second,
                    options,
                    baselineMetrics
                );
        }
    );


    return result;
}


void processNodeLayer(
    const core::Application& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const AttackStructureNode& node,
    const DctMultiStateJointSearchOptions& options,
    std::size_t passIndex,
    std::size_t nodeIndex,
    bool refinement,
    std::vector<DctMultiStateSearchState>& beam,
    std::vector<DctMultiStateSearchLayer>& layers,
    const DctMultiStateJointSearchProgressCallback& progressCallback
)
{
    std::vector<DctMultiStateSearchState>
        expanded;


    const std::size_t inputStateCount =
        beam.size();


    for (std::size_t stateIndex = 0;
         stateIndex < beam.size();
         ++stateIndex)
    {
        auto stateExpansions =
            expandStateOnNode(
                application,
                inputImage,
                roiMask,
                beam[stateIndex],
                node,
                options
            );


        expanded.insert(
            expanded.end(),
            stateExpansions.begin(),
            stateExpansions.end()
        );


        if (progressCallback)
        {
            progressCallback(
                DctMultiStateJointSearchProgress{
                    passIndex,
                    nodeIndex,
                    node.nodeId,
                    refinement,
                    stateIndex + 1,
                    beam.size()
                }
            );
        }
    }


    const auto reduced =
        reduceBeam(
            expanded,
            options
        );


    DctMultiStateSearchLayer
        layer;


    layer.passIndex =
        passIndex;


    layer.nodeIndex =
        nodeIndex;


    layer.nodeId =
        node.nodeId;


    layer.refinement =
        refinement;


    layer.inputStateCount =
        inputStateCount;


    layer.expandedStateCount =
        expanded.size();


    layer.paretoStateCount =
        reduced.paretoStateCount;


    layer.retainedStateCount =
        reduced.states.size();


    layers.push_back(
        layer
    );


    beam =
        reduced.states;
}


bool isBetterFeasibleFinal(
    const DctMultiStateSearchState& candidate,
    const DctMultiStateSearchState& currentBest,
    double epsilon
)
{
    return
        betterFeasibleState(
            candidate,
            currentBest,
            epsilon
        );
}

}


// =========================================================
// Module 3-B：多状态联合区间搜索
// =========================================================

DctMultiStateJointSearchResult
DctMultiStateJointSearch::search(
    const core::Application& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const AttackStructure& structure,
    const DctMultiStateJointSearchOptions& options,
    const DctMultiStateJointSearchProgressCallback& progressCallback
)
{
    validateStructure(
        structure
    );


    validateOptions(
        options
    );


    DctMultiStateJointSearchResult
        result;


    result.initialMetrics =
        DctIntervalFullValidator::
            evaluateConfiguration(
                application,
                inputImage,
                roiMask,
                {}
            );


    DctMultiStateSearchState
        initialState;


    initialState.metrics =
        result.initialMetrics;


    std::vector<DctMultiStateSearchState>
        beam =
        {
            initialState
        };


    // 第一遍：按静态结构顺序逐个扩展节点。
    for (std::size_t nodeIndex = 0;
         nodeIndex < structure.size();
         ++nodeIndex)
    {
        processNodeLayer(
            application,
            inputImage,
            roiMask,
            structure[nodeIndex],
            options,
            0,
            nodeIndex,
            false,
            beam,
            result.layers,
            progressCallback
        );
    }


    // Refinement：
    // 在已经形成多节点状态后重新回访每个节点，
    // 重新采样当前其它节点作用下的 monitor 分布。
    for (std::size_t round = 0;
         round < options.refinementRounds;
         ++round)
    {
        for (std::size_t nodeIndex = 0;
             nodeIndex < structure.size();
             ++nodeIndex)
        {
            processNodeLayer(
                application,
                inputImage,
                roiMask,
                structure[nodeIndex],
                options,
                round + 1,
                nodeIndex,
                true,
                beam,
                result.layers,
                progressCallback
            );
        }
    }


    result.finalStates =
        beam;


    for (const auto& state : result.finalStates)
    {
        if (
            state.metrics.globalPsnr
            <
            options.globalPsnrThreshold
            ||
            state.metrics.roiPsnr
            >=
            result.initialMetrics.roiPsnr
        )
        {
            continue;
        }


        if (
            !result.hasBestFeasibleState
            ||
            isBetterFeasibleFinal(
                state,
                result.bestFeasibleState,
                options.comparisonEpsilon
            )
        )
        {
            result.hasBestFeasibleState =
                true;


            result.bestFeasibleState =
                state;
        }
    }


    return result;
}

}

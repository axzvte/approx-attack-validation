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


std::vector<TriggerInterval>
toIntervals(
    const DctIntervalRepresentativeSelection& selection
)
{
    std::vector<TriggerInterval>
        intervals;


    intervals.reserve(
        selection.representatives.size()
    );


    for (const auto& metric : selection.representatives)
    {
        intervals.push_back(
            metric.interval
        );
    }


    return intervals;
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


    // Keeping the current state is always legal.
    // The search therefore does not have to activate every candidate node.
    expanded.push_back(
        currentState
    );


    // Remove the target node temporarily and keep all other current
    // redistribution settings. Samples therefore reflect the real current
    // state before this node is reconfigured.
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


            const auto representativeSelection =
                DctIntervalFastEvaluator::
                    selectRepresentativeMetrics(
                        fastEvaluation,
                        options.representativeIntervalCount
                    );


            if (
                representativeSelection.
                    representatives.empty()
            )
            {
                continue;
            }


            const auto fullReport =
                DctIntervalFullValidator::
                    validate(
                        application,
                        inputImage,
                        roiMask,
                        withoutTarget,
                        node.nodeId,
                        signal,
                        unit,
                        toIntervals(
                            representativeSelection
                        )
                    );


            AttackStructureNode
                implementationNode =
                    node;


            implementationNode.unit =
                unit;


            implementationNode.monitorInput =
                signal;


            expanded.reserve(
                expanded.size()
                +
                fullReport.candidates.size()
            );


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


bool dominates(
    const DctMultiStateSearchState& first,
    const DctMultiStateSearchState& second,
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
            epsilon;


    return
        globalNoWorse
        &&
        roiNoWorse
        &&
        strictlyBetter;
}


std::vector<DctMultiStateSearchState>
buildParetoFront(
    const std::vector<DctMultiStateSearchState>& states,
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


bool betterFeasibleState(
    const DctMultiStateSearchState& first,
    const DctMultiStateSearchState& second,
    double epsilon
)
{
    // Global 只负责满足整体质量约束。
    // 一旦达到阈值，首要目标就是让 ROI PSNR 尽可能低。
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
    const DctMultiStateJointSearchOptions& options
)
{
    const bool firstFeasible =
        satisfiesQualityThresholds(
            first,
            options
        );


    const bool secondFeasible =
        satisfiesQualityThresholds(
            second,
            options
        );


    if (firstFeasible != secondFeasible)
    {
        return firstFeasible;
    }


    if (firstFeasible)
    {
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


    // 尚未达到整体质量阈值时，优先保留离阈值更近的状态，
    // 同时保留 ROI 更低的候选，给后续节点补偿留下机会。
    const double firstDeficit =
        qualityThresholdDeficit(
            first,
            options
        );


    const double secondDeficit =
        qualityThresholdDeficit(
            second,
            options
        );


    if (firstDeficit != secondDeficit)
    {
        return
            firstDeficit
            <
            secondDeficit;
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
    double minGlobal,
    double maxGlobal,
    double minRoi,
    double maxRoi
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


    const double globalDifference =
        firstGlobal
        -
        secondGlobal;


    const double roiDifference =
        firstRoi
        -
        secondRoi;


    return
        globalDifference
        *
        globalDifference
        +
        roiDifference
        *
        roiDifference;
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
    const DctMultiStateJointSearchOptions& options
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

            [&options](
                const auto& first,
                const auto& second
            )
            {
                return
                    beamStateComesFirst(
                        first,
                        second,
                        options
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


    std::size_t maxGlobalIndex =
        0;


    std::size_t minRoiIndex =
        0;


    std::size_t bestFeasibleIndex =
        front.size();


    std::size_t closestToFeasibleIndex =
        front.size();


    double closestDeficit =
        std::numeric_limits<double>::infinity();


    for (std::size_t index = 0;
         index < front.size();
         ++index)
    {
        const auto& metrics =
            front[index].metrics;


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
            satisfiesQualityThresholds(
                front[index],
                options
            );


        if (feasible)
        {
            if (
                bestFeasibleIndex == front.size()
                ||
                betterFeasibleState(
                    front[index],
                    front[
                        bestFeasibleIndex
                    ],
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
            const double totalDeficit =
                qualityThresholdDeficit(
                    front[index],
                    options
                );


            if (
                totalDeficit
                <
                closestDeficit
            )
            {
                closestDeficit =
                    totalDeficit;


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


    // 显式保护几类有用状态：
    // 1. Global 最高；
    // 2. ROI 最低；
    // 3. 当前满足 Global 阈值时 ROI 最低的状态；
    // 4. 尚未可行但距离 Global 阈值最近、后续可能补偿回来的状态。
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
                            minGlobal,
                            maxGlobal,
                            minRoi,
                            maxRoi
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

        [&options](
            const auto& first,
            const auto& second
        )
        {
            return
                beamStateComesFirst(
                    first,
                    second,
                    options
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

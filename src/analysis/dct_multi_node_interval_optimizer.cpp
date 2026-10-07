#include "analysis/dct_multi_node_interval_optimizer.hpp"

#include "analysis/dct_interval_fast_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>


namespace analysis
{

namespace
{

using IntervalKey =
    std::pair<int, int>;


struct TargetCurrentState
{
    bool exists = false;

    core::AttackConfig config{
        -1,
        approximate::ApproxUnitId::Add12se5RP,
        core::MonitorSignal::Input1,
        0,
        0
    };
};


bool sameInterval(
    const TriggerInterval& first,
    const TriggerInterval& second
)
{
    return
        first.lower == second.lower
        &&
        first.upper == second.upper;
}


bool sameInterval(
    const core::AttackConfig& config,
    const TriggerInterval& interval
)
{
    return
        config.lower == interval.lower
        &&
        config.upper == interval.upper;
}


void validateStructure(
    const AttackStructure& structure
)
{
    if (structure.empty())
    {
        throw std::runtime_error(
            "DCT multi-node interval optimizer received an empty structure."
        );
    }


    std::set<int>
        nodeIds;


    for (const auto& node : structure)
    {
        if (node.nodeId < 0)
        {
            throw std::runtime_error(
                "DCT multi-node interval optimizer received a negative node ID."
            );
        }


        if (
            !nodeIds.insert(
                node.nodeId
            ).second
        )
        {
            throw std::runtime_error(
                "DCT multi-node interval optimizer received duplicate node IDs."
            );
        }
    }
}


void validateOptions(
    const DctMultiNodeIntervalOptimizerOptions& options
)
{
    if (options.candidatesPerRole == 0)
    {
        throw std::runtime_error(
            "DCT multi-node optimizer candidatesPerRole must be greater than zero."
        );
    }


    if (options.maxAttackRounds == 0)
    {
        throw std::runtime_error(
            "DCT multi-node optimizer maxAttackRounds must be greater than zero."
        );
    }


    if (!std::isfinite(options.globalPsnrThreshold))
    {
        throw std::runtime_error(
            "DCT multi-node optimizer Global PSNR threshold is not finite."
        );
    }


    if (
        !std::isfinite(
            options.improvementEpsilon
        )
        ||
        options.improvementEpsilon < 0.0
    )
    {
        throw std::runtime_error(
            "DCT multi-node optimizer improvement epsilon is invalid."
        );
    }
}


AttackConfiguration removeTarget(
    const AttackConfiguration& configuration,
    int nodeId,
    TargetCurrentState& targetState
)
{
    AttackConfiguration
        result;


    result.reserve(
        configuration.size()
    );


    targetState =
        TargetCurrentState{};


    for (const auto& config : configuration)
    {
        if (config.nodeId == nodeId)
        {
            if (targetState.exists)
            {
                throw std::runtime_error(
                    "DCT multi-node optimizer found duplicate target configs."
                );
            }


            targetState.exists =
                true;


            targetState.config =
                config;


            continue;
        }


        result.push_back(
            config
        );
    }


    return result;
}


void setTarget(
    AttackConfiguration& configuration,
    const AttackStructureNode& node,
    const TriggerInterval& interval
)
{
    for (auto& config : configuration)
    {
        if (config.nodeId != node.nodeId)
        {
            continue;
        }


        config.unit =
            node.unit;


        config.monitorInput =
            node.monitorInput;


        config.lower =
            interval.lower;


        config.upper =
            interval.upper;


        return;
    }


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


std::vector<TriggerInterval>
buildCandidatePool(
    const DctIntervalFastEvaluation& evaluation,
    std::size_t candidatesPerRole,
    const TargetCurrentState& targetState
)
{
    const auto roiAttack =
        DctIntervalFastEvaluator::
            selectTopMetrics(
                evaluation,
                DctIntervalFastRanking::RoiAttack,
                candidatesPerRole
            );


    const auto compensation =
        DctIntervalFastEvaluator::
            selectTopMetrics(
                evaluation,
                DctIntervalFastRanking::NonRoiCompensation,
                candidatesPerRole
            );


    const auto redistribution =
        DctIntervalFastEvaluator::
            selectTopMetrics(
                evaluation,
                DctIntervalFastRanking::Redistribution,
                candidatesPerRole
            );


    std::vector<TriggerInterval>
        intervals;


    std::set<IntervalKey>
        seen;


    const auto addMetrics =
        [&](
            const std::vector<
                DctIntervalFastMetric
            >& metrics
        )
        {
            for (const auto& metric : metrics)
            {
                const IntervalKey key{
                    metric.interval.lower,
                    metric.interval.upper
                };


                if (
                    seen.insert(
                        key
                    ).second
                )
                {
                    intervals.push_back(
                        metric.interval
                    );
                }
            }
        };


    addMetrics(
        roiAttack
    );


    addMetrics(
        compensation
    );


    addMetrics(
        redistribution
    );


    // 当前区间必须保留为候选。
    //
    // 这样一次节点更新至少可以“保持原状”，
    // 不会因为快速预筛选把当前可行解删掉。
    if (targetState.exists)
    {
        const TriggerInterval currentInterval{
            targetState.config.lower,
            targetState.config.upper
        };


        const IntervalKey key{
            currentInterval.lower,
            currentInterval.upper
        };


        if (
            seen.insert(
                key
            ).second
        )
        {
            intervals.push_back(
                currentInterval
            );
        }
    }


    return intervals;
}


std::size_t findIntervalIndex(
    const std::vector<TriggerInterval>& intervals,
    const TriggerInterval& interval
)
{
    for (std::size_t index = 0;
         index < intervals.size();
         ++index)
    {
        if (
            sameInterval(
                intervals[index],
                interval
            )
        )
        {
            return index;
        }
    }


    throw std::runtime_error(
        "DCT multi-node optimizer could not find the current interval."
    );
}


bool isBetterCompensation(
    const DctImageQualityMetrics& candidate,
    const DctImageQualityMetrics& best,
    double epsilon
)
{
    if (
        candidate.globalPsnr
        >
        best.globalPsnr
        +
        epsilon
    )
    {
        return true;
    }


    if (
        std::abs(
            candidate.globalPsnr
            -
            best.globalPsnr
        )
        <=
        epsilon
        &&
        candidate.nonRoiPsnr
        >
        best.nonRoiPsnr
        +
        epsilon
    )
    {
        return true;
    }


    return false;
}


bool isBetterFeasibleAttack(
    const DctImageQualityMetrics& candidate,
    const DctImageQualityMetrics& best,
    double epsilon
)
{
    if (
        candidate.roiPsnr
        <
        best.roiPsnr
        -
        epsilon
    )
    {
        return true;
    }


    if (
        std::abs(
            candidate.roiPsnr
            -
            best.roiPsnr
        )
        <=
        epsilon
        &&
        candidate.globalPsnr
        >
        best.globalPsnr
        +
        epsilon
    )
    {
        return true;
    }


    return false;
}


DctMultiNodeOptimizationStep optimizeOneNode(
    const applications::DctApplication& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const AttackStructureNode& node,
    AttackConfiguration& currentConfiguration,
    const DctMultiNodeIntervalOptimizerOptions& options,
    std::size_t round,
    bool compensationPhase
)
{
    TargetCurrentState
        targetState;


    // 采样与候选完整验证都从“暂时移除目标节点”的状态开始。
    //
    // 这样：
    // - 其它节点当前影响继续保留；
    // - 目标节点不会被自己的旧区间锁住输入分布；
    // - 每个候选在完整 DCT 中重新加入目标节点后，
    //   仍会真实体现其自身以及后续动态调用产生的传播影响。
    const AttackConfiguration
        withoutTarget =
            removeTarget(
                currentConfiguration,
                node.nodeId,
                targetState
            );


    std::vector<core::AddSample>
        samples;


    application.collectConfiguredAddSamples(
        inputImage,
        roiMask,
        withoutTarget,
        samples
    );


    const auto fastEvaluation =
        DctIntervalFastEvaluator::
            evaluateFromSamples(
                samples,
                node.nodeId,
                node.monitorInput,
                node.unit
            );


    const auto candidateIntervals =
        buildCandidatePool(
            fastEvaluation,
            options.candidatesPerRole,
            targetState
        );


    const auto fullReport =
        DctIntervalFullValidator::
            validate(
                application,
                inputImage,
                roiMask,
                withoutTarget,
                node.nodeId,
                node.monitorInput,
                node.unit,
                candidateIntervals
            );


    DctMultiNodeOptimizationStep
        step;


    step.round =
        round;


    step.nodeId =
        node.nodeId;


    step.compensationPhase =
        compensationPhase;


    step.hadPreviousInterval =
        targetState.exists;


    step.fastCandidateCount =
        fastEvaluation.metrics.size();


    step.fullCandidateCount =
        candidateIntervals.size();


    // 目标节点关闭时的完整 DCT 指标。
    const DctImageQualityMetrics
        targetOffMetrics =
            fullReport.currentMetrics;


    DctImageQualityMetrics
        incumbentMetrics =
            targetOffMetrics;


    std::optional<std::size_t>
        incumbentCandidateIndex;


    if (targetState.exists)
    {
        const TriggerInterval currentInterval{
            targetState.config.lower,
            targetState.config.upper
        };


        step.previousInterval =
            currentInterval;


        const std::size_t currentIndex =
            findIntervalIndex(
                candidateIntervals,
                currentInterval
            );


        incumbentCandidateIndex =
            currentIndex;


        incumbentMetrics =
            fullReport.candidates[
                currentIndex
            ].metrics;
    }


    if (compensationPhase)
    {
        // 补偿初始化允许选择“目标节点保持关闭”。
        //
        // 只有完整 DCT 的 Global PSNR 真正得到改善，
        // 才会启用该节点的补偿区间。
        DctImageQualityMetrics
            bestMetrics =
                targetOffMetrics;


        std::optional<std::size_t>
            bestIndex;


        for (std::size_t index = 0;
             index < fullReport.candidates.size();
             ++index)
        {
            const auto& candidate =
                fullReport.candidates[
                    index
                ];


            if (
                isBetterCompensation(
                    candidate.metrics,
                    bestMetrics,
                    options.improvementEpsilon
                )
            )
            {
                bestMetrics =
                    candidate.metrics;


                bestIndex =
                    index;
            }
        }


        step.beforeMetrics =
            incumbentMetrics;


        if (bestIndex.has_value())
        {
            const auto& selected =
                fullReport.candidates[
                    *bestIndex
                ];


            step.selectedInterval =
                selected.interval;


            step.afterMetrics =
                selected.metrics;


            // 初始补偿阶段当前通常没有目标节点配置。
            //
            // 若将来从已有配置调用，该逻辑仍只在结果确实变化时更新。
            const bool sameAsCurrent =
                targetState.exists
                &&
                sameInterval(
                    targetState.config,
                    selected.interval
                );


            if (!sameAsCurrent)
            {
                setTarget(
                    currentConfiguration,
                    node,
                    selected.interval
                );


                step.changed =
                    true;
            }
        }
        else
        {
            // 保持目标节点关闭。
            step.afterMetrics =
                targetOffMetrics;


            if (targetState.exists)
            {
                // 补偿初始化目前从空配置开始，不会走到这里。
                // 保留语义完整：如果调用方未来带已有配置，
                // bestIndex 为空意味着关闭目标节点更好。
                currentConfiguration =
                    withoutTarget;


                step.changed =
                    true;
            }
        }


        return step;
    }


    // =====================================================
    // ROI 攻击阶段
    // =====================================================

    step.beforeMetrics =
        incumbentMetrics;


    const bool incumbentFeasible =
        incumbentMetrics.globalPsnr
        >=
        options.globalPsnrThreshold;


    std::optional<std::size_t>
        bestIndex;


    DctImageQualityMetrics
        bestMetrics =
            incumbentMetrics;


    bool bestFeasible =
        incumbentFeasible;


    for (std::size_t index = 0;
         index < fullReport.candidates.size();
         ++index)
    {
        const auto& candidate =
            fullReport.candidates[
                index
            ];


        const bool candidateFeasible =
            candidate.metrics.globalPsnr
            >=
            options.globalPsnrThreshold;


        if (bestFeasible)
        {
            if (!candidateFeasible)
            {
                continue;
            }


            if (
                isBetterFeasibleAttack(
                    candidate.metrics,
                    bestMetrics,
                    options.improvementEpsilon
                )
            )
            {
                bestMetrics =
                    candidate.metrics;


                bestIndex =
                    index;
            }


            continue;
        }


        // 当前状态尚不满足全局约束：
        // 一旦发现可行候选，立即切换到“可行解优先”。
        if (candidateFeasible)
        {
            bestFeasible =
                true;


            bestMetrics =
                candidate.metrics;


            bestIndex =
                index;


            continue;
        }


        // 所有候选仍不可行时，优先恢复 Global PSNR。
        if (
            candidate.metrics.globalPsnr
            >
            bestMetrics.globalPsnr
            +
            options.improvementEpsilon
        )
        {
            bestMetrics =
                candidate.metrics;


            bestIndex =
                index;
        }
    }


    if (bestIndex.has_value())
    {
        const auto& selected =
            fullReport.candidates[
                *bestIndex
            ];


        step.selectedInterval =
            selected.interval;


        step.afterMetrics =
            selected.metrics;


        const bool sameAsCurrent =
            targetState.exists
            &&
            sameInterval(
                targetState.config,
                selected.interval
            );


        if (!sameAsCurrent)
        {
            setTarget(
                currentConfiguration,
                node,
                selected.interval
            );


            step.changed =
                true;
        }
    }
    else
    {
        // 没有候选严格优于当前状态。
        step.afterMetrics =
            incumbentMetrics;


        if (targetState.exists)
        {
            step.selectedInterval =
            {
                targetState.config.lower,
                targetState.config.upper
            };
        }
    }


    return step;
}

}


// =========================================================
// 单张图片：固定硬件结构下的多节点区间联合优化
// =========================================================

DctMultiNodeIntervalOptimizationResult
DctMultiNodeIntervalOptimizer::optimize(
    const applications::DctApplication& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const AttackStructure& structure,
    const DctMultiNodeIntervalOptimizerOptions& options
)
{
    validateStructure(
        structure
    );


    validateOptions(
        options
    );


    DctMultiNodeIntervalOptimizationResult
        result;


    result.initialMetrics =
        DctIntervalFullValidator::
            evaluateConfiguration(
                application,
                inputImage,
                roiMask,
                {}
            );


    AttackConfiguration
        currentConfiguration;


    // =====================================================
    // Phase 1：补偿初始化
    // =====================================================
    //
    // 目的不是最终决定节点角色，而是先让真正有补偿能力的
    // unit 在完整 DCT 中释放一些全局误差预算。
    for (const auto& node : structure)
    {
        auto step =
            optimizeOneNode(
                application,
                inputImage,
                roiMask,
                node,
                currentConfiguration,
                options,
                0,
                true
            );


        result.steps.push_back(
            std::move(
                step
            )
        );
    }


    // =====================================================
    // Phase 2：在全局约束下逐节点降低 ROI PSNR
    // =====================================================
    for (std::size_t round = 1;
         round <= options.maxAttackRounds;
         ++round)
    {
        bool changedInRound =
            false;


        for (const auto& node : structure)
        {
            auto step =
                optimizeOneNode(
                    application,
                    inputImage,
                    roiMask,
                    node,
                    currentConfiguration,
                    options,
                    round,
                    false
                );


            changedInRound =
                changedInRound
                ||
                step.changed;


            result.steps.push_back(
                std::move(
                    step
                )
            );
        }


        result.completedAttackRounds =
            round;


        if (!changedInRound)
        {
            break;
        }
    }


    result.configuration =
        currentConfiguration;


    result.finalMetrics =
        DctIntervalFullValidator::
            evaluateConfiguration(
                application,
                inputImage,
                roiMask,
                result.configuration
            );


    return result;
}

}

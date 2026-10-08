#pragma once

#include "analysis/dct_interval_full_validator.hpp"
#include "analysis/two_stage_search.hpp"
#include "core/application.hpp"

#include <cstddef>
#include <functional>
#include <vector>


namespace analysis
{

struct DctMultiStateJointSearchOptions
{
    // Candidate approximate implementations used for error redistribution.
    //
    // Empty: keep the legacy behavior and use AttackStructureNode::unit only.
    // Non-empty: each node jointly searches implementation x interval.
    std::vector<approximate::ApproxUnitId>
        redistributionUnits;


    // 每个“当前状态 + 目标节点”最多保留多少个快速代表区间，
    // 再交给完整应用。
    std::size_t representativeIntervalCount = 20;


    // 每处理完一个节点后，最多保留多少个不同
    // Global / ROI 权衡状态。
    std::size_t beamWidth = 20;


    // 第一遍所有节点处理完成后，再做多少轮逐节点 refinement。
    // refinement 时目标节点先暂时移除，其余节点保持当前配置，
    // 再重新采样并重新找该节点区间。
    std::size_t refinementRounds = 1;


    // 最终“可行解”的全图质量下限。
    // 中间状态不会因为低于该阈值而直接删除，
    // 以保留后续节点补偿回来的可能性。
    double globalPsnrThreshold = 30.0;


    // 最终“可行解”的 Non-ROI 质量下限。
    // 该约束用于避免“全图一起恶化”却仍因 ROI 较低而被选中。
    double nonRoiPsnrThreshold = 30.0;


    double comparisonEpsilon = 1.0e-9;
};


struct DctMultiStateSearchState
{
    AttackConfiguration configuration;

    DctImageQualityMetrics metrics;
};


struct DctMultiStateSearchLayer
{
    std::size_t passIndex = 0;
    std::size_t nodeIndex = 0;

    int nodeId = -1;

    bool refinement = false;

    std::size_t inputStateCount = 0;
    std::size_t expandedStateCount = 0;
    std::size_t paretoStateCount = 0;
    std::size_t retainedStateCount = 0;
};


struct DctMultiStateJointSearchProgress
{
    std::size_t passIndex = 0;
    std::size_t nodeIndex = 0;

    int nodeId = -1;

    bool refinement = false;

    std::size_t completedStates = 0;
    std::size_t totalStates = 0;
};


using DctMultiStateJointSearchProgressCallback =
    std::function<
        void(
            const DctMultiStateJointSearchProgress&
        )
    >;


struct DctMultiStateJointSearchResult
{
    DctImageQualityMetrics initialMetrics;

    std::vector<DctMultiStateSearchLayer>
        layers;


    std::vector<DctMultiStateSearchState>
        finalStates;


    bool hasBestFeasibleState = false;

    DctMultiStateSearchState
        bestFeasibleState;
};


class DctMultiStateJointSearch
{
public:

    // Module 3-B：
    // 给定节点与 MonitorSignal，在单张图片上联合搜索
    // 各节点的误差重分布配置。
    //
    // 当 redistributionUnits 非空时，每个节点同时搜索
    // “近似实现 + 区间”，无需预先固定某一个近似实现。
    //
    // 与旧贪心优化器不同：
    //
    // 1. 每一步保留多个 Global / ROI / Non-ROI 不同权衡状态；
    //    Global / Non-ROI 用于质量约束，满足后主要追求更低 ROI；
    // 2. 中间状态不强制 Global PSNR >= threshold；
    // 3. 每扩展一个节点，都基于“该状态当前真实运行”
    //    重新采集目标节点 monitor signal；
    // 4. 目标节点已有区间时，refinement 会暂时移除它，
    //    保留其它节点后重新生成区间候选；
    // 5. Module 3-A 的代表区间只负责预筛，
    //    所有保留候选最终都真正跑完整应用。
    //
    // structure 可包含任意数量节点；当前课题后续主要使用 1~5 个。
    static DctMultiStateJointSearchResult search(
        const core::Application& application,
        const cv::Mat& inputImage,
        const cv::Mat& roiMask,
        const AttackStructure& structure,
        const DctMultiStateJointSearchOptions& options =
            DctMultiStateJointSearchOptions{},
        const DctMultiStateJointSearchProgressCallback& progressCallback =
            {}
    );
};

}

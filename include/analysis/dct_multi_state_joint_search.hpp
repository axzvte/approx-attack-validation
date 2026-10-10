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


    // Candidate monitor signals searched jointly for every node.
    //
    // Empty: keep AttackStructureNode::monitorInput only.
    // Non-empty: search monitor signal x implementation x interval together.
    std::vector<core::MonitorSignal>
        monitorSignals;


    // 当 monitor 的不同取值很多时，主联合搜索不再枚举全部
    // n*(n+1)/2 个区间，而是先从当前图片动态样本中挑选少量边界。
    //
    // 0：保持旧行为，使用全部 observed values；
    // >0：最多保留该数量的候选边界，再组合连续区间。
    std::size_t candidateBoundaryCount = 24;


    // 每个“当前状态 + 目标节点 + monitor + implementation”
    // 先在快速阶段最多保留多少个局部代表区间。
    //
    // 这些候选不会立即全部运行完整图像，而是先与其它
    // monitor / implementation 的候选合并后统一竞争。
    std::size_t representativeIntervalCount = 20;


    // 将所有 monitor x implementation 的快速代表区间合并以后，
    // 每个“当前状态 + 目标节点”最多有多少个候选进入完整图像验证。
    //
    // 例如 3 monitor x 9 unit x 4 local representatives = 108 个
    // 快速候选，可以统一压缩到 16 个再真正运行完整应用。
    std::size_t fullValidationCandidateCount = 16;


    // 每处理完一个节点后，最多保留多少个不同
    // Global / ROI 权衡状态。
    std::size_t beamWidth = 20;


    // 第一遍所有节点处理完成后，再做多少轮逐节点 refinement。
    // refinement 时目标节点先暂时移除，其余节点保持当前配置，
    // 再重新采样并重新找该节点区间。
    std::size_t refinementRounds = 1;


    // 最终整体质量下限。
    //
    // 最终可行解还必须满足：
    //   ΔMSE_ROI > ΔMSE_NonROI
    // 即相对当前 Application Baseline，新增误差更多地进入 ROI。
    //
    // 中间状态不会因为暂时不满足 Global 或区域选择性而直接删除，
    // 以保留后续节点补偿 / 重分布回来的可能性。
    double globalPsnrThreshold = 30.0;


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
    // 1. 每一步保留多个 Global / ROI / 区域重分布不同权衡状态；
    //    最终要求 Global 达标且 ΔMSE_ROI > ΔMSE_NonROI，
    //    在此基础上主要追求更低 ROI；
    // 2. 中间状态不强制 Global PSNR >= threshold；
    // 3. 每扩展一个节点，都基于“该状态当前真实运行”
    //    重新采集目标节点 monitor signal；
    // 4. 目标节点已有区间时，refinement 会暂时移除它，
    //    保留其它节点后重新生成区间候选；
    // 5. Module 3-A 先为每个 monitor / implementation 生成局部代表区间；
    // 6. 所有局部代表区间再统一按 ROI / Global / 重分布快速指标竞争，
    //    只有少量候选真正运行完整应用。
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

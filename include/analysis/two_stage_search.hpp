#pragma once

#include "analysis/brute_force_search.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>


namespace analysis
{

struct ImageCase
{
    std::string inputPath;
    std::string roiMaskPath;
};


struct TwoStageDataset
{
    std::vector<ImageCase>
        stage1Images;

    std::vector<ImageCase>
        stage2Images;
};


struct AttackStructureNode
{
    int nodeId;

    approximate::ApproxUnitId
        unit;

    core::MonitorInput
        monitorInput;
};


using AttackStructure =
    std::vector<AttackStructureNode>;


using StageImageCallback =
    std::function<
        void(
            std::uint64_t configurationIndex,
            const AttackConfiguration& configuration,
            std::size_t imageIndex,
            const ImageCase& imageCase
        )
    >;


class TwoStageSearch
{
public:

    static constexpr std::size_t
        kDefaultImagesPerStage =
            10;


    // 检查：
    // 1. 第一阶段是否为 10 张
    // 2. 第二阶段是否为 10 张
    // 3. 两阶段输入图像是否互不重复
    // 4. 图像路径与 ROI 路径是否为空
    static void validateDataset(
        const TwoStageDataset& dataset,
        std::size_t expectedImagesPerStage =
            kDefaultImagesPerStage
    );


    // Stage 1：
    // 完整搜索
    // 节点位置 × 攻击近似加法器 × monitor input × 区间
    static std::uint64_t countStage1Configurations(
        const BruteForceSearchSpace& stage1SearchSpace
    );


    static void enumerateStage1(
        const BruteForceSearchSpace& stage1SearchSpace,
        const ConfigurationCallback& callback
    );


    // 真正执行 Stage 1 的多图流程：
    // 每一个完整攻击配置都会依次作用于第一阶段的 10 张图。
    static void runStage1(
        const TwoStageDataset& dataset,
        const BruteForceSearchSpace& stage1SearchSpace,
        const StageImageCallback& callback
    );


    // 从 Stage 1 的完整配置中提取硬件结构。
    //
    // 区间 [lower, upper] 不属于结构本身，
    // 因此不会保留。
    static AttackStructure extractStructure(
        const AttackConfiguration& configuration
    );


    // Stage 2：
    // 固定第一阶段选择的：
    // 1. 节点数量
    // 2. 节点位置
    // 3. 攻击近似加法器
    // 4. monitor input
    //
    // 只重新搜索各节点的触发区间。
    static BruteForceSearchSpace
    buildStage2SearchSpace(
        const BruteForceSearchSpace& stage1SearchSpace,
        const AttackStructure& structure
    );


    static std::uint64_t countStage2Configurations(
        const BruteForceSearchSpace& stage1SearchSpace,
        const AttackStructure& structure
    );


    static void enumerateStage2(
        const BruteForceSearchSpace& stage1SearchSpace,
        const AttackStructure& structure,
        const ConfigurationCallback& callback
    );


    // 真正执行 Stage 2 的多图流程：
    // 固定结构后，每一个区间组合依次作用于第二阶段的 10 张图。
    static void runStage2(
        const TwoStageDataset& dataset,
        const BruteForceSearchSpace& stage1SearchSpace,
        const AttackStructure& structure,
        const StageImageCallback& callback
    );
};

}

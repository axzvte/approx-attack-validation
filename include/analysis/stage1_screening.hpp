#pragma once

#include "analysis/two_stage_search.hpp"

#include <cstddef>
#include <functional>
#include <vector>


namespace analysis
{

struct Stage1ImageScore
{
    // false 表示该配置在当前图像上不满足约束，
    // 例如全局质量低于允许阈值。
    bool valid = false;

    // 统一约定：score 越大越好。
    //
    // 不同应用可以自行定义 score：
    // Sharpen / DCT 可以基于 ROI 与 non-ROI 的 PSNR/MSE 差异，
    // Sobel / Conv 可以基于 MED 差异。
    double score = 0.0;
};


using Stage1Evaluator =
    std::function<
        Stage1ImageScore(
            const AttackConfiguration& configuration,
            std::size_t imageIndex,
            const ImageCase& imageCase
        )
    >;


struct Stage1ScreeningOptions
{
    // 第一阶段默认要求 10 张图全部通过约束。
    std::size_t minimumValidImages = 10;

    // 为了不把不同硬件规模直接混在一起比较，
    // 每一种攻击节点数量分别保留若干套结构。
    std::size_t topStructuresPerNodeCount = 5;
};


struct Stage1SelectedStructure
{
    AttackStructure structure;

    // 该结构在 Stage 1 中表现最好的完整配置。
    // 包含当时对应的区间，便于回看第一阶段结果。
    AttackConfiguration
        bestConfiguration;

    std::size_t validImageCount = 0;

    double meanScore = 0.0;

    double worstScore = 0.0;
};


class Stage1Screening
{
public:

    // 第一阶段筛选逻辑：
    //
    // 1. 枚举全部完整配置
    // 2. 每个配置依次在 10 张 Stage 1 图像上评价
    // 3. 不满足 minimumValidImages 的配置淘汰
    // 4. 对同一硬件结构，只保留表现最好的区间配置
    // 5. 按攻击节点数量分组，各组分别保留 Top-K 结构
    //
    // 这样不会直接把 1 节点结构和 4 节点结构混在一起排序，
    // 后续可以再结合实际综合硬件开销进行取舍。
    static std::vector<Stage1SelectedStructure>
    screen(
        const TwoStageDataset& dataset,
        const BruteForceSearchSpace& searchSpace,
        const Stage1Evaluator& evaluator,
        const Stage1ScreeningOptions& options = {}
    );
};

}

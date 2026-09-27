#include "analysis/two_stage_search.hpp"

#include "approximate/evoapprox_adapter.hpp"
#include "core/attack_config.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>


int main()
{
    // =====================================================
    // 10 + 10 数据集检查
    // =====================================================

    analysis::TwoStageDataset
        dataset;


    for (int i = 0; i < 10; ++i)
    {
        dataset.stage1Images.push_back(
            {
                "stage1_image_"
                    +
                    std::to_string(i)
                    +
                    ".png",

                "stage1_mask_"
                    +
                    std::to_string(i)
                    +
                    ".png"
            }
        );


        dataset.stage2Images.push_back(
            {
                "stage2_image_"
                    +
                    std::to_string(i)
                    +
                    ".png",

                "stage2_mask_"
                    +
                    std::to_string(i)
                    +
                    ".png"
            }
        );
    }


    analysis::TwoStageSearch::validateDataset(
        dataset
    );


    // =====================================================
    // Stage 1 搜索空间
    // =====================================================

    analysis::BruteForceSearchSpace
        stage1SearchSpace;


    analysis::NodeSearchSpace
        node0;


    node0.nodeId =
        0;


    node0.attackUnits =
    {
        approximate::ApproxUnitId::Add12se5Z0,
        approximate::ApproxUnitId::Add12se5QT
    };


    node0.monitorSpaces =
    {
        {
            core::MonitorInput::Input1,
            {
                { 0, 10 },
                { 11, 20 }
            }
        },

        {
            core::MonitorInput::Input2,
            {
                { 21, 30 },
                { 31, 40 },
                { 41, 50 }
            }
        }
    };


    analysis::NodeSearchSpace
        node1;


    node1.nodeId =
        1;


    node1.attackUnits =
    {
        approximate::ApproxUnitId::Add12se5SB,
        approximate::ApproxUnitId::Add12se5TE
    };


    node1.monitorSpaces =
    {
        {
            core::MonitorInput::Input1,
            {
                { -20, -10 },
                { -9, 0 }
            }
        },

        {
            core::MonitorInput::Input2,
            {
                { 100, 110 }
            }
        }
    };


    stage1SearchSpace.nodes =
    {
        node0,
        node1
    };


    stage1SearchSpace.minAttackNodes =
        1;


    stage1SearchSpace.maxAttackNodes =
        2;


    // Node 0:
    // 2 units * (2 + 3 intervals) = 10
    //
    // Node 1:
    // 2 units * (2 + 1 intervals) = 6
    //
    // Stage 1:
    // 10 + 6 + 10*6 = 76
    const std::uint64_t
        stage1Count =
            analysis::TwoStageSearch::
                countStage1Configurations(
                    stage1SearchSpace
                );


    if (stage1Count != 76)
    {
        std::cerr
            << "Unexpected Stage 1 count: "
            << stage1Count
            << ".\n";

        return 1;
    }


    // =====================================================
    // 模拟 Stage 1 中选中的一条好配置
    // =====================================================

    const analysis::AttackConfiguration
        selectedStage1Configuration =
    {
        {
            0,
            approximate::ApproxUnitId::Add12se5Z0,
            core::MonitorInput::Input2,
            21,
            30
        },

        {
            1,
            approximate::ApproxUnitId::Add12se5SB,
            core::MonitorInput::Input1,
            -20,
            -10
        }
    };


    const analysis::AttackStructure
        structure =
            analysis::TwoStageSearch::
                extractStructure(
                    selectedStage1Configuration
                );


    if (structure.size() != 2)
    {
        std::cerr
            << "Unexpected extracted structure size.\n";

        return 1;
    }


    // =====================================================
    // Stage 2
    //
    // 固定：
    // Node 0 + 5Z0 + Input2
    // Node 1 + 5SB + Input1
    //
    // 只重新搜索区间：
    //
    // Node 0 Input2 = 3 个区间
    // Node 1 Input1 = 2 个区间
    //
    // 完整笛卡尔积 = 3 * 2 = 6
    // =====================================================

    const std::uint64_t
        stage2Count =
            analysis::TwoStageSearch::
                countStage2Configurations(
                    stage1SearchSpace,
                    structure
                );


    if (stage2Count != 6)
    {
        std::cerr
            << "Unexpected Stage 2 count: "
            << stage2Count
            << ".\n";

        return 1;
    }


    std::uint64_t
        emittedStage2 =
            0;


    analysis::TwoStageSearch::enumerateStage2(
        stage1SearchSpace,
        structure,

        [&](
            const analysis::AttackConfiguration&
                configuration
        )
        {
            ++emittedStage2;


            if (configuration.size() != 2)
            {
                throw std::runtime_error(
                    "Stage 2 configuration must keep exactly two selected nodes."
                );
            }


            const auto& first =
                configuration[0];


            const auto& second =
                configuration[1];


            if (
                first.nodeId != 0
                ||
                first.unit
                    !=
                    approximate::ApproxUnitId::Add12se5Z0
                ||
                first.monitorInput
                    !=
                    core::MonitorInput::Input2
            )
            {
                throw std::runtime_error(
                    "Stage 2 changed the fixed structure of node 0."
                );
            }


            if (
                second.nodeId != 1
                ||
                second.unit
                    !=
                    approximate::ApproxUnitId::Add12se5SB
                ||
                second.monitorInput
                    !=
                    core::MonitorInput::Input1
            )
            {
                throw std::runtime_error(
                    "Stage 2 changed the fixed structure of node 1."
                );
            }
        }
    );


    if (emittedStage2 != 6)
    {
        std::cerr
            << "Unexpected Stage 2 emitted count: "
            << emittedStage2
            << ".\n";

        return 1;
    }


    std::cout
        << "Stage 1 images: "
        << dataset.stage1Images.size()
        << "\n";


    std::cout
        << "Stage 2 images: "
        << dataset.stage2Images.size()
        << "\n";


    std::cout
        << "Stage 1 configurations: "
        << stage1Count
        << "\n";


    std::cout
        << "Stage 2 interval-only configurations: "
        << stage2Count
        << "\n";


    std::cout
        << "TwoStageSearch test passed.\n";


    return 0;
}

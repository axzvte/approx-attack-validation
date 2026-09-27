#include "analysis/stage1_screening.hpp"

#include "approximate/evoapprox_adapter.hpp"
#include "core/attack_config.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>


int main()
{
    // =====================================================
    // 10 + 10 数据集
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


    // =====================================================
    // 两个候选节点
    //
    // 每个节点：
    // 1 个攻击加法器
    // 1 个 monitor input
    // 2 个区间
    // =====================================================

    analysis::NodeSearchSpace
        node0;


    node0.nodeId =
        0;


    node0.attackUnits =
    {
        approximate::ApproxUnitId::Add12se5Z0
    };


    node0.monitorSpaces =
    {
        {
            core::MonitorInput::Input1,
            {
                { 0, 9 },
                { 10, 19 }
            }
        }
    };


    analysis::NodeSearchSpace
        node1;


    node1.nodeId =
        1;


    node1.attackUnits =
    {
        approximate::ApproxUnitId::Add12se5SB
    };


    node1.monitorSpaces =
    {
        {
            core::MonitorInput::Input2,
            {
                { 0, 9 },
                { 10, 19 }
            }
        }
    };


    analysis::BruteForceSearchSpace
        searchSpace;


    searchSpace.nodes =
    {
        node0,
        node1
    };


    searchSpace.minAttackNodes =
        1;


    searchSpace.maxAttackNodes =
        2;


    analysis::Stage1ScreeningOptions
        options;


    // 每一种攻击节点数量只保留 1 套结构。
    options.topStructuresPerNodeCount =
        1;


    // 10 张图必须全部有效。
    options.minimumValidImages =
        10;


    // =====================================================
    // 模拟评价器
    //
    // 这里只验证筛选框架，不使用真实图像指标。
    //
    // 规则：
    // 1 节点：
    //   Node 0 优于 Node 1
    //   且 Node 0 的 [10,19] 优于 [0,9]
    //
    // 2 节点：
    //   两个节点都选择 [10,19] 时最好
    // =====================================================

    const auto selected =
        analysis::Stage1Screening::screen(
            dataset,
            searchSpace,

            [](
                const analysis::AttackConfiguration&
                    configuration,
                std::size_t,
                const analysis::ImageCase&
            )
            {
                analysis::Stage1ImageScore
                    result;


                result.valid =
                    true;


                if (configuration.size() == 1)
                {
                    const auto& config =
                        configuration[0];


                    result.score =
                        (
                            config.nodeId == 0
                            ?
                            20.0
                            :
                            10.0
                        )
                        +
                        (
                            config.lower == 10
                            ?
                            2.0
                            :
                            0.0
                        );


                    return result;
                }


                double score =
                    30.0;


                for (const auto& config : configuration)
                {
                    if (config.lower == 10)
                    {
                        score +=
                            1.0;
                    }
                }


                result.score =
                    score;


                return result;
            },

            options
        );


    // 应该分别保留：
    // 1 节点 Top-1
    // 2 节点 Top-1
    if (selected.size() != 2)
    {
        std::cerr
            << "Expected two selected structures, got "
            << selected.size()
            << ".\n";

        return 1;
    }


    const auto& oneNode =
        selected[0];


    if (
        oneNode.structure.size() != 1
        ||
        oneNode.structure[0].nodeId != 0
        ||
        oneNode.bestConfiguration.size() != 1
        ||
        oneNode.bestConfiguration[0].lower != 10
        ||
        oneNode.bestConfiguration[0].upper != 19
        ||
        oneNode.validImageCount != 10
    )
    {
        std::cerr
            << "Single-node Stage 1 screening result is incorrect.\n";

        return 1;
    }


    const auto& twoNode =
        selected[1];


    if (
        twoNode.structure.size() != 2
        ||
        twoNode.bestConfiguration.size() != 2
        ||
        twoNode.bestConfiguration[0].lower != 10
        ||
        twoNode.bestConfiguration[1].lower != 10
        ||
        twoNode.validImageCount != 10
    )
    {
        std::cerr
            << "Two-node Stage 1 screening result is incorrect.\n";

        return 1;
    }


    std::cout
        << "Selected one-node mean score: "
        << oneNode.meanScore
        << "\n";


    std::cout
        << "Selected two-node mean score: "
        << twoNode.meanScore
        << "\n";


    std::cout
        << "Stage1Screening test passed.\n";


    return 0;
}

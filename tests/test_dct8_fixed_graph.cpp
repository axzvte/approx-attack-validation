#include "applications/dct8_fixed_graph.hpp"

#include "approximate/evoapprox_adapter.hpp"
#include "core/attack_config.hpp"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>


int main()
{
    applications::Dct8FixedGraph graph;


    const applications::Dct8FixedGraph::Vector input =
    {
        -128,
        -64,
        -32,
        0,
        16,
        32,
        64,
        127
    };


    // =====================================================
    // 精确 DCT 与 OpenCV 比较
    // =====================================================

    applications::Dct8FixedGraph::Trace
        exactTrace{};


    const auto exactOutput =
        graph.runExact(
            input,
            &exactTrace
        );


    cv::Mat cvInput(
        1,
        8,
        CV_64F
    );


    for (int i = 0; i < 8; ++i)
    {
        cvInput.at<double>(0, i) =
            static_cast<double>(
                input[i]
            );
    }


    cv::Mat cvOutput;


    cv::dct(
        cvInput,
        cvOutput
    );


    double maximumDifference =
        0.0;


    for (int i = 0; i < 8; ++i)
    {
        const double difference =
            std::abs(
                static_cast<double>(
                    exactOutput[i]
                )
                -
                cvOutput.at<double>(0, i)
            );


        maximumDifference =
            std::max(
                maximumDifference,
                difference
            );
    }


    if (maximumDifference > 2.0)
    {
        std::cerr
            << "Fixed DCT error is too large.\n";

        return 1;
    }


    // =====================================================
    // 检查32个位置
    // =====================================================

    for (int i = 0;
         i < applications::Dct8FixedGraph::kAddNodeCount;
         ++i)
    {
        if (exactTrace[i].nodeId != i)
        {
            std::cerr
                << "Trace error at node "
                << i
                << ".\n";

            return 1;
        }
    }


    // =====================================================
    // 检查默认工作 Baseline 是否为 Sparse-3
    // =====================================================

    applications::Dct8FixedGraph::Trace
        baselineTrace{};


    const std::vector<core::AttackConfig>
        emptyConfigs;


    graph.runApprox(
        input,
        emptyConfigs,
        &baselineTrace
    );


    // Node 0 不在 Sparse-3 的近似节点列表中，
    // 因此默认 Baseline 应为精确加法。
    const int expectedBaseline =
        input[0]
        +
        input[7];


    if (
        baselineTrace[0].output
        !=
        expectedBaseline
    )
    {
        std::cerr
            << "Default Sparse-3 baseline is incorrect.\n";

        return 1;
    }


    // =====================================================
    // Sparse/exact Baseline configuration test
    // =====================================================

    const auto exactBaselineConfig =
        applications::Dct8FixedGraph::
            createAllExactBaselineConfig();


    applications::Dct8FixedGraph
        exactBaselineGraph(
            exactBaselineConfig
        );


    applications::Dct8FixedGraph::Trace
        exactBaselineTrace{};


    const auto exactBaselineOutputVector =
        exactBaselineGraph.runApprox(
            input,
            {},
            &exactBaselineTrace
        );


    if (
        exactBaselineTrace[0].baselineOutput
        !=
        input[0] + input[7]
        ||
        exactBaselineOutputVector
        !=
        exactOutput
    )
    {
        std::cerr
            << "Exact DCT baseline configuration is incorrect.\n";

        return 1;
    }


    const auto sparseConfig =
        applications::Dct8FixedGraph::
            createSparseApproximateBaselineConfig(
                {
                    25,
                    19,
                    13
                },
                approximate::ApproxUnitId::Add12se5RP
            );


    if (
        !sparseConfig[0].exact
        ||
        sparseConfig[25].exact
        ||
        sparseConfig[19].exact
        ||
        sparseConfig[13].exact
    )
    {
        std::cerr
            << "Sparse DCT baseline configuration is incorrect.\n";

        return 1;
    }


    // =====================================================
    // ADD_00 全区间触发 5Z0
    // =====================================================

    const std::vector<core::AttackConfig>
        configs =
    {
        {
            0,
            approximate::ApproxUnitId::Add12se5Z0,
            core::MonitorInput::Input1,
            -2048,
            2047
        }
    };


    applications::Dct8FixedGraph::Trace
        attackTrace{};


    graph.runApprox(
        input,
        configs,
        &attackTrace
    );


    const int expectedAttack =
        approximate::addSigned12(
            input[0],
            input[7],
            approximate::ApproxUnitId::Add12se5Z0
        );


    if (
        attackTrace[0].output
        !=
        expectedAttack
    )
    {
        std::cerr
            << "Configured approximate unit was not triggered.\n";

        return 1;
    }


    // =====================================================
    // ADD_00 使用 Baseline 5RP 输出作为 monitor signal
    // =====================================================

    const std::vector<core::AttackConfig>
        outputMonitorConfigs =
    {
        {
            0,
            approximate::ApproxUnitId::Add12se5Z0,
            core::MonitorSignal::BaselineOutput,
            expectedBaseline,
            expectedBaseline
        }
    };


    applications::Dct8FixedGraph::Trace
        outputMonitorTrace{};


    graph.runApprox(
        input,
        outputMonitorConfigs,
        &outputMonitorTrace
    );


    if (
        outputMonitorTrace[0].baselineOutput
        !=
        expectedBaseline
        ||
        outputMonitorTrace[0].output
        !=
        expectedAttack
    )
    {
        std::cerr
            << "Baseline-output monitor did not trigger the configured unit.\n";

        return 1;
    }



    // ADD_01 也不在 Sparse-3 近似节点列表中，
    // 未攻击时应继续使用精确 Baseline。
    const int expectedNode1 =
        input[1]
        +
        input[6];


    if (
        attackTrace[1].output
        !=
        expectedNode1
    )
    {
        std::cerr
            << "Unconfigured node did not use Sparse-3 baseline.\n";

        return 1;
    }


    std::cout
        << "Maximum fixed DCT difference: "
        << maximumDifference
        << "\n";


    std::cout
        << "Baseline ADD_00 output: "
        << expectedBaseline
        << "\n";


    std::cout
        << "Attack ADD_00 output: "
        << expectedAttack
        << "\n";


    std::cout
        << "Dct8FixedGraph test passed.\n";


    return 0;
}
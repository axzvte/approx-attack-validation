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
    // 检查默认基准是否为5RP
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


    const int expectedBaseline =
        approximate::addSigned12(
            input[0],
            input[7],
            approximate::ApproxUnitId::Add12se5RP
        );


    if (
        baselineTrace[0].output
        !=
        expectedBaseline
    )
    {
        std::cerr
            << "Baseline approximate unit is incorrect.\n";

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


    // 未配置的 ADD_01 仍应使用5RP
    const int expectedNode1 =
        approximate::addSigned12(
            input[1],
            input[6],
            approximate::ApproxUnitId::Add12se5RP
        );


    if (
        attackTrace[1].output
        !=
        expectedNode1
    )
    {
        std::cerr
            << "Unconfigured node did not use baseline unit.\n";

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
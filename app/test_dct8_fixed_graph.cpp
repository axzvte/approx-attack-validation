#include "applications/dct8_fixed_graph.hpp"
#include "applications/dct8_graph.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>


int main()
{
    applications::Dct8Graph
        floatGraph;


    applications::Dct8FixedGraph
        fixedGraph;


    const applications::Dct8FixedGraph::Vector
        fixedInput =
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


    applications::Dct8Graph::Vector
        floatInput{};


    for (int i = 0; i < 8; ++i)
    {
        floatInput[i] =
            static_cast<double>(
                fixedInput[i]
            );
    }


    // =====================================================
    // 浮点参考
    // =====================================================

    const auto floatOutput =
        floatGraph.runExact(
            floatInput
        );


    // =====================================================
    // 定点整数DCT
    // =====================================================

    applications::Dct8FixedGraph::Trace
        trace{};


    const auto fixedOutput =
        fixedGraph.runExact(
            fixedInput,
            &trace
        );


    // =====================================================
    // 比较
    // =====================================================

    std::cout
        << std::fixed
        << std::setprecision(10);


    std::cout
        << "\n========================================\n"
        << "Fixed-point DCT validation\n"
        << "========================================\n";


    std::cout
        << std::setw(8)
        << "Index"

        << std::setw(20)
        << "Float"

        << std::setw(16)
        << "Fixed"

        << std::setw(20)
        << "Difference"

        << "\n";


    double maximumDifference =
        0.0;


    for (int i = 0; i < 8; ++i)
    {
        const double difference =
            std::abs(
                floatOutput[i]
                -
                static_cast<double>(
                    fixedOutput[i]
                )
            );


        maximumDifference =
            std::max(
                maximumDifference,
                difference
            );


        std::cout
            << std::setw(8)
            << i

            << std::setw(20)
            << floatOutput[i]

            << std::setw(16)
            << fixedOutput[i]

            << std::setw(20)
            << difference

            << "\n";
    }


    std::cout
        << "\nMaximum difference: "
        << maximumDifference
        << "\n";


    // 单个乘积会发生整数舍入，
    // 一个DCT输出需要累加4个乘积。
    //
    // 第一版允许最大误差2。
    if (maximumDifference > 2.0)
    {
        std::cerr
            << "Fixed-point DCT error is too large.\n";

        return 1;
    }


    // =====================================================
    // 检查32个ADD全部执行
    // =====================================================

    for (int i = 0;
         i < applications::Dct8FixedGraph::kAddNodeCount;
         ++i)
    {
        if (trace[i].nodeId != i)
        {
            std::cerr
                << "Trace error at node "
                << i
                << "\n";

            return 1;
        }
    }


    // =====================================================
    // 打印32个整数ADD
    // =====================================================

    std::cout
        << "\n========================================\n"
        << "Fixed ADD/SUB trace\n"
        << "========================================\n";


    std::cout
        << std::setw(6)
        << "ID"

        << std::setw(22)
        << "Name"

        << std::setw(12)
        << "Input1"

        << std::setw(12)
        << "Input2"

        << std::setw(12)
        << "Output"

        << "\n";


    for (int i = 0;
         i < applications::Dct8FixedGraph::kAddNodeCount;
         ++i)
    {
        const auto& node =
            trace[i];


        std::cout
            << std::setw(6)
            << node.nodeId

            << std::setw(22)
            << applications::Dct8FixedGraph::nodeName(
                   node.nodeId
               )

            << std::setw(12)
            << node.input1

            << std::setw(12)
            << node.input2

            << std::setw(12)
            << node.output

            << "\n";
    }


    std::cout
        << "\nDct8FixedGraph test passed.\n";


    return 0;
}
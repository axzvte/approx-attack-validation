#include "applications/dct8_graph.hpp"

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>


int main()
{
    applications::Dct8Graph graph;


    const applications::Dct8Graph::Vector input =
    {
        -128.0,
        -64.0,
        -32.0,
        0.0,
        16.0,
        32.0,
        64.0,
        127.0
    };


    // =====================================================
    // 保存32个ADD/SUB位置的运行信息
    // =====================================================

    applications::Dct8Graph::Trace trace{};


    const auto graphOutput =
        graph.runExact(
            input,
            &trace
        );


    // =====================================================
    // OpenCV参考结果
    // =====================================================

    cv::Mat cvInput(
        1,
        8,
        CV_64F
    );


    for (int i = 0; i < 8; ++i)
    {
        cvInput.at<double>(0, i) =
            input[i];
    }


    cv::Mat cvOutput;


    cv::dct(
        cvInput,
        cvOutput
    );


    // =====================================================
    // 验证最终DCT输出
    // =====================================================

    double maxDifference =
        0.0;


    std::cout
        << std::fixed
        << std::setprecision(10);


    std::cout
        << "\n========================================\n"
        << "DCT output validation\n"
        << "========================================\n";


    std::cout
        << "Index    Dct8Graph        OpenCV"
        << "           Difference\n";


    for (int i = 0; i < 8; ++i)
    {
        const double reference =
            cvOutput.at<double>(0, i);


        const double difference =
            std::abs(
                graphOutput[i]
                -
                reference
            );


        maxDifference =
            std::max(
                maxDifference,
                difference
            );


        std::cout
            << i
            << "        "
            << graphOutput[i]
            << "        "
            << reference
            << "        "
            << difference
            << "\n";
    }


    std::cout
        << "\nMaximum difference: "
        << maxDifference
        << "\n";


    if (maxDifference > 1e-9)
    {
        std::cerr
            << "Dct8Graph result does not match OpenCV.\n";

        return 1;
    }


    // =====================================================
    // 打印32个ADD/SUB位置
    // =====================================================

    std::cout
        << "\n========================================\n"
        << "ADD/SUB trace\n"
        << "========================================\n";


    std::cout
        << std::setw(6)
        << "ID"

        << std::setw(22)
        << "Name"

        << std::setw(16)
        << "Input1"

        << std::setw(16)
        << "Input2"

        << std::setw(16)
        << "Output"

        << "\n";


    for (int i = 0;
         i < applications::Dct8Graph::kAddNodeCount;
         ++i)
    {
        const auto& node =
            trace[i];


        // 检查所有32个位置是否真的都执行了
        if (node.nodeId != i)
        {
            std::cerr
                << "Trace error at node "
                << i
                << "\n";

            return 1;
        }


        std::cout
            << std::setw(6)
            << node.nodeId

            << std::setw(22)
            << applications::Dct8Graph::nodeName(
                   node.nodeId
               )

            << std::setw(16)
            << node.input1

            << std::setw(16)
            << node.input2

            << std::setw(16)
            << node.output

            << "\n";
    }


    std::cout
        << "\nDct8Graph test passed.\n";


    return 0;
}
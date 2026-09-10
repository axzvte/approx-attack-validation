#include "applications/dct8_fixed_graph.hpp"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>


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


    applications::Dct8FixedGraph::Trace trace{};


    const auto fixedOutput =
        graph.runExact(
            input,
            &trace
        );


    // ================================================
    // OpenCV 浮点 DCT 作为参考
    // ================================================

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


    double maximumDifference = 0.0;


    std::cout
        << std::fixed
        << std::setprecision(10);


    std::cout
        << "Index"
        << std::setw(20)
        << "Fixed"
        << std::setw(20)
        << "OpenCV"
        << std::setw(20)
        << "Difference"
        << "\n";


    for (int i = 0; i < 8; ++i)
    {
        const double reference =
            cvOutput.at<double>(0, i);


        const double difference =
            std::abs(
                static_cast<double>(
                    fixedOutput[i]
                )
                -
                reference
            );


        maximumDifference =
            std::max(
                maximumDifference,
                difference
            );


        std::cout
            << i
            << std::setw(20)
            << fixedOutput[i]
            << std::setw(20)
            << reference
            << std::setw(20)
            << difference
            << "\n";
    }


    std::cout
        << "\nMaximum difference: "
        << maximumDifference
        << "\n";


    if (maximumDifference > 2.0)
    {
        std::cerr
            << "Fixed-point DCT error is too large.\n";

        return 1;
    }


    // ================================================
    // 确认32个ADD/SUB全部执行
    // ================================================

    for (
        int i = 0;
        i < applications::Dct8FixedGraph::kAddNodeCount;
        ++i
    )
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


    std::cout
        << "Dct8FixedGraph test passed.\n";


    return 0;
}
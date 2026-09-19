#include "applications/dct.hpp"
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
    applications::DctApplication application;


    // =====================================================
    // 32个ADD注册检查
    // =====================================================

    const auto& nodes =
        application.addNodes();


    if (
        nodes.size()
        !=
        applications::Dct8FixedGraph::kAddNodeCount
    )
    {
        std::cerr
            << "Expected 32 ADD/SUB nodes.\n";

        return 1;
    }


    // =====================================================
    // 精确重建测试
    // =====================================================

    cv::Mat inputImage(
        8,
        8,
        CV_8UC1
    );


    for (int row = 0;
         row < 8;
         ++row)
    {
        for (int col = 0;
             col < 8;
             ++col)
        {
            inputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    row * 32
                    +
                    col * 4
                );
        }
    }


    const cv::Mat exactImage =
        application.runExact(
            inputImage
        );


    int maximumDifference =
        0;


    for (int row = 0;
         row < 8;
         ++row)
    {
        for (int col = 0;
             col < 8;
             ++col)
        {
            const int difference =
                std::abs(
                    static_cast<int>(
                        inputImage.at<unsigned char>(
                            row,
                            col
                        )
                    )
                    -
                    static_cast<int>(
                        exactImage.at<unsigned char>(
                            row,
                            col
                        )
                    )
                );


            maximumDifference =
                std::max(
                    maximumDifference,
                    difference
                );
        }
    }


    if (maximumDifference > 1)
    {
        std::cerr
            << "Exact fixed DCT reconstruction error is too large.\n";

        return 1;
    }


    // =====================================================
    // 近似 DCT 测试
    //
    // 使用较小变化的图像，保证12-bit中间数据安全。
    // =====================================================

    cv::Mat approxInput(
        8,
        8,
        CV_8UC1
    );


    for (int row = 0;
         row < 8;
         ++row)
    {
        for (int col = 0;
             col < 8;
             ++col)
        {
            approxInput.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    128
                    +
                    row
                    +
                    col
                );
        }
    }


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


    const cv::Mat approxImage =
        application.runApprox(
            approxInput,
            configs
        );


    if (
        approxImage.rows != approxInput.rows
        ||
        approxImage.cols != approxInput.cols
        ||
        approxImage.type() != CV_8UC1
    )
    {
        std::cerr
            << "Approximate DCT output is invalid.\n";

        return 1;
    }


    const double exactPSNR =
        cv::PSNR(
            inputImage,
            exactImage
        );


    std::cout
        << "Maximum exact pixel difference: "
        << maximumDifference
        << "\n";


    std::cout
        << "Exact reconstruction PSNR: "
        << exactPSNR
        << " dB\n";


    std::cout
        << "Approximate DCT pipeline completed.\n";


    std::cout
        << "DctApplication test passed.\n";


    return 0;
}
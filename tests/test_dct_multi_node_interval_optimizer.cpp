#include "analysis/dct_multi_node_interval_optimizer.hpp"

#include "applications/dct.hpp"
#include "approximate/evoapprox_adapter.hpp"

#include <opencv2/core.hpp>

#include <cmath>
#include <iostream>


int main()
{
    applications::DctApplication
        application;


    cv::Mat inputImage(
        16,
        16,
        CV_8UC1
    );


    for (int row = 0;
         row < inputImage.rows;
         ++row)
    {
        for (int col = 0;
             col < inputImage.cols;
             ++col)
        {
            inputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    80
                    +
                    2 * row
                    +
                    col
                );
        }
    }


    cv::Mat roiMask =
        cv::Mat::zeros(
            inputImage.size(),
            CV_8UC1
        );


    for (int row = 0;
         row < roiMask.rows;
         ++row)
    {
        for (int col = 0;
             col < roiMask.cols / 2;
             ++col)
        {
            roiMask.at<unsigned char>(
                row,
                col
            ) =
                255;
        }
    }


    // 使用与 Baseline 相同的 5RP，
    // 验证多节点优化器不会凭空改变图像质量。
    const analysis::AttackStructure
        structure =
    {
        {
            6,
            approximate::ApproxUnitId::Add12se5RP,
            core::MonitorInput::Input1
        },
        {
            20,
            approximate::ApproxUnitId::Add12se5RP,
            core::MonitorInput::Input2
        }
    };


    analysis::DctMultiNodeIntervalOptimizerOptions
        options;


    options.candidatesPerRole =
        2;


    options.maxAttackRounds =
        2;


    options.globalPsnrThreshold =
        0.0;


    const auto result =
        analysis::
            DctMultiNodeIntervalOptimizer::
                optimize(
                    application,
                    inputImage,
                    roiMask,
                    structure,
                    options
                );


    if (
        std::abs(
            result.finalMetrics.globalPsnr
            -
            result.initialMetrics.globalPsnr
        )
            >
            1.0e-12
        ||
        std::abs(
            result.finalMetrics.roiPsnr
            -
            result.initialMetrics.roiPsnr
        )
            >
            1.0e-12
        ||
        std::abs(
            result.finalMetrics.nonRoiPsnr
            -
            result.initialMetrics.nonRoiPsnr
        )
            >
            1.0e-12
    )
    {
        std::cerr
            << "DCT multi-node optimizer changed metrics for 5RP-only structure.\n";


        return 1;
    }


    std::cout
        << "DCT multi-node interval optimizer test passed.\n";


    return 0;
}

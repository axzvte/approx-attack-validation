#include "analysis/dct_interval_full_validator.hpp"

#include "applications/dct.hpp"
#include "approximate/evoapprox_adapter.hpp"

#include <opencv2/core.hpp>

#include <cmath>
#include <iostream>
#include <vector>


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


    const std::vector<core::AttackConfig>
        currentConfiguration;


    const std::vector<analysis::TriggerInterval>
        intervals =
    {
        {
            -2048,
            2047
        },
        {
            -10,
            10
        }
    };


    // 使用与 Baseline 相同的 5RP 作为 attack unit。
    // 无论区间如何，输出都应与 currentConfiguration 完全一致，
    // 因此三个 PSNR delta 都应为 0。
    const auto report =
        analysis::
            DctIntervalFullValidator::
                validate(
                    application,
                    inputImage,
                    roiMask,
                    currentConfiguration,
                    6,
                    core::MonitorInput::Input1,
                    approximate::ApproxUnitId::Add12se5RP,
                    intervals
                );


    if (report.candidates.size() != intervals.size())
    {
        std::cerr
            << "Unexpected DCT full interval validation result count.\n";


        return 1;
    }


    for (const auto& result : report.candidates)
    {
        if (
            std::abs(
                result.globalPsnrDelta
            )
                >
                1.0e-12
            ||
            std::abs(
                result.roiPsnrDelta
            )
                >
                1.0e-12
            ||
            std::abs(
                result.nonRoiPsnrDelta
            )
                >
                1.0e-12
        )
        {
            std::cerr
                << "DCT full interval validation delta should be zero for 5RP.\n";


            return 1;
        }
    }


    std::cout
        << "DCT interval full validator test passed.\n";


    return 0;
}

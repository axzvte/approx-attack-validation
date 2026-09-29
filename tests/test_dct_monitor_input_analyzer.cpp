#include "analysis/dct_monitor_input_analyzer.hpp"

#include "applications/dct.hpp"

#include <opencv2/core.hpp>

#include <cmath>
#include <iostream>
#include <vector>


int main()
{
    applications::DctApplication
        application;


    cv::Mat inputImage(
        8,
        8,
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
                    120
                    +
                    row
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


    const auto report =
        analysis::DctMonitorInputAnalyzer::analyze(
            application,
            {
                inputImage
            },
            {
                roiMask
            },
            {
                0,
                6
            }
        );


    if (
        report.perImageResults.size()
        !=
        2
        *
        2
        *
        2
    )
    {
        std::cerr
            << "Unexpected per-image result count.\n";


        return 1;
    }


    if (
        report.summaries.size()
        !=
        2
        *
        2
        *
        2
    )
    {
        std::cerr
            << "Unexpected summary count.\n";


        return 1;
    }


    for (const auto& summary : report.summaries)
    {
        if (
            !std::isfinite(
                summary.meanGap
            )
            ||
            !std::isfinite(
                summary.stdGap
            )
        )
        {
            std::cerr
                << "Monitor-input analysis returned non-finite values.\n";


            return 1;
        }


        if (
            summary.meanGap < 0.0
            ||
            summary.meanGap > 1.0
        )
        {
            std::cerr
                << "Monitor-input mean gap is outside [0, 1].\n";


            return 1;
        }
    }


    std::cout
        << "DCT monitor-input analyzer test passed.\n";


    return 0;
}

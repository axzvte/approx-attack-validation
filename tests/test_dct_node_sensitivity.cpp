#include "analysis/dct_node_sensitivity.hpp"

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


    cv::Mat inputImage1(
        8,
        8,
        CV_8UC1
    );


    for (int row = 0;
         row < inputImage1.rows;
         ++row)
    {
        for (int col = 0;
             col < inputImage1.cols;
             ++col)
        {
            inputImage1.at<unsigned char>(
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


    cv::Mat inputImage2 =
        inputImage1.clone();


    for (int row = 0;
         row < inputImage2.rows;
         ++row)
    {
        for (int col = 0;
             col < inputImage2.cols;
             ++col)
        {
            inputImage2.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    100
                    +
                    2 * row
                    +
                    col
                );
        }
    }


    cv::Mat roiMask =
        cv::Mat::zeros(
            inputImage1.size(),
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


    const std::vector<approximate::ApproxUnitId>
        attackUnits =
    {
        approximate::ApproxUnitId::Add12se5L8,
        approximate::ApproxUnitId::Add12se5Z0
    };


    const std::vector<cv::Mat>
        inputImages =
    {
        inputImage1,
        inputImage2
    };


    const std::vector<cv::Mat>
        roiMasks =
    {
        roiMask,
        roiMask
    };


    const auto report =
        analysis::DctNodeSensitivityAnalyzer::analyze(
            application,
            inputImages,
            roiMasks,
            attackUnits
        );


    const std::size_t expectedUnitResults =
        application.addNodes().size()
        *
        attackUnits.size();


    if (
        report.unitResults.size()
        !=
        expectedUnitResults
    )
    {
        std::cerr
            << "Unexpected DCT sensitivity unit-result count.\n";


        return 1;
    }


    const std::size_t expectedPerImageResults =
        inputImages.size()
        *
        application.addNodes().size()
        *
        attackUnits.size();


    if (
        report.perImageUnitResults.size()
        !=
        expectedPerImageResults
    )
    {
        std::cerr
            << "Unexpected DCT per-image sensitivity result count.\n";


        return 1;
    }


    if (
        report.nodeSummaries.size()
        !=
        application.addNodes().size()
    )
    {
        std::cerr
            << "Unexpected DCT sensitivity node-summary count.\n";


        return 1;
    }


    bool foundLocalError =
        false;


    for (const auto& result : report.unitResults)
    {
        if (
            !std::isfinite(
                result.localMse
            )
            ||
            !std::isfinite(
                result.outputMse
            )
            ||
            !std::isfinite(
                result.roiMse
            )
            ||
            !std::isfinite(
                result.nonRoiMse
            )
            ||
            !std::isfinite(
                result.sensitivity
            )
            ||
            !std::isfinite(
                result.roiSensitivity
            )
            ||
            !std::isfinite(
                result.roiToNonRoiRatio
            )
        )
        {
            std::cerr
                << "DCT sensitivity returned a non-finite metric.\n";


            return 1;
        }


        if (result.localSampleCount == 0)
        {
            std::cerr
                << "DCT sensitivity returned zero local samples.\n";


            return 1;
        }


        if (result.localMse > 0.0)
        {
            foundLocalError =
                true;
        }
    }


    for (const auto& result : report.perImageUnitResults)
    {
        if (
            !std::isfinite(
                result.localMse
            )
            ||
            !std::isfinite(
                result.outputMse
            )
            ||
            !std::isfinite(
                result.sensitivity
            )
        )
        {
            std::cerr
                << "DCT per-image sensitivity returned a non-finite metric.\n";


            return 1;
        }


        if (result.localSampleCount == 0)
        {
            std::cerr
                << "DCT per-image sensitivity returned zero local samples.\n";


            return 1;
        }
    }


    for (const auto& summary : report.nodeSummaries)
    {
        if (
            !std::isfinite(
                summary.meanImageSensitivity
            )
            ||
            !std::isfinite(
                summary.stdImageSensitivity
            )
            ||
            !std::isfinite(
                summary.cvImageSensitivity
            )
        )
        {
            std::cerr
                << "DCT cross-image sensitivity returned a non-finite metric.\n";


            return 1;
        }


        if (
            summary.meanImageSensitivity < 0.0
            ||
            summary.stdImageSensitivity < 0.0
            ||
            summary.cvImageSensitivity < 0.0
        )
        {
            std::cerr
                << "DCT cross-image sensitivity returned a negative metric.\n";


            return 1;
        }
    }


    if (!foundLocalError)
    {
        std::cerr
            << "DCT sensitivity produced no local approximation error.\n";


        return 1;
    }


    std::cout
        << "DCT node sensitivity test passed.\n";


    return 0;
}

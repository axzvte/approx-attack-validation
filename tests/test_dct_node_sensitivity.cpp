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


    const std::vector<approximate::ApproxUnitId>
        attackUnits =
    {
        approximate::ApproxUnitId::Add12se5L8,
        approximate::ApproxUnitId::Add12se5Z0
    };


    const auto report =
        analysis::DctNodeSensitivityAnalyzer::analyze(
            application,
            {
                inputImage
            },
            {
                roiMask
            },
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

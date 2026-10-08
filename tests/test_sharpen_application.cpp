#include "applications/sharpen.hpp"

#include <opencv2/core.hpp>

#include <iostream>
#include <vector>


int main()
{
    cv::Mat image(
        8,
        8,
        CV_8UC1
    );


    for (int row = 0;
         row < image.rows;
         ++row)
    {
        for (int col = 0;
             col < image.cols;
             ++col)
        {
            image.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    20
                    +
                    row * 12
                    +
                    col * 7
                );
        }
    }


    cv::Mat roiMask =
        cv::Mat::zeros(
            image.size(),
            CV_8UC1
        );


    roiMask(
        cv::Rect(
            2,
            2,
            4,
            4
        )
    ).setTo(
        255
    );


    const auto exactConfig =
        applications::SharpenApplication::
            createAllExactBaselineConfig();


    applications::SharpenApplication
        exactApplication(
            exactConfig
        );


    const cv::Mat exactImage =
        exactApplication.runExact(
            image
        );


    const cv::Mat exactBaselineImage =
        exactApplication.runApprox(
            image,
            {}
        );


    if (
        cv::countNonZero(
            exactImage
            !=
            exactBaselineImage
        )
        !=
        0
    )
    {
        std::cerr
            << "Exact sharpening baseline does not match exact output.\n";

        return 1;
    }


    std::vector<core::AddSample>
        baselineSamples;


    exactApplication.collectBaselineAddSamples(
        image,
        roiMask,
        baselineSamples
    );


    const std::size_t expectedSamples =
        static_cast<std::size_t>(
            (image.rows - 2)
            *
            (image.cols - 2)
            *
            applications::SharpenApplication::kAddNodeCount
        );


    if (
        baselineSamples.size()
        !=
        expectedSamples
    )
    {
        std::cerr
            << "Unexpected sharpening sample count.\n";

        return 1;
    }


    const std::vector<core::AttackConfig>
        configuration =
    {
        core::AttackConfig{
            0,
            approximate::ApproxUnitId::Add12se5Z0,
            core::MonitorSignal::Input1,
            -2048,
            2047
        }
    };


    std::vector<core::AddSample>
        configuredSamples;


    exactApplication.collectConfiguredAddSamples(
        image,
        roiMask,
        configuration,
        configuredSamples
    );


    if (
        configuredSamples.size()
        !=
        expectedSamples
    )
    {
        std::cerr
            << "Configured sharpening sample count is incorrect.\n";

        return 1;
    }


    bool downstreamChanged =
        false;


    for (std::size_t index = 0;
         index < baselineSamples.size();
         ++index)
    {
        if (
            baselineSamples[index].nodeId == 1
            &&
            configuredSamples[index].input1
                !=
                baselineSamples[index].input1
        )
        {
            downstreamChanged =
                true;

            break;
        }
    }


    if (!downstreamChanged)
    {
        std::cerr
            << "Upstream sharpening error did not propagate to downstream samples.\n";

        return 1;
    }


    std::cout
        << "SharpenApplication generic-interface test passed.\n";


    return 0;
}

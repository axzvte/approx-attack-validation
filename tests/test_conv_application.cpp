#include "applications/conv.hpp"

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
                    row * 13
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
        applications::ConvApplication::
            createAllExactBaselineConfig();


    applications::ConvApplication
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
            << "Exact Conv baseline does not match exact output.\n";

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
            applications::ConvApplication::kAddNodeCount
        );


    if (
        baselineSamples.size()
        !=
        expectedSamples
    )
    {
        std::cerr
            << "Unexpected Conv sample count.\n";

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
            << "Configured Conv sample count is incorrect.\n";

        return 1;
    }


    // C5.input1 必须来自当前配置下 C1.output，验证误差真实传播。
    for (std::size_t base = 0;
         base < configuredSamples.size();
         base += applications::ConvApplication::kAddNodeCount)
    {
        if (
            configuredSamples[base + 4].input1
            !=
            configuredSamples[base].output
        )
        {
            std::cerr
                << "Configured Conv downstream input is not the current upstream output.\n";

            return 1;
        }
    }


    std::cout
        << "ConvApplication generic-interface test passed.\n";


    return 0;
}

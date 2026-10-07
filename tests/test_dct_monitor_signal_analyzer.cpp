#include "analysis/dct_monitor_signal_analyzer.hpp"

#include "applications/dct.hpp"

#include <opencv2/core.hpp>

#include <cmath>
#include <iostream>
#include <vector>


int main()
{
    applications::DctApplication application;


    std::vector<cv::Mat>
        images;


    std::vector<cv::Mat>
        masks;


    for (int imageIndex = 0;
         imageIndex < 2;
         ++imageIndex)
    {
        cv::Mat image(
            16,
            16,
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
                        70
                        +
                        row
                        +
                        2 * col
                        +
                        imageIndex
                    );
            }
        }


        cv::Mat mask =
            cv::Mat::zeros(
                image.size(),
                CV_8UC1
            );


        mask(
            cv::Rect(
                0,
                0,
                image.cols / 2,
                image.rows
            )
        ).setTo(
            255
        );


        images.push_back(
            image
        );


        masks.push_back(
            mask
        );
    }


    const std::vector<int>
        nodes =
    {
        0,
        6
    };


    const auto report =
        analysis::
            DctMonitorSignalAnalyzer::
                analyze(
                    application,
                    images,
                    masks,
                    nodes
                );


    const std::size_t expectedPerImage =
        nodes.size()
        *
        3
        *
        2;


    const std::size_t expectedSummaries =
        nodes.size()
        *
        3
        *
        2;


    if (
        report.perImageResults.size()
        !=
        expectedPerImage
        *
        images.size()
        ||
        report.summaries.size()
        !=
        expectedSummaries
    )
    {
        std::cerr
            << "Unexpected DCT monitor-signal report size.\n";


        return 1;
    }


    for (const auto& summary : report.summaries)
    {
        if (
            summary.meanGap < 0.0
            ||
            !std::isfinite(
                summary.meanGap
            )
        )
        {
            std::cerr
                << "Invalid DCT monitor-signal mean gap.\n";


            return 1;
        }
    }


    std::cout
        << "DCT monitor-signal analyzer test passed.\n";


    return 0;
}

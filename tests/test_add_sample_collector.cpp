#include "analysis/add_sample_collector.hpp"
#include "applications/dct.hpp"

#include <opencv2/core.hpp>

#include <cmath>
#include <iostream>


int main()
{
    applications::DctApplication application;

    analysis::AddSampleCollector collector;


    cv::Mat image(
        8,
        8,
        CV_8UC1,
        cv::Scalar(128)
    );


    cv::Mat roiMask =
        cv::Mat::zeros(
            8,
            8,
            CV_8UC1
        );


    // 左边4列设置为ROI
    for (int row = 0;
         row < 8;
         ++row)
    {
        for (int col = 0;
             col < 4;
             ++col)
        {
            roiMask.at<unsigned char>(
                row,
                col
            ) =
                255;
        }
    }


    collector.collect(
        application,
        image,
        roiMask
    );


    // 一个8×8 block:
    //
    // 16次一维DCT × 32 ADD
    //
    // = 512 samples

    if (collector.size() != 512)
    {
        std::cerr
            << "Expected 512 samples, got "
            << collector.size()
            << ".\n";

        return 1;
    }


    for (int nodeId = 0;
         nodeId < 32;
         ++nodeId)
    {
        if (
            collector.countForNode(nodeId)
            !=
            16
        )
        {
            std::cerr
                << "Wrong sample count for node "
                << nodeId
                << ".\n";

            return 1;
        }
    }


    // 当前block一半属于ROI
    // 所有样本权重都应该是0.5

    for (const auto& sample :
         collector.samples())
    {
        if (
            std::abs(
                sample.roiWeight
                -
                0.5
            )
            >
            1e-12
        )
        {
            std::cerr
                << "Incorrect ROI weight: "
                << sample.roiWeight
                << "\n";

            return 1;
        }
    }


    std::cout
        << "Total samples: "
        << collector.size()
        << "\n";


    std::cout
        << "Samples per node: 16\n";


    std::cout
        << "ROI weight: "
        << collector.samples()[0].roiWeight
        << "\n";


    std::cout
        << "AddSampleCollector test passed.\n";


    return 0;
}
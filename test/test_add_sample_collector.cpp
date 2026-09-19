#include "analysis/add_sample_collector.hpp"
#include "applications/dct.hpp"
#include "applications/dct8_fixed_graph.hpp"

#include <opencv2/core.hpp>

#include <cmath>
#include <iostream>


int main()
{
    applications::DctApplication application;
    analysis::AddSampleCollector collector;


    // =====================================================
    // 1. 检查 DCT 是否注册 32 个 ADD/SUB
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
            << "Expected 32 ADD/SUB nodes, got "
            << nodes.size()
            << ".\n";

        return 1;
    }


    // =====================================================
    // 2. 构造 8×8 测试图像
    // =====================================================

    cv::Mat inputImage(
        8,
        8,
        CV_8UC1
    );


    for (int row = 0; row < 8; ++row)
    {
        for (int col = 0; col < 8; ++col)
        {
            inputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    128 + row + col
                );
        }
    }


    // =====================================================
    // 3. 构造 ROI mask
    //
    // 左半边 32 个像素属于 ROI
    // 因此整个 block:
    //
    // roiWeight = 32 / 64 = 0.5
    // =====================================================

    cv::Mat roiMask =
        cv::Mat::zeros(
            8,
            8,
            CV_8UC1
        );


    for (int row = 0; row < 8; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            roiMask.at<unsigned char>(
                row,
                col
            ) = 255;
        }
    }


    // =====================================================
    // 4. 收集中间数据
    // =====================================================

    collector.collect(
        application,
        inputImage,
        roiMask
    );


    // =====================================================
    // 一个 8×8 block：
    //
    // 8 次行 DCT
    // +
    // 8 次列 DCT
    //
    // = 16 次 Dct8FixedGraph
    //
    // 每次 32 个 ADD
    //
    // 16 × 32 = 512 samples
    // =====================================================

    const std::size_t expectedTotalSamples =
        16
        *
        applications::Dct8FixedGraph::kAddNodeCount;


    if (
        collector.size()
        !=
        expectedTotalSamples
    )
    {
        std::cerr
            << "Expected "
            << expectedTotalSamples
            << " samples, got "
            << collector.size()
            << ".\n";

        return 1;
    }


    // =====================================================
    // 5. 每个静态 ADD 应执行 16 次
    // =====================================================

    for (int nodeId = 0;
         nodeId <
             applications::Dct8FixedGraph::kAddNodeCount;
         ++nodeId)
    {
        const std::size_t count =
            collector.countForNode(
                nodeId
            );


        if (count != 16)
        {
            std::cerr
                << "Node "
                << nodeId
                << " expected 16 samples, got "
                << count
                << ".\n";

            return 1;
        }
    }


    // =====================================================
    // 6. 当前 block 一半属于 ROI
    //
    // 所以每个样本的 roiWeight 都应为 0.5
    // =====================================================

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
                << ".\n";

            return 1;
        }
    }


    // =====================================================
    // 7. 打印第一条样本
    // =====================================================

    const auto& firstSample =
        collector.samples().front();


    std::cout
        << "Total samples       : "
        << collector.size()
        << "\n";


    std::cout
        << "Samples per ADD     : 16\n";


    std::cout
        << "First node ID       : "
        << firstSample.nodeId
        << "\n";


    std::cout
        << "First input1        : "
        << firstSample.input1
        << "\n";


    std::cout
        << "First input2        : "
        << firstSample.input2
        << "\n";


    std::cout
        << "First output        : "
        << firstSample.output
        << "\n";


    std::cout
        << "ROI weight          : "
        << firstSample.roiWeight
        << "\n";


    std::cout
        << "AddSampleCollector test passed.\n";


    return 0;
}
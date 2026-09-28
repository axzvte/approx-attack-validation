#include "analysis/dct_node_sensitivity.hpp"

#include "applications/dct8_fixed_graph.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "core/add_sample.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>


namespace analysis
{

namespace
{

constexpr double kEpsilon =
    1.0e-12;


struct PixelErrorTotals
{
    long double globalSquaredError = 0.0L;
    long double roiSquaredError = 0.0L;
    long double nonRoiSquaredError = 0.0L;

    std::uint64_t globalPixelCount = 0;
    std::uint64_t roiPixelCount = 0;
    std::uint64_t nonRoiPixelCount = 0;
};


void validateImagePair(
    const cv::Mat& inputImage,
    const cv::Mat& roiMask
)
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "DCT sensitivity input image is empty."
        );
    }


    if (roiMask.empty())
    {
        throw std::runtime_error(
            "DCT sensitivity ROI mask is empty."
        );
    }


    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "DCT sensitivity requires CV_8UC1 input images."
        );
    }


    if (roiMask.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "DCT sensitivity requires CV_8UC1 ROI masks."
        );
    }


    if (
        inputImage.rows != roiMask.rows
        ||
        inputImage.cols != roiMask.cols
    )
    {
        throw std::runtime_error(
            "DCT sensitivity input image and ROI mask sizes do not match."
        );
    }


    std::uint64_t roiCount = 0;
    std::uint64_t nonRoiCount = 0;


    for (int row = 0;
         row < roiMask.rows;
         ++row)
    {
        for (int col = 0;
             col < roiMask.cols;
             ++col)
        {
            if (
                region_mask::isImportantPixel(
                    roiMask,
                    row,
                    col
                )
            )
            {
                ++roiCount;
            }
            else
            {
                ++nonRoiCount;
            }
        }
    }


    if (roiCount == 0)
    {
        throw std::runtime_error(
            "DCT sensitivity ROI mask contains no ROI pixels."
        );
    }


    if (nonRoiCount == 0)
    {
        throw std::runtime_error(
            "DCT sensitivity ROI mask contains no non-ROI pixels."
        );
    }
}


void accumulatePixelErrors(
    const cv::Mat& baselineImage,
    const cv::Mat& attackedImage,
    const cv::Mat& roiMask,
    PixelErrorTotals& totals
)
{
    if (
        baselineImage.empty()
        ||
        attackedImage.empty()
    )
    {
        throw std::runtime_error(
            "DCT sensitivity output image is empty."
        );
    }


    if (
        baselineImage.type() != CV_8UC1
        ||
        attackedImage.type() != CV_8UC1
    )
    {
        throw std::runtime_error(
            "DCT sensitivity output images must be CV_8UC1."
        );
    }


    if (
        baselineImage.rows != attackedImage.rows
        ||
        baselineImage.cols != attackedImage.cols
        ||
        baselineImage.rows != roiMask.rows
        ||
        baselineImage.cols != roiMask.cols
    )
    {
        throw std::runtime_error(
            "DCT sensitivity output image dimensions do not match."
        );
    }


    for (int row = 0;
         row < baselineImage.rows;
         ++row)
    {
        for (int col = 0;
             col < baselineImage.cols;
             ++col)
        {
            const long double baselineValue =
                baselineImage.at<unsigned char>(
                    row,
                    col
                );


            const long double attackedValue =
                attackedImage.at<unsigned char>(
                    row,
                    col
                );


            const long double difference =
                attackedValue
                -
                baselineValue;


            const long double squaredError =
                difference
                *
                difference;


            totals.globalSquaredError +=
                squaredError;

            ++totals.globalPixelCount;


            if (
                region_mask::isImportantPixel(
                    roiMask,
                    row,
                    col
                )
            )
            {
                totals.roiSquaredError +=
                    squaredError;

                ++totals.roiPixelCount;
            }
            else
            {
                totals.nonRoiSquaredError +=
                    squaredError;

                ++totals.nonRoiPixelCount;
            }
        }
    }
}


double meanSquaredError(
    long double squaredError,
    std::uint64_t count
)
{
    if (count == 0)
    {
        throw std::runtime_error(
            "DCT sensitivity metric has zero samples."
        );
    }


    return
        static_cast<double>(
            squaredError
            /
            static_cast<long double>(
                count
            )
        );
}


double normalizedSensitivity(
    double outputMse,
    double localMse
)
{
    if (
        outputMse < 0.0
        ||
        localMse < 0.0
    )
    {
        throw std::runtime_error(
            "DCT sensitivity received a negative MSE."
        );
    }


    if (localMse <= kEpsilon)
    {
        return 0.0;
    }


    return
        std::sqrt(
            outputMse
            /
            localMse
        );
}

}


// =========================================================
// DCT 单节点敏感性分析
// =========================================================

DctNodeSensitivityReport
DctNodeSensitivityAnalyzer::analyze(
    const applications::DctApplication& application,
    const std::vector<cv::Mat>& inputImages,
    const std::vector<cv::Mat>& roiMasks,
    const std::vector<approximate::ApproxUnitId>& attackUnits,
    const DctNodeSensitivityProgressCallback& progressCallback
)
{
    if (inputImages.empty())
    {
        throw std::runtime_error(
            "DCT sensitivity image set is empty."
        );
    }


    if (
        inputImages.size()
        !=
        roiMasks.size()
    )
    {
        throw std::runtime_error(
            "DCT sensitivity image and ROI mask counts do not match."
        );
    }


    if (attackUnits.empty())
    {
        throw std::runtime_error(
            "DCT sensitivity attack-unit set is empty."
        );
    }


    if (
        application.addNodes().size()
        !=
        applications::Dct8FixedGraph::kAddNodeCount
    )
    {
        throw std::runtime_error(
            "DCT sensitivity expected exactly 32 ADD/SUB nodes."
        );
    }


    for (std::size_t imageIndex = 0;
         imageIndex < inputImages.size();
         ++imageIndex)
    {
        validateImagePair(
            inputImages[imageIndex],
            roiMasks[imageIndex]
        );
    }


    const std::size_t unitCount =
        attackUnits.size();


    std::vector<
        std::vector<long double>
    >
        localSquaredErrorSums(
            applications::Dct8FixedGraph::kAddNodeCount,
            std::vector<long double>(
                unitCount,
                0.0L
            )
        );


    std::array<
        std::uint64_t,
        applications::Dct8FixedGraph::kAddNodeCount
    >
        localSampleCounts{};


    std::vector<cv::Mat>
        baselineImages;


    baselineImages.reserve(
        inputImages.size()
    );


    // =====================================================
    // 先跑正常 5RP Baseline。
    //
    // 同时在 Baseline 的真实动态输入上计算：
    //
    // attackUnit(a,b) - baseline5RP(a,b)
    //
    // 这样 localMse 只描述“节点自身换单元后产生的扰动”，
    // 不混入该扰动已经传播到后续输入后的二次影响。
    // =====================================================

    for (std::size_t imageIndex = 0;
         imageIndex < inputImages.size();
         ++imageIndex)
    {
        const cv::Mat baselineImage =
            application.runApprox(
                inputImages[imageIndex],
                {}
            );


        baselineImages.push_back(
            baselineImage
        );


        std::vector<core::AddSample>
            samples;


        application.collectBaselineAddSamples(
            inputImages[imageIndex],
            roiMasks[imageIndex],
            samples
        );


        for (const auto& sample : samples)
        {
            if (
                sample.nodeId < 0
                ||
                sample.nodeId
                    >=
                    applications::Dct8FixedGraph::kAddNodeCount
            )
            {
                throw std::runtime_error(
                    "DCT sensitivity collected an invalid node ID."
                );
            }


            ++localSampleCounts[
                sample.nodeId
            ];


            for (std::size_t unitIndex = 0;
                 unitIndex < unitCount;
                 ++unitIndex)
            {
                const int attackedLocalOutput =
                    approximate::addSigned12(
                        sample.input1,
                        sample.input2,
                        attackUnits[unitIndex]
                    );


                const long double localError =
                    static_cast<long double>(
                        attackedLocalOutput
                    )
                    -
                    static_cast<long double>(
                        sample.output
                    );


                localSquaredErrorSums[
                    sample.nodeId
                ][
                    unitIndex
                ]
                    +=
                    localError
                    *
                    localError;
            }
        }


        if (progressCallback)
        {
            progressCallback(
                DctNodeSensitivityProgress{
                    DctNodeSensitivityPhase::Baseline,
                    imageIndex + 1,
                    inputImages.size(),
                    -1,
                    approximate::ApproxUnitId::Add12se5RP,
                    imageIndex + 1,
                    inputImages.size()
                }
            );
        }
    }


    // =====================================================
    // 每个节点 × 每个攻击单元做一次“单节点全触发”。
    // =====================================================

    DctNodeSensitivityReport
        report;


    report.unitResults.reserve(
        applications::Dct8FixedGraph::kAddNodeCount
        *
        unitCount
    );


    report.nodeSummaries.reserve(
        applications::Dct8FixedGraph::kAddNodeCount
    );


    for (int nodeId = 0;
         nodeId
            <
            applications::Dct8FixedGraph::kAddNodeCount;
         ++nodeId)
    {
        if (
            localSampleCounts[nodeId]
            ==
            0
        )
        {
            throw std::runtime_error(
                "DCT sensitivity found a node with no baseline samples."
            );
        }


        DctNodeSensitivitySummary
            summary;


        summary.nodeId =
            nodeId;


        bool firstUnit =
            true;


        for (std::size_t unitIndex = 0;
             unitIndex < unitCount;
             ++unitIndex)
        {
            const approximate::ApproxUnitId
                attackUnit =
                    attackUnits[
                        unitIndex
                    ];


            const core::AttackConfig
                fullTriggerConfig =
            {
                nodeId,
                attackUnit,
                core::MonitorInput::Input1,
                -2048,
                2047
            };


            PixelErrorTotals
                outputTotals;


            for (std::size_t imageIndex = 0;
                 imageIndex < inputImages.size();
                 ++imageIndex)
            {
                const cv::Mat attackedImage =
                    application.runApprox(
                        inputImages[imageIndex],
                        {
                            fullTriggerConfig
                        }
                    );


                accumulatePixelErrors(
                    baselineImages[imageIndex],
                    attackedImage,
                    roiMasks[imageIndex],
                    outputTotals
                );


                if (progressCallback)
                {
                    const std::size_t configurationIndex =
                        static_cast<std::size_t>(
                            nodeId
                        )
                        *
                        unitCount
                        +
                        unitIndex;


                    const std::size_t completedImages =
                        configurationIndex
                        *
                        inputImages.size()
                        +
                        imageIndex
                        +
                        1;


                    const std::size_t totalImages =
                        static_cast<std::size_t>(
                            applications::Dct8FixedGraph::kAddNodeCount
                        )
                        *
                        unitCount
                        *
                        inputImages.size();


                    progressCallback(
                        DctNodeSensitivityProgress{
                            DctNodeSensitivityPhase::Attack,
                            completedImages,
                            totalImages,
                            nodeId,
                            attackUnit,
                            imageIndex + 1,
                            inputImages.size()
                        }
                    );
                }
            }


            DctNodeUnitSensitivity
                result;


            result.nodeId =
                nodeId;


            result.attackUnit =
                attackUnit;


            result.localSampleCount =
                localSampleCounts[
                    nodeId
                ];


            result.localMse =
                meanSquaredError(
                    localSquaredErrorSums[
                        nodeId
                    ][
                        unitIndex
                    ],
                    localSampleCounts[
                        nodeId
                    ]
                );


            result.outputMse =
                meanSquaredError(
                    outputTotals.globalSquaredError,
                    outputTotals.globalPixelCount
                );


            result.roiMse =
                meanSquaredError(
                    outputTotals.roiSquaredError,
                    outputTotals.roiPixelCount
                );


            result.nonRoiMse =
                meanSquaredError(
                    outputTotals.nonRoiSquaredError,
                    outputTotals.nonRoiPixelCount
                );


            result.sensitivity =
                normalizedSensitivity(
                    result.outputMse,
                    result.localMse
                );


            result.roiSensitivity =
                normalizedSensitivity(
                    result.roiMse,
                    result.localMse
                );


            result.roiToNonRoiRatio =
                result.roiMse
                /
                (
                    result.nonRoiMse
                    +
                    kEpsilon
                );


            summary.meanSensitivity +=
                result.sensitivity;


            summary.meanRoiSensitivity +=
                result.roiSensitivity;


            summary.meanRoiToNonRoiRatio +=
                result.roiToNonRoiRatio;


            if (
                firstUnit
                ||
                result.sensitivity
                    >
                    summary.maxSensitivity
            )
            {
                firstUnit =
                    false;


                summary.maxSensitivity =
                    result.sensitivity;


                summary.maxSensitivityUnit =
                    attackUnit;
            }


            report.unitResults.push_back(
                result
            );
        }


        const double divisor =
            static_cast<double>(
                unitCount
            );


        summary.meanSensitivity /=
            divisor;


        summary.meanRoiSensitivity /=
            divisor;


        summary.meanRoiToNonRoiRatio /=
            divisor;


        report.nodeSummaries.push_back(
            summary
        );
    }


    return report;
}

}

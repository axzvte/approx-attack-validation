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


void mergePixelErrorTotals(
    PixelErrorTotals& target,
    const PixelErrorTotals& source
)
{
    target.globalSquaredError +=
        source.globalSquaredError;

    target.roiSquaredError +=
        source.roiSquaredError;

    target.nonRoiSquaredError +=
        source.nonRoiSquaredError;

    target.globalPixelCount +=
        source.globalPixelCount;

    target.roiPixelCount +=
        source.roiPixelCount;

    target.nonRoiPixelCount +=
        source.nonRoiPixelCount;
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


void fillSensitivityMetrics(
    DctNodeImageUnitSensitivity& result,
    long double localSquaredError,
    std::uint64_t localSampleCount,
    const PixelErrorTotals& outputTotals
)
{
    result.localSampleCount =
        localSampleCount;


    result.localMse =
        meanSquaredError(
            localSquaredError,
            localSampleCount
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
}


void fillSensitivityMetrics(
    DctNodeUnitSensitivity& result,
    long double localSquaredError,
    std::uint64_t localSampleCount,
    const PixelErrorTotals& outputTotals
)
{
    result.localSampleCount =
        localSampleCount;


    result.localMse =
        meanSquaredError(
            localSquaredError,
            localSampleCount
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


    const std::size_t imageCount =
        inputImages.size();


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


    std::vector<
        std::vector<
            std::vector<long double>
        >
    >
        perImageLocalSquaredErrorSums(
            imageCount,
            std::vector<
                std::vector<long double>
            >(
                applications::Dct8FixedGraph::kAddNodeCount,
                std::vector<long double>(
                    unitCount,
                    0.0L
                )
            )
        );


    std::vector<
        std::array<
            std::uint64_t,
            applications::Dct8FixedGraph::kAddNodeCount
        >
    >
        perImageLocalSampleCounts(
            imageCount
        );


    std::vector<cv::Mat>
        baselineImages;


    baselineImages.reserve(
        imageCount
    );


    // =====================================================
    // 先跑 DctApplication 当前持有的 Baseline。
    // 当前节点筛选程序显式传入 Sparse-3：
    // Node 25、19、13 = 5RP，其余节点 = Exact。
    //
    // 同时分别保留：
    // 1. 全部图片汇总后的局部误差；
    // 2. 每张图片自己的局部误差。
    // =====================================================

    for (std::size_t imageIndex = 0;
         imageIndex < imageCount;
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


            ++perImageLocalSampleCounts[
                imageIndex
            ][
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


                const long double squaredLocalError =
                    localError
                    *
                    localError;


                localSquaredErrorSums[
                    sample.nodeId
                ][
                    unitIndex
                ]
                    +=
                    squaredLocalError;


                perImageLocalSquaredErrorSums[
                    imageIndex
                ][
                    sample.nodeId
                ][
                    unitIndex
                ]
                    +=
                    squaredLocalError;
            }
        }


        if (progressCallback)
        {
            progressCallback(
                DctNodeSensitivityProgress{
                    DctNodeSensitivityPhase::Baseline,
                    imageIndex + 1,
                    imageCount,
                    -1,
                    approximate::ApproxUnitId::Add12se5RP,
                    imageIndex + 1,
                    imageCount
                }
            );
        }
    }


    // =====================================================
    // 每个节点 × 每个攻击单元做一次“单节点全触发”。
    //
    // 在这一轮中同时得到：
    // 1. 原有的“全部图片合并结果”；
    // 2. 新增的“每张图片独立结果”。
    // =====================================================

    DctNodeSensitivityReport
        report;


    report.unitResults.reserve(
        applications::Dct8FixedGraph::kAddNodeCount
        *
        unitCount
    );


    report.perImageUnitResults.reserve(
        imageCount
        *
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


        std::vector<double>
            perImageMeanSensitivity(
                imageCount,
                0.0
            );


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
                 imageIndex < imageCount;
                 ++imageIndex)
            {
                if (
                    perImageLocalSampleCounts[
                        imageIndex
                    ][
                        nodeId
                    ]
                    ==
                    0
                )
                {
                    throw std::runtime_error(
                        "DCT sensitivity found an image/node pair with no baseline samples."
                    );
                }


                const cv::Mat attackedImage =
                    application.runApprox(
                        inputImages[imageIndex],
                        {
                            fullTriggerConfig
                        }
                    );


                PixelErrorTotals
                    imageOutputTotals;


                accumulatePixelErrors(
                    baselineImages[imageIndex],
                    attackedImage,
                    roiMasks[imageIndex],
                    imageOutputTotals
                );


                mergePixelErrorTotals(
                    outputTotals,
                    imageOutputTotals
                );


                DctNodeImageUnitSensitivity
                    imageResult;


                imageResult.imageIndex =
                    imageIndex;


                imageResult.nodeId =
                    nodeId;


                imageResult.attackUnit =
                    attackUnit;


                fillSensitivityMetrics(
                    imageResult,
                    perImageLocalSquaredErrorSums[
                        imageIndex
                    ][
                        nodeId
                    ][
                        unitIndex
                    ],
                    perImageLocalSampleCounts[
                        imageIndex
                    ][
                        nodeId
                    ],
                    imageOutputTotals
                );


                perImageMeanSensitivity[
                    imageIndex
                ]
                    +=
                    imageResult.sensitivity;


                report.perImageUnitResults.push_back(
                    imageResult
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
                        imageCount
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
                        imageCount;


                    progressCallback(
                        DctNodeSensitivityProgress{
                            DctNodeSensitivityPhase::Attack,
                            completedImages,
                            totalImages,
                            nodeId,
                            attackUnit,
                            imageIndex + 1,
                            imageCount
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


            fillSensitivityMetrics(
                result,
                localSquaredErrorSums[
                    nodeId
                ][
                    unitIndex
                ],
                localSampleCounts[
                    nodeId
                ],
                outputTotals
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


        const double unitDivisor =
            static_cast<double>(
                unitCount
            );


        summary.meanSensitivity /=
            unitDivisor;


        summary.meanRoiSensitivity /=
            unitDivisor;


        summary.meanRoiToNonRoiRatio /=
            unitDivisor;


        // =================================================
        // 跨图片稳定性。
        //
        // 每张图片先对 attack units 取平均：
        //
        // S_i^(m) = mean_u S_(i,u)^(m)
        //
        // 然后再对所有图片计算 mean / std / CV。
        // =================================================

        for (double& imageSensitivity : perImageMeanSensitivity)
        {
            imageSensitivity /=
                unitDivisor;


            summary.meanImageSensitivity +=
                imageSensitivity;
        }


        const double imageDivisor =
            static_cast<double>(
                imageCount
            );


        summary.meanImageSensitivity /=
            imageDivisor;


        double squaredDeviationSum =
            0.0;


        for (const double imageSensitivity : perImageMeanSensitivity)
        {
            const double difference =
                imageSensitivity
                -
                summary.meanImageSensitivity;


            squaredDeviationSum +=
                difference
                *
                difference;
        }


        summary.stdImageSensitivity =
            std::sqrt(
                squaredDeviationSum
                /
                imageDivisor
            );


        if (
            summary.meanImageSensitivity
            >
            kEpsilon
        )
        {
            summary.cvImageSensitivity =
                summary.stdImageSensitivity
                /
                summary.meanImageSensitivity;
        }
        else
        {
            summary.cvImageSensitivity =
                0.0;
        }


        report.nodeSummaries.push_back(
            summary
        );
    }


    return report;
}

}

#include "analysis/dct_interval_full_validator.hpp"

#include "metrics/psnr.hpp"
#include "region/region_mask.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>


namespace analysis
{

namespace
{

double calculateNonRoiPsnr(
    const cv::Mat& referenceImage,
    const cv::Mat& testImage,
    const cv::Mat& roiMask
)
{
    if (
        referenceImage.empty()
        ||
        testImage.empty()
        ||
        roiMask.empty()
    )
    {
        throw std::runtime_error(
            "DCT Non-ROI PSNR input is empty."
        );
    }


    if (
        referenceImage.type() != CV_8UC1
        ||
        testImage.type() != CV_8UC1
        ||
        roiMask.type() != CV_8UC1
    )
    {
        throw std::runtime_error(
            "DCT Non-ROI PSNR requires CV_8UC1 images and mask."
        );
    }


    if (
        referenceImage.rows != testImage.rows
        ||
        referenceImage.cols != testImage.cols
        ||
        referenceImage.rows != roiMask.rows
        ||
        referenceImage.cols != roiMask.cols
    )
    {
        throw std::runtime_error(
            "DCT Non-ROI PSNR dimensions do not match."
        );
    }


    long double squaredErrorSum =
        0.0L;


    std::size_t pixelCount =
        0;


    for (int row = 0;
         row < referenceImage.rows;
         ++row)
    {
        for (int col = 0;
             col < referenceImage.cols;
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
                continue;
            }


            const long double difference =
                static_cast<long double>(
                    referenceImage.at<unsigned char>(
                        row,
                        col
                    )
                )
                -
                static_cast<long double>(
                    testImage.at<unsigned char>(
                        row,
                        col
                    )
                );


            squaredErrorSum +=
                difference
                *
                difference;


            ++pixelCount;
        }
    }


    if (pixelCount == 0)
    {
        throw std::runtime_error(
            "DCT Non-ROI PSNR mask contains no Non-ROI pixels."
        );
    }


    const long double mse =
        squaredErrorSum
        /
        static_cast<long double>(
            pixelCount
        );


    if (mse == 0.0L)
    {
        return
            std::numeric_limits<double>::infinity();
    }


    return
        10.0
        *
        std::log10(
            (255.0 * 255.0)
            /
            static_cast<double>(
                mse
            )
        );
}


DctImageQualityMetrics calculateMetrics(
    const cv::Mat& referenceImage,
    const cv::Mat& testImage,
    const cv::Mat& roiMask
)
{
    DctImageQualityMetrics
        result;


    result.globalPsnr =
        metrics::calculateGlobalPSNR(
            referenceImage,
            testImage
        );


    result.roiPsnr =
        metrics::calculateImportantRegionPSNR(
            referenceImage,
            testImage,
            roiMask
        );


    result.nonRoiPsnr =
        calculateNonRoiPsnr(
            referenceImage,
            testImage,
            roiMask
        );


    return result;
}


std::vector<core::AttackConfig>
replaceTargetConfiguration(
    const std::vector<core::AttackConfig>& currentConfiguration,
    int nodeId,
    core::MonitorInput monitorInput,
    approximate::ApproxUnitId attackUnit,
    const TriggerInterval& interval
)
{
    std::vector<core::AttackConfig>
        result =
            currentConfiguration;


    bool replaced =
        false;


    for (auto& config : result)
    {
        if (config.nodeId != nodeId)
        {
            continue;
        }


        if (replaced)
        {
            throw std::runtime_error(
                "Current DCT configuration contains duplicate target node entries."
            );
        }


        config.nodeId =
            nodeId;


        config.unit =
            attackUnit;


        config.monitorInput =
            monitorInput;


        config.lower =
            interval.lower;


        config.upper =
            interval.upper;


        replaced =
            true;
    }


    if (!replaced)
    {
        result.push_back(
            core::AttackConfig{
                nodeId,
                attackUnit,
                monitorInput,
                interval.lower,
                interval.upper
            }
        );
    }


    return result;
}

}


// =========================================================
// Module 2-B：真实完整 DCT 区间验证
// =========================================================

DctIntervalFullValidationReport
DctIntervalFullValidator::validate(
    const applications::DctApplication& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const std::vector<core::AttackConfig>& currentConfiguration,
    int nodeId,
    core::MonitorInput monitorInput,
    approximate::ApproxUnitId attackUnit,
    const std::vector<TriggerInterval>& candidateIntervals
)
{
    if (candidateIntervals.empty())
    {
        throw std::runtime_error(
            "DCT full interval validator received no candidate intervals."
        );
    }


    if (nodeId < 0)
    {
        throw std::runtime_error(
            "DCT full interval validator received a negative node ID."
        );
    }


    for (const auto& interval : candidateIntervals)
    {
        if (interval.lower > interval.upper)
        {
            throw std::runtime_error(
                "DCT full interval validator received an invalid interval."
            );
        }
    }


    const cv::Mat exactReference =
        application.runExact(
            inputImage
        );


    const cv::Mat currentImage =
        application.runApprox(
            inputImage,
            currentConfiguration
        );


    DctIntervalFullValidationReport
        report;


    report.currentMetrics =
        calculateMetrics(
            exactReference,
            currentImage,
            roiMask
        );


    report.candidates.reserve(
        candidateIntervals.size()
    );


    for (const auto& interval : candidateIntervals)
    {
        const auto candidateConfiguration =
            replaceTargetConfiguration(
                currentConfiguration,
                nodeId,
                monitorInput,
                attackUnit,
                interval
            );


        const cv::Mat candidateImage =
            application.runApprox(
                inputImage,
                candidateConfiguration
            );


        DctIntervalFullValidationResult
            result;


        result.interval =
            interval;


        result.metrics =
            calculateMetrics(
                exactReference,
                candidateImage,
                roiMask
            );


        result.globalPsnrDelta =
            result.metrics.globalPsnr
            -
            report.currentMetrics.globalPsnr;


        result.roiPsnrDelta =
            result.metrics.roiPsnr
            -
            report.currentMetrics.roiPsnr;


        result.nonRoiPsnrDelta =
            result.metrics.nonRoiPsnr
            -
            report.currentMetrics.nonRoiPsnr;


        report.candidates.push_back(
            result
        );
    }


    return report;
}

}

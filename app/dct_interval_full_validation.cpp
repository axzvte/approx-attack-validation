#include "analysis/dct_interval_fast_evaluator.hpp"
#include "analysis/dct_interval_full_validator.hpp"

#include "applications/dct.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{

std::string twoDigit(
    int value
)
{
    std::ostringstream stream;


    stream
        << std::setw(2)
        << std::setfill('0')
        << value;


    return stream.str();
}


std::vector<analysis::TriggerInterval>
toIntervals(
    const std::vector<analysis::DctIntervalFastMetric>& metrics
)
{
    std::vector<analysis::TriggerInterval>
        result;


    result.reserve(
        metrics.size()
    );


    for (const auto& metric : metrics)
    {
        result.push_back(
            metric.interval
        );
    }


    return result;
}


void printValidationGroup(
    const std::string& title,
    const std::vector<analysis::DctIntervalFastMetric>& fastMetrics,
    const analysis::DctIntervalFullValidationReport& report
)
{
    if (
        fastMetrics.size()
        !=
        report.candidates.size()
    )
    {
        throw std::runtime_error(
            "Fast and full DCT validation candidate counts do not match."
        );
    }


    std::cout
        << "\n"
        << title
        << "\n"
        << std::string(
            title.size(),
            '-'
        )
        << "\n";


    std::cout
        << std::left
        << std::setw(7)
        << "Rank"
        << std::setw(10)
        << "Lower"
        << std::setw(10)
        << "Upper"
        << std::setw(15)
        << "FastROIErr"
        << std::setw(17)
        << "FastNonROIErr"
        << std::setw(14)
        << "GlobalPSNR"
        << std::setw(13)
        << "dGlobal"
        << std::setw(14)
        << "ROIPSNR"
        << std::setw(13)
        << "dROI"
        << std::setw(14)
        << "NonROIPSNR"
        << "dNonROI"
        << "\n";


    for (std::size_t index = 0;
         index < fastMetrics.size();
         ++index)
    {
        const auto& fast =
            fastMetrics[index];


        const auto& full =
            report.candidates[index];


        std::cout
            << std::left
            << std::setw(7)
            << (index + 1)
            << std::setw(10)
            << fast.interval.lower
            << std::setw(10)
            << fast.interval.upper
            << std::setw(15)
            << fast.roiErrorChange
            << std::setw(17)
            << fast.nonRoiErrorChange
            << std::setw(14)
            << full.metrics.globalPsnr
            << std::setw(13)
            << full.globalPsnrDelta
            << std::setw(14)
            << full.metrics.roiPsnr
            << std::setw(13)
            << full.roiPsnrDelta
            << std::setw(14)
            << full.metrics.nonRoiPsnr
            << full.nonRoiPsnrDelta
            << "\n";
    }
}

}


int main(
    int argc,
    char** argv
)
{
    try
    {
        const std::filesystem::path dataRoot =
            (
                argc >= 2
            )
            ?
            std::filesystem::path(
                argv[1]
            )
            :
            std::filesystem::path(
                "data"
            );


        const int imageIndex =
            (
                argc >= 3
            )
            ?
            std::stoi(
                argv[2]
            )
            :
            1;


        if (
            imageIndex < 1
            ||
            imageIndex > 10
        )
        {
            throw std::runtime_error(
                "Stage 1 image index must be in [1, 10]."
            );
        }


        const cv::Mat inputImage =
            image_io::loadGrayImage(
                (
                    dataRoot
                    /
                    "stage1"
                    /
                    "input"
                    /
                    (
                        "image_"
                        +
                        twoDigit(
                            imageIndex
                        )
                        +
                        ".jpg"
                    )
                ).string()
            );


        const cv::Mat sharedRoiMask =
            image_io::loadGrayImage(
                (
                    dataRoot
                    /
                    "mask"
                    /
                    "roi_mask.jpg"
                ).string()
            );


        const cv::Mat roiMask =
            region_mask::resizeMaskToImage(
                sharedRoiMask,
                inputImage
            );


        applications::DctApplication
            application;


        std::vector<core::AddSample>
            samples;


        application.collectBaselineAddSamples(
            inputImage,
            roiMask,
            samples
        );


        constexpr int nodeId =
            6;


        constexpr core::MonitorInput monitorInput =
            core::MonitorInput::Input1;


        constexpr approximate::ApproxUnitId attackUnit =
            approximate::ApproxUnitId::Add12se5L8;


        const auto fastEvaluation =
            analysis::
                DctIntervalFastEvaluator::
                    evaluateFromSamples(
                        samples,
                        nodeId,
                        monitorInput,
                        attackUnit
                    );


        // 先验证每类前 3 个。
        //
        // 这一步的目的不是确定 Top-K，
        // 而是检查快速局部指标与最终图像 PSNR 方向是否一致。
        constexpr std::size_t
            validationCount =
                3;


        const auto roiAttack =
            analysis::
                DctIntervalFastEvaluator::
                    selectTopMetrics(
                        fastEvaluation,
                        analysis::DctIntervalFastRanking::RoiAttack,
                        validationCount
                    );


        const auto nonRoiCompensation =
            analysis::
                DctIntervalFastEvaluator::
                    selectTopMetrics(
                        fastEvaluation,
                        analysis::DctIntervalFastRanking::NonRoiCompensation,
                        validationCount
                    );


        const auto redistribution =
            analysis::
                DctIntervalFastEvaluator::
                    selectTopMetrics(
                        fastEvaluation,
                        analysis::DctIntervalFastRanking::Redistribution,
                        validationCount
                    );


        const std::vector<core::AttackConfig>
            currentConfiguration;


        const auto roiReport =
            analysis::
                DctIntervalFullValidator::
                    validate(
                        application,
                        inputImage,
                        roiMask,
                        currentConfiguration,
                        nodeId,
                        monitorInput,
                        attackUnit,
                        toIntervals(
                            roiAttack
                        )
                    );


        const auto compensationReport =
            analysis::
                DctIntervalFullValidator::
                    validate(
                        application,
                        inputImage,
                        roiMask,
                        currentConfiguration,
                        nodeId,
                        monitorInput,
                        attackUnit,
                        toIntervals(
                            nonRoiCompensation
                        )
                    );


        const auto redistributionReport =
            analysis::
                DctIntervalFullValidator::
                    validate(
                        application,
                        inputImage,
                        roiMask,
                        currentConfiguration,
                        nodeId,
                        monitorInput,
                        attackUnit,
                        toIntervals(
                            redistribution
                        )
                    );


        std::cout
            << "DCT fast-to-full interval validation (Module 2-B)\n"
            << "================================================\n"
            << "Image: image_"
            << twoDigit(
                imageIndex
            )
            << ".jpg\n"
            << "Node: 6\n"
            << "Monitor: input1\n"
            << "Attack unit: 5L8\n"
            << "Reference: exact DCT reconstruction\n"
            << "Current configuration: Baseline 5RP\n\n"
            << std::fixed
            << std::setprecision(
                6
            )
            << "Baseline Global PSNR: "
            << roiReport.currentMetrics.globalPsnr
            << "\n"
            << "Baseline ROI PSNR: "
            << roiReport.currentMetrics.roiPsnr
            << "\n"
            << "Baseline Non-ROI PSNR: "
            << roiReport.currentMetrics.nonRoiPsnr
            << "\n";


        printValidationGroup(
            "A. ROI attack candidates",
            roiAttack,
            roiReport
        );


        printValidationGroup(
            "B. Non-ROI compensation candidates",
            nonRoiCompensation,
            compensationReport
        );


        printValidationGroup(
            "C. Redistribution candidates",
            redistribution,
            redistributionReport
        );


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "DCT fast-to-full interval validation failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

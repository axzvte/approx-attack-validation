#include "analysis/dct_interval_fast_evaluator.hpp"
#include "analysis/dct_interval_full_validator.hpp"

#include "applications/dct.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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


std::string unitName(
    approximate::ApproxUnitId unit
)
{
    switch (unit)
    {
        case approximate::ApproxUnitId::Add12se5L8:
            return "5L8";

        case approximate::ApproxUnitId::Add12se5PD:
            return "5PD";

        case approximate::ApproxUnitId::Add12se5PN:
            return "5PN";

        case approximate::ApproxUnitId::Add12se5QC:
            return "5QC";

        case approximate::ApproxUnitId::Add12se5QT:
            return "5QT";

        case approximate::ApproxUnitId::Add12se5RP:
            return "5RP";

        case approximate::ApproxUnitId::Add12se5TE:
            return "5TE";

        case approximate::ApproxUnitId::Add12se5SB:
            return "5SB";

        case approximate::ApproxUnitId::Add12se5Z0:
            return "5Z0";
    }


    return "UNKNOWN";
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


struct UnitValidationSummary
{
    approximate::ApproxUnitId unit =
        approximate::ApproxUnitId::Add12se5RP;


    double maxFastRoiErrorChange = 0.0;
    double bestActualRoiDelta = 0.0;

    double minFastNonRoiErrorChange = 0.0;
    double bestActualNonRoiDelta = 0.0;

    double maxFastRedistribution = 0.0;
    double redistributionTop1RoiDelta = 0.0;
    double redistributionTop1NonRoiDelta = 0.0;
};


double minimumRoiDelta(
    const analysis::DctIntervalFullValidationReport& report
)
{
    double value =
        std::numeric_limits<double>::infinity();


    for (const auto& candidate : report.candidates)
    {
        value =
            std::min(
                value,
                candidate.roiPsnrDelta
            );
    }


    return value;
}


double maximumNonRoiDelta(
    const analysis::DctIntervalFullValidationReport& report
)
{
    double value =
        -std::numeric_limits<double>::infinity();


    for (const auto& candidate : report.candidates)
    {
        value =
            std::max(
                value,
                candidate.nonRoiPsnrDelta
            );
    }


    return value;
}


void writeGroupCsv(
    std::ofstream& file,
    const std::string& unit,
    const std::string& role,
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
            "Fast/full DCT candidate counts do not match."
        );
    }


    for (std::size_t index = 0;
         index < fastMetrics.size();
         ++index)
    {
        const auto& fast =
            fastMetrics[index];


        const auto& full =
            report.candidates[index];


        file
            << unit
            << ","
            << role
            << ","
            << (index + 1)
            << ","
            << fast.interval.lower
            << ","
            << fast.interval.upper
            << ","
            << fast.roiTriggerRate
            << ","
            << fast.nonRoiTriggerRate
            << ","
            << fast.roiErrorChange
            << ","
            << fast.nonRoiErrorChange
            << ","
            << fast.redistributionScore
            << ","
            << full.metrics.globalPsnr
            << ","
            << full.globalPsnrDelta
            << ","
            << full.metrics.roiPsnr
            << ","
            << full.roiPsnrDelta
            << ","
            << full.metrics.nonRoiPsnr
            << ","
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


        const std::filesystem::path outputPath =
            (
                argc >= 4
            )
            ?
            std::filesystem::path(
                argv[3]
            )
            :
            std::filesystem::path(
                "dct_unit_interval_validation.csv"
            );


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


        const std::vector<approximate::ApproxUnitId>
            attackUnits =
        {
            approximate::ApproxUnitId::Add12se5L8,
            approximate::ApproxUnitId::Add12se5PD,
            approximate::ApproxUnitId::Add12se5PN,
            approximate::ApproxUnitId::Add12se5QC,
            approximate::ApproxUnitId::Add12se5QT,
            approximate::ApproxUnitId::Add12se5TE,
            approximate::ApproxUnitId::Add12se5SB,
            approximate::ApproxUnitId::Add12se5Z0
        };


        // 仍然只验证每类快速排名前 3 个。
        //
        // 当前目的只是判断“不同 unit 的快速方向是否能在完整 DCT 中体现”，
        // 不是在这里决定正式 Top-K。
        constexpr std::size_t
            validationCount =
                3;


        const std::vector<core::AttackConfig>
            currentConfiguration;


        std::ofstream csv(
            outputPath
        );


        if (!csv.is_open())
        {
            throw std::runtime_error(
                "Unable to open DCT unit validation CSV."
            );
        }


        csv
            << std::setprecision(12)
            << "unit,role,rank,lower,upper,"
            << "roi_trigger_rate,non_roi_trigger_rate,"
            << "fast_roi_error_change,fast_non_roi_error_change,"
            << "fast_redistribution,"
            << "global_psnr,d_global,"
            << "roi_psnr,d_roi,"
            << "non_roi_psnr,d_non_roi\n";


        std::vector<UnitValidationSummary>
            summaries;


        summaries.reserve(
            attackUnits.size()
        );


        bool baselinePrinted =
            false;


        for (const auto attackUnit : attackUnits)
        {
            const auto fastEvaluation =
                analysis::
                    DctIntervalFastEvaluator::
                        evaluateFromSamples(
                            samples,
                            nodeId,
                            monitorInput,
                            attackUnit
                        );


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


            if (!baselinePrinted)
            {
                std::cout
                    << "DCT unit comparison for interval behavior\n"
                    << "=========================================\n"
                    << "Image: image_"
                    << twoDigit(
                        imageIndex
                    )
                    << ".jpg\n"
                    << "Node: 6\n"
                    << "Monitor: input1\n"
                    << "Reference: exact DCT reconstruction\n"
                    << "Current configuration: Baseline 5RP\n"
                    << "Validated candidates per role/unit: "
                    << validationCount
                    << "\n\n"
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
                    << "\n\n";


                baselinePrinted =
                    true;
            }


            writeGroupCsv(
                csv,
                unitName(
                    attackUnit
                ),
                "roi_attack",
                roiAttack,
                roiReport
            );


            writeGroupCsv(
                csv,
                unitName(
                    attackUnit
                ),
                "non_roi_compensation",
                nonRoiCompensation,
                compensationReport
            );


            writeGroupCsv(
                csv,
                unitName(
                    attackUnit
                ),
                "redistribution",
                redistribution,
                redistributionReport
            );


            UnitValidationSummary
                summary;


            summary.unit =
                attackUnit;


            summary.maxFastRoiErrorChange =
                roiAttack.front().roiErrorChange;


            summary.bestActualRoiDelta =
                minimumRoiDelta(
                    roiReport
                );


            summary.minFastNonRoiErrorChange =
                nonRoiCompensation.front().nonRoiErrorChange;


            summary.bestActualNonRoiDelta =
                maximumNonRoiDelta(
                    compensationReport
                );


            summary.maxFastRedistribution =
                redistribution.front().redistributionScore;


            summary.redistributionTop1RoiDelta =
                redistributionReport.candidates.front().roiPsnrDelta;


            summary.redistributionTop1NonRoiDelta =
                redistributionReport.candidates.front().nonRoiPsnrDelta;


            summaries.push_back(
                summary
            );
        }


        std::cout
            << std::left
            << std::setw(8)
            << "Unit"
            << std::setw(16)
            << "MaxFastROIErr"
            << std::setw(15)
            << "Best_dROI"
            << std::setw(19)
            << "MinFastNonROIErr"
            << std::setw(18)
            << "Best_dNonROI"
            << std::setw(18)
            << "MaxFastRedis"
            << std::setw(18)
            << "RedisTop1_dROI"
            << "RedisTop1_dNonROI"
            << "\n";


        for (const auto& summary : summaries)
        {
            std::cout
                << std::left
                << std::setw(8)
                << unitName(
                    summary.unit
                )
                << std::setw(16)
                << summary.maxFastRoiErrorChange
                << std::setw(15)
                << summary.bestActualRoiDelta
                << std::setw(19)
                << summary.minFastNonRoiErrorChange
                << std::setw(18)
                << summary.bestActualNonRoiDelta
                << std::setw(18)
                << summary.maxFastRedistribution
                << std::setw(18)
                << summary.redistributionTop1RoiDelta
                << summary.redistributionTop1NonRoiDelta
                << "\n";
        }


        std::cout
            << "\nDetailed CSV: "
            << outputPath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "DCT unit interval validation failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

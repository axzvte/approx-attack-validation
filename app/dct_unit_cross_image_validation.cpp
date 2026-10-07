#include "analysis/dct_interval_fast_evaluator.hpp"
#include "analysis/dct_interval_full_validator.hpp"

#include "applications/dct.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace
{

constexpr double kGlobalPsnrThreshold =
    30.0;


constexpr std::size_t kValidationCountPerRole =
    3;


constexpr int kNodeId =
    6;


constexpr core::MonitorInput kMonitorInput =
    core::MonitorInput::Input1;


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


using IntervalKey =
    std::pair<int, int>;


IntervalKey intervalKey(
    const analysis::TriggerInterval& interval
)
{
    return
    {
        interval.lower,
        interval.upper
    };
}


void addUniqueIntervals(
    const std::vector<
        analysis::DctIntervalFastMetric
    >& metrics,
    std::vector<analysis::TriggerInterval>& intervals,
    std::map<IntervalKey, std::size_t>& indexByInterval
)
{
    for (const auto& metric : metrics)
    {
        const IntervalKey key =
            intervalKey(
                metric.interval
            );


        if (
            indexByInterval.find(
                key
            )
            !=
            indexByInterval.end()
        )
        {
            continue;
        }


        indexByInterval[
            key
        ] =
            intervals.size();


        intervals.push_back(
            metric.interval
        );
    }
}


const analysis::DctIntervalFullValidationResult&
findFullResult(
    const analysis::DctIntervalFullValidationReport& report,
    const std::map<IntervalKey, std::size_t>& indexByInterval,
    const analysis::TriggerInterval& interval
)
{
    const auto iterator =
        indexByInterval.find(
            intervalKey(
                interval
            )
        );


    if (
        iterator
        ==
        indexByInterval.end()
        ||
        iterator->second
        >=
        report.candidates.size()
    )
    {
        throw std::runtime_error(
            "Unable to match fast interval with full DCT validation result."
        );
    }


    return
        report.candidates[
            iterator->second
        ];
}


struct SelectedCandidate
{
    analysis::TriggerInterval interval;

    analysis::DctIntervalFastMetric fast;

    analysis::DctIntervalFullValidationResult full;
};


SelectedCandidate selectStrongestRoiAttack(
    const std::vector<
        analysis::DctIntervalFastMetric
    >& fastMetrics,
    const analysis::DctIntervalFullValidationReport& report,
    const std::map<IntervalKey, std::size_t>& indexByInterval
)
{
    if (fastMetrics.empty())
    {
        throw std::runtime_error(
            "ROI attack candidate list is empty."
        );
    }


    std::optional<SelectedCandidate>
        best;


    for (const auto& fast : fastMetrics)
    {
        const auto& full =
            findFullResult(
                report,
                indexByInterval,
                fast.interval
            );


        if (
            !best.has_value()
            ||
            full.roiPsnrDelta
                <
                best->full.roiPsnrDelta
        )
        {
            best =
                SelectedCandidate{
                    fast.interval,
                    fast,
                    full
                };
        }
    }


    return
        *best;
}


std::optional<SelectedCandidate>
selectStrongestFeasibleRoiAttack(
    const std::vector<
        analysis::DctIntervalFastMetric
    >& fastMetrics,
    const analysis::DctIntervalFullValidationReport& report,
    const std::map<IntervalKey, std::size_t>& indexByInterval
)
{
    std::optional<SelectedCandidate>
        best;


    for (const auto& fast : fastMetrics)
    {
        const auto& full =
            findFullResult(
                report,
                indexByInterval,
                fast.interval
            );


        if (
            full.metrics.globalPsnr
            <
            kGlobalPsnrThreshold
        )
        {
            continue;
        }


        if (
            !best.has_value()
            ||
            full.roiPsnrDelta
                <
                best->full.roiPsnrDelta
        )
        {
            best =
                SelectedCandidate{
                    fast.interval,
                    fast,
                    full
                };
        }
    }


    return best;
}


SelectedCandidate selectStrongestNonRoiCompensation(
    const std::vector<
        analysis::DctIntervalFastMetric
    >& fastMetrics,
    const analysis::DctIntervalFullValidationReport& report,
    const std::map<IntervalKey, std::size_t>& indexByInterval
)
{
    if (fastMetrics.empty())
    {
        throw std::runtime_error(
            "Non-ROI compensation candidate list is empty."
        );
    }


    std::optional<SelectedCandidate>
        best;


    for (const auto& fast : fastMetrics)
    {
        const auto& full =
            findFullResult(
                report,
                indexByInterval,
                fast.interval
            );


        if (
            !best.has_value()
            ||
            full.nonRoiPsnrDelta
                >
                best->full.nonRoiPsnrDelta
        )
        {
            best =
                SelectedCandidate{
                    fast.interval,
                    fast,
                    full
                };
        }
    }


    return
        *best;
}


SelectedCandidate selectStrongestActualRedistribution(
    const std::vector<
        analysis::DctIntervalFastMetric
    >& fastMetrics,
    const analysis::DctIntervalFullValidationReport& report,
    const std::map<IntervalKey, std::size_t>& indexByInterval
)
{
    if (fastMetrics.empty())
    {
        throw std::runtime_error(
            "Redistribution candidate list is empty."
        );
    }


    std::optional<SelectedCandidate>
        best;


    double bestGap =
        -std::numeric_limits<double>::infinity();


    for (const auto& fast : fastMetrics)
    {
        const auto& full =
            findFullResult(
                report,
                indexByInterval,
                fast.interval
            );


        // dROI 越负越差，dNonROI 越正越好。
        //
        // 因而：
        // dNonROI - dROI
        //
        // 越大，表示最终图像质量越倾向于
        // “ROI 更差 / Non-ROI 更好”。
        const double actualGap =
            full.nonRoiPsnrDelta
            -
            full.roiPsnrDelta;


        if (
            !best.has_value()
            ||
            actualGap
            >
            bestGap
        )
        {
            bestGap =
                actualGap;


            best =
                SelectedCandidate{
                    fast.interval,
                    fast,
                    full
                };
        }
    }


    return
        *best;
}


struct PerImageUnitResult
{
    int imageIndex = 0;

    approximate::ApproxUnitId unit =
        approximate::ApproxUnitId::Add12se5RP;


    double baselineGlobalPsnr = 0.0;
    double baselineRoiPsnr = 0.0;
    double baselineNonRoiPsnr = 0.0;


    SelectedCandidate roiAttack;

    std::optional<SelectedCandidate>
        feasibleRoiAttack;

    SelectedCandidate nonRoiCompensation;

    SelectedCandidate redistribution;
};


struct UnitSummary
{
    approximate::ApproxUnitId unit =
        approximate::ApproxUnitId::Add12se5RP;


    double sumBestRoiDelta = 0.0;
    int roiAttackImageCount = 0;


    double sumFeasibleRoiDelta = 0.0;
    int feasibleRoiCandidateImageCount = 0;
    int feasibleRoiAttackImageCount = 0;


    double sumBestNonRoiDelta = 0.0;
    int compensationImageCount = 0;


    double sumRedistributionGap = 0.0;
    int redistributionImageCount = 0;


    int imageCount = 0;
};


void updateSummary(
    UnitSummary& summary,
    const PerImageUnitResult& result
)
{
    ++summary.imageCount;


    summary.sumBestRoiDelta +=
        result.roiAttack.full.roiPsnrDelta;


    if (
        result.roiAttack.full.roiPsnrDelta
        <
        0.0
    )
    {
        ++summary.roiAttackImageCount;
    }


    if (
        result.feasibleRoiAttack.has_value()
    )
    {
        ++summary.feasibleRoiCandidateImageCount;


        summary.sumFeasibleRoiDelta +=
            result.feasibleRoiAttack->
                full.roiPsnrDelta;


        if (
            result.feasibleRoiAttack->
                full.roiPsnrDelta
            <
            0.0
        )
        {
            ++summary.feasibleRoiAttackImageCount;
        }
    }


    summary.sumBestNonRoiDelta +=
        result.nonRoiCompensation.
            full.nonRoiPsnrDelta;


    if (
        result.nonRoiCompensation.
            full.nonRoiPsnrDelta
        >
        0.0
    )
    {
        ++summary.compensationImageCount;
    }


    const double redistributionGap =
        result.redistribution.
            full.nonRoiPsnrDelta
        -
        result.redistribution.
            full.roiPsnrDelta;


    summary.sumRedistributionGap +=
        redistributionGap;


    if (redistributionGap > 0.0)
    {
        ++summary.redistributionImageCount;
    }
}


void writeCandidateColumns(
    std::ofstream& csv,
    const SelectedCandidate& candidate
)
{
    csv
        << candidate.interval.lower
        << ","
        << candidate.interval.upper
        << ","
        << candidate.fast.roiErrorChange
        << ","
        << candidate.fast.nonRoiErrorChange
        << ","
        << candidate.fast.redistributionScore
        << ","
        << candidate.full.metrics.globalPsnr
        << ","
        << candidate.full.globalPsnrDelta
        << ","
        << candidate.full.metrics.roiPsnr
        << ","
        << candidate.full.roiPsnrDelta
        << ","
        << candidate.full.metrics.nonRoiPsnr
        << ","
        << candidate.full.nonRoiPsnrDelta;
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


        const std::filesystem::path detailCsvPath =
            (
                argc >= 3
            )
            ?
            std::filesystem::path(
                argv[2]
            )
            :
            std::filesystem::path(
                "dct_unit_cross_image_details.csv"
            );


        const std::filesystem::path summaryCsvPath =
            (
                argc >= 4
            )
            ?
            std::filesystem::path(
                argv[3]
            )
            :
            std::filesystem::path(
                "dct_unit_cross_image_summary.csv"
            );


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


        applications::DctApplication
            application;


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


        std::ofstream detailCsv(
            detailCsvPath
        );


        if (!detailCsv.is_open())
        {
            throw std::runtime_error(
                "Unable to open DCT cross-image detail CSV."
            );
        }


        detailCsv
            << std::setprecision(12)
            << "image,unit,"
            << "baseline_global_psnr,baseline_roi_psnr,baseline_non_roi_psnr,"
            << "attack_lower,attack_upper,"
            << "attack_fast_roi_error,attack_fast_non_roi_error,attack_fast_redis,"
            << "attack_global_psnr,attack_d_global,"
            << "attack_roi_psnr,attack_d_roi,"
            << "attack_non_roi_psnr,attack_d_non_roi,"
            << "feasible_attack_exists,feasible_attack_lower,feasible_attack_upper,"
            << "feasible_attack_global_psnr,feasible_attack_d_roi,"
            << "comp_lower,comp_upper,"
            << "comp_fast_roi_error,comp_fast_non_roi_error,comp_fast_redis,"
            << "comp_global_psnr,comp_d_global,"
            << "comp_roi_psnr,comp_d_roi,"
            << "comp_non_roi_psnr,comp_d_non_roi,"
            << "redis_lower,redis_upper,"
            << "redis_fast_roi_error,redis_fast_non_roi_error,redis_fast_redis,"
            << "redis_global_psnr,redis_d_global,"
            << "redis_roi_psnr,redis_d_roi,"
            << "redis_non_roi_psnr,redis_d_non_roi\n";


        std::map<
            approximate::ApproxUnitId,
            UnitSummary
        >
            summaries;


        for (const auto unit : attackUnits)
        {
            UnitSummary summary;
            summary.unit = unit;


            summaries[
                unit
            ] =
                summary;
        }


        const std::vector<core::AttackConfig>
            baselineConfiguration;


        std::cout
            << "DCT cross-image unit validation\n"
            << "===============================\n"
            << "Node: "
            << kNodeId
            << "\n"
            << "Monitor: input1\n"
            << "Stage 1 images: 10\n"
            << "Units: 8\n"
            << "Fast candidates validated per role: "
            << kValidationCountPerRole
            << "\n"
            << "Global PSNR feasibility threshold: "
            << kGlobalPsnrThreshold
            << " dB\n\n";


        for (int imageIndex = 1;
             imageIndex <= 10;
             ++imageIndex)
        {
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


            const cv::Mat roiMask =
                region_mask::resizeMaskToImage(
                    sharedRoiMask,
                    inputImage
                );


            std::vector<core::AddSample>
                samples;


            application.collectBaselineAddSamples(
                inputImage,
                roiMask,
                samples
            );


            for (std::size_t unitIndex = 0;
                 unitIndex < attackUnits.size();
                 ++unitIndex)
            {
                const auto unit =
                    attackUnits[
                        unitIndex
                    ];


                std::cout
                    << "[image "
                    << imageIndex
                    << "/10] [unit "
                    << (unitIndex + 1)
                    << "/"
                    << attackUnits.size()
                    << "] "
                    << unitName(
                        unit
                    )
                    << "\n";


                const auto fastEvaluation =
                    analysis::
                        DctIntervalFastEvaluator::
                            evaluateFromSamples(
                                samples,
                                kNodeId,
                                kMonitorInput,
                                unit
                            );


                const auto roiAttackMetrics =
                    analysis::
                        DctIntervalFastEvaluator::
                            selectTopMetrics(
                                fastEvaluation,
                                analysis::DctIntervalFastRanking::RoiAttack,
                                kValidationCountPerRole
                            );


                const auto compensationMetrics =
                    analysis::
                        DctIntervalFastEvaluator::
                            selectTopMetrics(
                                fastEvaluation,
                                analysis::DctIntervalFastRanking::NonRoiCompensation,
                                kValidationCountPerRole
                            );


                const auto redistributionMetrics =
                    analysis::
                        DctIntervalFastEvaluator::
                            selectTopMetrics(
                                fastEvaluation,
                                analysis::DctIntervalFastRanking::Redistribution,
                                kValidationCountPerRole
                            );


                std::vector<analysis::TriggerInterval>
                    uniqueIntervals;


                std::map<IntervalKey, std::size_t>
                    indexByInterval;


                addUniqueIntervals(
                    roiAttackMetrics,
                    uniqueIntervals,
                    indexByInterval
                );


                addUniqueIntervals(
                    compensationMetrics,
                    uniqueIntervals,
                    indexByInterval
                );


                addUniqueIntervals(
                    redistributionMetrics,
                    uniqueIntervals,
                    indexByInterval
                );


                const auto fullReport =
                    analysis::
                        DctIntervalFullValidator::
                            validate(
                                application,
                                inputImage,
                                roiMask,
                                baselineConfiguration,
                                kNodeId,
                                kMonitorInput,
                                unit,
                                uniqueIntervals
                            );


                PerImageUnitResult
                    result;


                result.imageIndex =
                    imageIndex;


                result.unit =
                    unit;


                result.baselineGlobalPsnr =
                    fullReport.currentMetrics.
                        globalPsnr;


                result.baselineRoiPsnr =
                    fullReport.currentMetrics.
                        roiPsnr;


                result.baselineNonRoiPsnr =
                    fullReport.currentMetrics.
                        nonRoiPsnr;


                result.roiAttack =
                    selectStrongestRoiAttack(
                        roiAttackMetrics,
                        fullReport,
                        indexByInterval
                    );


                result.feasibleRoiAttack =
                    selectStrongestFeasibleRoiAttack(
                        roiAttackMetrics,
                        fullReport,
                        indexByInterval
                    );


                result.nonRoiCompensation =
                    selectStrongestNonRoiCompensation(
                        compensationMetrics,
                        fullReport,
                        indexByInterval
                    );


                result.redistribution =
                    selectStrongestActualRedistribution(
                        redistributionMetrics,
                        fullReport,
                        indexByInterval
                    );


                updateSummary(
                    summaries[
                        unit
                    ],
                    result
                );


                detailCsv
                    << imageIndex
                    << ","
                    << unitName(
                        unit
                    )
                    << ","
                    << result.baselineGlobalPsnr
                    << ","
                    << result.baselineRoiPsnr
                    << ","
                    << result.baselineNonRoiPsnr
                    << ",";


                writeCandidateColumns(
                    detailCsv,
                    result.roiAttack
                );


                detailCsv
                    << ","
                    << (
                        result.feasibleRoiAttack.
                            has_value()
                        ?
                        1
                        :
                        0
                    )
                    << ",";


                if (
                    result.feasibleRoiAttack.
                        has_value()
                )
                {
                    detailCsv
                        << result.feasibleRoiAttack->
                            interval.lower
                        << ","
                        << result.feasibleRoiAttack->
                            interval.upper
                        << ","
                        << result.feasibleRoiAttack->
                            full.metrics.globalPsnr
                        << ","
                        << result.feasibleRoiAttack->
                            full.roiPsnrDelta;
                }
                else
                {
                    detailCsv
                        << ",,,";
                }


                detailCsv
                    << ",";


                writeCandidateColumns(
                    detailCsv,
                    result.nonRoiCompensation
                );


                detailCsv
                    << ",";


                writeCandidateColumns(
                    detailCsv,
                    result.redistribution
                );


                detailCsv
                    << "\n";
            }
        }


        std::ofstream summaryCsv(
            summaryCsvPath
        );


        if (!summaryCsv.is_open())
        {
            throw std::runtime_error(
                "Unable to open DCT cross-image summary CSV."
            );
        }


        summaryCsv
            << std::setprecision(12)
            << "unit,"
            << "mean_best_d_roi,attack_image_count,"
            << "mean_feasible_d_roi,feasible_candidate_image_count,feasible_attack_image_count,"
            << "mean_best_d_non_roi,compensation_image_count,"
            << "mean_actual_redistribution_gap,redistribution_image_count\n";


        std::cout
            << "\nCross-image summary\n"
            << "-------------------\n"
            << std::fixed
            << std::setprecision(
                6
            )
            << std::left
            << std::setw(7)
            << "Unit"
            << std::setw(16)
            << "MeanBest_dROI"
            << std::setw(13)
            << "AttackImgs"
            << std::setw(18)
            << "MeanFeas_dROI"
            << std::setw(16)
            << "FeasAtkImgs"
            << std::setw(19)
            << "MeanBest_dNonROI"
            << std::setw(12)
            << "CompImgs"
            << std::setw(17)
            << "MeanRedisGap"
            << "RedisImgs"
            << "\n";


        for (const auto unit : attackUnits)
        {
            const auto& summary =
                summaries.at(
                    unit
                );


            const double meanBestRoiDelta =
                summary.sumBestRoiDelta
                /
                static_cast<double>(
                    summary.imageCount
                );


            const double meanFeasibleRoiDelta =
                summary.feasibleRoiCandidateImageCount
                >
                0
                ?
                summary.sumFeasibleRoiDelta
                /
                static_cast<double>(
                    summary.feasibleRoiCandidateImageCount
                )
                :
                std::numeric_limits<double>::quiet_NaN();


            const double meanBestNonRoiDelta =
                summary.sumBestNonRoiDelta
                /
                static_cast<double>(
                    summary.imageCount
                );


            const double meanRedistributionGap =
                summary.sumRedistributionGap
                /
                static_cast<double>(
                    summary.imageCount
                );


            summaryCsv
                << unitName(
                    unit
                )
                << ","
                << meanBestRoiDelta
                << ","
                << summary.roiAttackImageCount
                << ","
                << meanFeasibleRoiDelta
                << ","
                << summary.feasibleRoiCandidateImageCount
                << ","
                << summary.feasibleRoiAttackImageCount
                << ","
                << meanBestNonRoiDelta
                << ","
                << summary.compensationImageCount
                << ","
                << meanRedistributionGap
                << ","
                << summary.redistributionImageCount
                << "\n";


            std::cout
                << std::left
                << std::setw(7)
                << unitName(
                    unit
                )
                << std::setw(16)
                << meanBestRoiDelta
                << std::setw(13)
                << (
                    std::to_string(
                        summary.roiAttackImageCount
                    )
                    +
                    "/10"
                )
                << std::setw(18)
                << meanFeasibleRoiDelta
                << std::setw(16)
                << (
                    std::to_string(
                        summary.feasibleRoiAttackImageCount
                    )
                    +
                    "/10"
                )
                << std::setw(19)
                << meanBestNonRoiDelta
                << std::setw(12)
                << (
                    std::to_string(
                        summary.compensationImageCount
                    )
                    +
                    "/10"
                )
                << std::setw(17)
                << meanRedistributionGap
                << summary.redistributionImageCount
                << "/10"
                << "\n";
        }


        std::cout
            << "\nDetail CSV: "
            << detailCsvPath.string()
            << "\n"
            << "Summary CSV: "
            << summaryCsvPath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "DCT cross-image unit validation failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

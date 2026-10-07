#include "analysis/dct_interval_fast_evaluator.hpp"
#include "analysis/dct_interval_full_validator.hpp"

#include "applications/dct.hpp"
#include "applications/dct8_fixed_graph.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{

struct BaselineExperiment
{
    std::string name;

    applications::Dct8FixedGraph::BaselineConfig
        baselineConfig;
};


struct AggregateResult
{
    double baselineGlobalSum = 0.0;
    double baselineRoiSum = 0.0;
    double baselineNonRoiSum = 0.0;

    double feasibleAttackGlobalSum = 0.0;
    double feasibleAttackRoiSum = 0.0;
    double feasibleAttackNonRoiSum = 0.0;

    std::size_t feasibleAttackImageCount = 0;
};


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
    const std::vector<
        analysis::DctIntervalFastMetric
    >& metrics
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


const analysis::DctIntervalFullValidationResult*
findBestFeasibleAttack(
    const analysis::DctIntervalFullValidationReport& report,
    double globalPsnrThreshold
)
{
    const analysis::DctIntervalFullValidationResult*
        best =
            nullptr;


    for (const auto& candidate : report.candidates)
    {
        if (
            candidate.metrics.globalPsnr
            <
            globalPsnrThreshold
        )
        {
            continue;
        }


        if (
            best == nullptr
            ||
            candidate.metrics.roiPsnr
                <
                best->metrics.roiPsnr
        )
        {
            best =
                &candidate;
        }
    }


    return best;
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


        constexpr int attackNode =
            6;


        constexpr core::MonitorSignal monitorSignal =
            core::MonitorSignal::Input1;


        constexpr approximate::ApproxUnitId attackUnit =
            approximate::ApproxUnitId::Add12se5Z0;


        constexpr std::size_t representativeCount =
            20;


        constexpr double globalPsnrThreshold =
            30.0;


        const std::vector<BaselineExperiment>
            experiments =
        {
            {
                "All-5RP",
                applications::Dct8FixedGraph::
                    createAllApproximateBaselineConfig(
                        approximate::ApproxUnitId::Add12se5RP
                    )
            },

            {
                "Sparse-2",
                applications::Dct8FixedGraph::
                    createSparseApproximateBaselineConfig(
                        {
                            25,
                            19
                        },
                        approximate::ApproxUnitId::Add12se5RP
                    )
            },

            {
                "Sparse-3",
                applications::Dct8FixedGraph::
                    createSparseApproximateBaselineConfig(
                        {
                            25,
                            19,
                            13
                        },
                        approximate::ApproxUnitId::Add12se5RP
                    )
            }
        };


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


        std::vector<AggregateResult>
            aggregates(
                experiments.size()
            );


        std::cout
            << "DCT sparse-baseline validation\n"
            << "==============================\n"
            << "Stage 1 images: 10\n"
            << "Legacy baseline: all 32 ADD/SUB nodes use 5RP\n"
            << "Sparse-2: nodes 25, 19 use 5RP; all others exact\n"
            << "Sparse-3: nodes 25, 19, 13 use 5RP; all others exact\n"
            << "Attack probe: Node 6 / input1 / 5Z0\n"
            << "Representative intervals per image: "
            << representativeCount
            << "\n"
            << "Final attack feasibility: Global PSNR >= "
            << globalPsnrThreshold
            << " dB\n\n";


        std::cout
            << std::left
            << std::setw(8)
            << "Image"
            << std::setw(12)
            << "Baseline"
            << std::setw(14)
            << "BaseGlobal"
            << std::setw(14)
            << "BaseROI"
            << std::setw(14)
            << "BaseNonROI"
            << std::setw(14)
            << "AttackGlobal"
            << std::setw(14)
            << "AttackROI"
            << std::setw(14)
            << "AttackNonROI"
            << "Interval"
            << "\n";


        std::cout
            << std::fixed
            << std::setprecision(
                6
            );


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


            for (std::size_t experimentIndex = 0;
                 experimentIndex < experiments.size();
                 ++experimentIndex)
            {
                const auto& experiment =
                    experiments[
                        experimentIndex
                    ];


                applications::DctApplication
                    application(
                        experiment.baselineConfig
                    );


                const auto baselineMetrics =
                    analysis::
                        DctIntervalFullValidator::
                            evaluateConfiguration(
                                application,
                                inputImage,
                                roiMask,
                                {}
                            );


                auto& aggregate =
                    aggregates[
                        experimentIndex
                    ];


                aggregate.baselineGlobalSum +=
                    baselineMetrics.globalPsnr;


                aggregate.baselineRoiSum +=
                    baselineMetrics.roiPsnr;


                aggregate.baselineNonRoiSum +=
                    baselineMetrics.nonRoiPsnr;


                std::vector<core::AddSample>
                    samples;


                application.collectBaselineAddSamples(
                    inputImage,
                    roiMask,
                    samples
                );


                const auto fastEvaluation =
                    analysis::
                        DctIntervalFastEvaluator::
                            evaluateFromSamples(
                                samples,
                                attackNode,
                                monitorSignal,
                                attackUnit
                            );


                const auto representativeSelection =
                    analysis::
                        DctIntervalFastEvaluator::
                            selectRepresentativeMetrics(
                                fastEvaluation,
                                representativeCount
                            );


                const auto fullReport =
                    analysis::
                        DctIntervalFullValidator::
                            validate(
                                application,
                                inputImage,
                                roiMask,
                                {},
                                attackNode,
                                monitorSignal,
                                attackUnit,
                                toIntervals(
                                    representativeSelection.
                                        representatives
                                )
                            );


                const auto* bestAttack =
                    findBestFeasibleAttack(
                        fullReport,
                        globalPsnrThreshold
                    );


                std::cout
                    << std::left
                    << std::setw(8)
                    << imageIndex
                    << std::setw(12)
                    << experiment.name
                    << std::setw(14)
                    << baselineMetrics.globalPsnr
                    << std::setw(14)
                    << baselineMetrics.roiPsnr
                    << std::setw(14)
                    << baselineMetrics.nonRoiPsnr;


                if (bestAttack != nullptr)
                {
                    aggregate.feasibleAttackGlobalSum +=
                        bestAttack->metrics.globalPsnr;


                    aggregate.feasibleAttackRoiSum +=
                        bestAttack->metrics.roiPsnr;


                    aggregate.feasibleAttackNonRoiSum +=
                        bestAttack->metrics.nonRoiPsnr;


                    ++aggregate.feasibleAttackImageCount;


                    std::ostringstream intervalText;


                    intervalText
                        << "["
                        << bestAttack->interval.lower
                        << ","
                        << bestAttack->interval.upper
                        << "]";


                    std::cout
                        << std::setw(14)
                        << bestAttack->metrics.globalPsnr
                        << std::setw(14)
                        << bestAttack->metrics.roiPsnr
                        << std::setw(14)
                        << bestAttack->metrics.nonRoiPsnr
                        << intervalText.str();
                }
                else
                {
                    std::cout
                        << std::setw(14)
                        << "N/A"
                        << std::setw(14)
                        << "N/A"
                        << std::setw(14)
                        << "N/A"
                        << "-";
                }


                std::cout
                    << "\n";
            }
        }


        std::cout
            << "\nSummary over 10 Stage 1 images\n"
            << "------------------------------\n"
            << std::left
            << std::setw(12)
            << "Baseline"
            << std::setw(15)
            << "MeanBaseGlob"
            << std::setw(15)
            << "MeanBaseROI"
            << std::setw(15)
            << "MeanBaseNonROI"
            << std::setw(15)
            << "MeanAtkGlob"
            << std::setw(15)
            << "MeanAtkROI"
            << std::setw(15)
            << "MeanAtkNonROI"
            << "FeasibleImgs"
            << "\n";


        for (std::size_t experimentIndex = 0;
             experimentIndex < experiments.size();
             ++experimentIndex)
        {
            const auto& experiment =
                experiments[
                    experimentIndex
                ];


            const auto& aggregate =
                aggregates[
                    experimentIndex
                ];


            const double baselineDivisor =
                10.0;


            std::cout
                << std::left
                << std::setw(12)
                << experiment.name
                << std::setw(15)
                << aggregate.baselineGlobalSum
                    /
                    baselineDivisor
                << std::setw(15)
                << aggregate.baselineRoiSum
                    /
                    baselineDivisor
                << std::setw(15)
                << aggregate.baselineNonRoiSum
                    /
                    baselineDivisor;


            if (
                aggregate.feasibleAttackImageCount
                >
                0
            )
            {
                const double attackDivisor =
                    static_cast<double>(
                        aggregate.feasibleAttackImageCount
                    );


                std::cout
                    << std::setw(15)
                    << aggregate.feasibleAttackGlobalSum
                        /
                        attackDivisor
                    << std::setw(15)
                    << aggregate.feasibleAttackRoiSum
                        /
                        attackDivisor
                    << std::setw(15)
                    << aggregate.feasibleAttackNonRoiSum
                        /
                        attackDivisor;
            }
            else
            {
                std::cout
                    << std::setw(15)
                    << "N/A"
                    << std::setw(15)
                    << "N/A"
                    << std::setw(15)
                    << "N/A";
            }


            std::cout
                << aggregate.feasibleAttackImageCount
                << "/10\n";
        }


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "DCT sparse-baseline validation failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

#include "analysis/dct_interval_full_validator.hpp"

#include "applications/conv.hpp"
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

struct UnitExperiment
{
    approximate::ApproxUnitId unit;

    std::string name;
};


struct AggregateMetrics
{
    double globalSum = 0.0;
    double roiSum = 0.0;
    double nonRoiSum = 0.0;

    double minimumGlobal =
        std::numeric_limits<double>::infinity();

    double minimumRoi =
        std::numeric_limits<double>::infinity();

    double minimumNonRoi =
        std::numeric_limits<double>::infinity();

    int minimumGlobalImageIndex = -1;

    std::size_t imageCount = 0;
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


void accumulate(
    AggregateMetrics& aggregate,
    const analysis::DctImageQualityMetrics& metrics,
    int imageIndex
)
{
    aggregate.globalSum +=
        metrics.globalPsnr;

    aggregate.roiSum +=
        metrics.roiPsnr;

    aggregate.nonRoiSum +=
        metrics.nonRoiPsnr;


    if (
        metrics.globalPsnr
        <
        aggregate.minimumGlobal
    )
    {
        aggregate.minimumGlobal =
            metrics.globalPsnr;

        aggregate.minimumGlobalImageIndex =
            imageIndex;
    }


    aggregate.minimumRoi =
        std::min(
            aggregate.minimumRoi,
            metrics.roiPsnr
        );


    aggregate.minimumNonRoi =
        std::min(
            aggregate.minimumNonRoi,
            metrics.nonRoiPsnr
        );


    ++aggregate.imageCount;
}


double mean(
    double sum,
    std::size_t count
)
{
    if (count == 0)
    {
        throw std::runtime_error(
            "Conv baseline unit sweep has zero images."
        );
    }


    return
        sum
        /
        static_cast<double>(
            count
        );
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
            argc >= 2
            ?
            std::filesystem::path(
                argv[1]
            )
            :
            std::filesystem::path(
                "data"
            );


        const std::filesystem::path outputDirectory =
            argc >= 3
            ?
            std::filesystem::path(
                argv[2]
            )
            :
            std::filesystem::path(
                "."
            );


        std::filesystem::create_directories(
            outputDirectory
        );


        const std::vector<UnitExperiment>
            experiments =
        {
            {
                approximate::ApproxUnitId::Add12se5L8,
                "5L8"
            },
            {
                approximate::ApproxUnitId::Add12se5PD,
                "5PD"
            },
            {
                approximate::ApproxUnitId::Add12se5PN,
                "5PN"
            },
            {
                approximate::ApproxUnitId::Add12se5QC,
                "5QC"
            },
            {
                approximate::ApproxUnitId::Add12se5QT,
                "5QT"
            },
            {
                approximate::ApproxUnitId::Add12se5RP,
                "5RP"
            },
            {
                approximate::ApproxUnitId::Add12se5SB,
                "5SB"
            },
            {
                approximate::ApproxUnitId::Add12se5TE,
                "5TE"
            },
            {
                approximate::ApproxUnitId::Add12se5Z0,
                "5Z0"
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


        const auto detailPath =
            outputDirectory
            /
            "conv_baseline_unit_sweep.csv";


        const auto summaryPath =
            outputDirectory
            /
            "conv_baseline_unit_sweep_summary.csv";


        std::ofstream detailCsv(
            detailPath
        );


        std::ofstream summaryCsv(
            summaryPath
        );


        if (
            !detailCsv.is_open()
            ||
            !summaryCsv.is_open()
        )
        {
            throw std::runtime_error(
                "Unable to open Conv baseline unit-sweep CSV output."
            );
        }


        detailCsv
            << std::setprecision(12)
            << "image_index,unit,global_psnr,roi_psnr,non_roi_psnr\n";


        summaryCsv
            << std::setprecision(12)
            << "unit,mean_global_psnr,min_global_psnr,worst_global_image,"
            << "mean_roi_psnr,min_roi_psnr,"
            << "mean_non_roi_psnr,min_non_roi_psnr,"
            << "min_global_ge_30,min_global_ge_33,"
            << "target_band_33_to_37\n";


        std::vector<AggregateMetrics>
            aggregates(
                experiments.size()
            );


        std::cout
            << "Conv baseline approximate-unit sweep\n"
            << "====================================\n"
            << "Kernel: [1 2 1; 2 4 2; 1 2 1] / 16\n"
            << "ADD nodes: all 8 approximate\n"
            << "Stage-1 images: 10\n"
            << "Approximate implementations: "
            << experiments.size()
            << "\n"
            << "Reference: exact Conv output\n"
            << "No interval search / no joint search\n\n";


        std::cout
            << std::left
            << std::setw(10)
            << "Image"
            << std::setw(10)
            << "Unit"
            << std::setw(16)
            << "Global"
            << std::setw(16)
            << "ROI"
            << "NonROI\n"
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


                const auto config =
                    applications::ConvApplication::
                        createAllApproximateBaselineConfig(
                            experiment.unit
                        );


                const applications::ConvApplication
                    application(
                        config
                    );


                const auto metrics =
                    analysis::DctIntervalFullValidator::
                        evaluateConfiguration(
                            application,
                            inputImage,
                            roiMask,
                            {}
                        );


                accumulate(
                    aggregates[
                        experimentIndex
                    ],
                    metrics,
                    imageIndex
                );


                detailCsv
                    << imageIndex
                    << ","
                    << experiment.name
                    << ","
                    << metrics.globalPsnr
                    << ","
                    << metrics.roiPsnr
                    << ","
                    << metrics.nonRoiPsnr
                    << "\n";


                std::cout
                    << std::left
                    << std::setw(10)
                    << (
                        "image_"
                        +
                        twoDigit(
                            imageIndex
                        )
                    )
                    << std::setw(10)
                    << experiment.name
                    << std::setw(16)
                    << metrics.globalPsnr
                    << std::setw(16)
                    << metrics.roiPsnr
                    << metrics.nonRoiPsnr
                    << "\n";
            }
        }


        std::cout
            << "\nSummary over 10 Stage-1 images\n"
            << "--------------------------------\n"
            << std::left
            << std::setw(10)
            << "Unit"
            << std::setw(16)
            << "MeanGlobal"
            << std::setw(16)
            << "MinGlobal"
            << std::setw(12)
            << "WorstImg"
            << std::setw(16)
            << "MeanROI"
            << std::setw(10)
            << ">=33"
            << "Target33-37\n";


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


            const double meanGlobal =
                mean(
                    aggregate.globalSum,
                    aggregate.imageCount
                );


            const double meanRoi =
                mean(
                    aggregate.roiSum,
                    aggregate.imageCount
                );


            const double meanNonRoi =
                mean(
                    aggregate.nonRoiSum,
                    aggregate.imageCount
                );


            const bool minimumAtLeast30 =
                aggregate.minimumGlobal
                >=
                30.0;


            const bool minimumAtLeast33 =
                aggregate.minimumGlobal
                >=
                33.0;


            const bool inTargetBand =
                aggregate.minimumGlobal
                >=
                33.0
                &&
                meanGlobal
                <=
                37.0;


            summaryCsv
                << experiment.name
                << ","
                << meanGlobal
                << ","
                << aggregate.minimumGlobal
                << ","
                << aggregate.minimumGlobalImageIndex
                << ","
                << meanRoi
                << ","
                << aggregate.minimumRoi
                << ","
                << meanNonRoi
                << ","
                << aggregate.minimumNonRoi
                << ","
                << (
                    minimumAtLeast30
                    ?
                    1
                    :
                    0
                )
                << ","
                << (
                    minimumAtLeast33
                    ?
                    1
                    :
                    0
                )
                << ","
                << (
                    inTargetBand
                    ?
                    1
                    :
                    0
                )
                << "\n";


            std::cout
                << std::left
                << std::setw(10)
                << experiment.name
                << std::setw(16)
                << meanGlobal
                << std::setw(16)
                << aggregate.minimumGlobal
                << std::setw(12)
                << (
                    "image_"
                    +
                    twoDigit(
                        aggregate.minimumGlobalImageIndex
                    )
                )
                << std::setw(16)
                << meanRoi
                << std::setw(10)
                << (
                    minimumAtLeast33
                    ?
                    "YES"
                    :
                    "NO"
                )
                << (
                    inTargetBand
                    ?
                    "YES"
                    :
                    "NO"
                )
                << "\n";
        }


        std::cout
            << "\nDetailed CSV: "
            << detailPath.string()
            << "\n"
            << "Summary CSV: "
            << summaryPath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Conv baseline unit sweep failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

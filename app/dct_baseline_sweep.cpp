#include "analysis/dct_interval_full_validator.hpp"

#include "applications/dct.hpp"
#include "applications/dct8_fixed_graph.hpp"
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

struct BaselineExperiment
{
    std::string name;

    std::vector<int>
        approximateNodeIds;

    applications::Dct8FixedGraph::BaselineConfig
        config;
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


std::string nodeListText(
    const std::vector<int>& nodeIds
)
{
    if (nodeIds.empty())
    {
        return "-";
    }

    std::ostringstream stream;

    for (std::size_t index = 0;
         index < nodeIds.size();
         ++index)
    {
        if (index != 0)
        {
            stream << ";";
        }

        stream << nodeIds[index];
    }

    return stream.str();
}


void accumulate(
    AggregateMetrics& aggregate,
    const analysis::DctImageQualityMetrics& metrics
)
{
    aggregate.globalSum +=
        metrics.globalPsnr;

    aggregate.roiSum +=
        metrics.roiPsnr;

    aggregate.nonRoiSum +=
        metrics.nonRoiPsnr;

    aggregate.minimumGlobal =
        std::min(
            aggregate.minimumGlobal,
            metrics.globalPsnr
        );

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
            "Baseline sweep has zero images."
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


        const std::filesystem::path outputDirectory =
            (
                argc >= 3
            )
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


        const std::vector<BaselineExperiment>
            experiments =
        {
            {
                "Sparse-3",
                {
                    13,
                    19,
                    25
                },
                applications::Dct8FixedGraph::
                    createSparseApproximateBaselineConfig(
                        {
                            13,
                            19,
                            25
                        },
                        approximate::ApproxUnitId::Add12se5RP
                    )
            },

            {
                "Balanced-8",
                {
                    10,
                    13,
                    16,
                    19,
                    22,
                    25,
                    28,
                    31
                },
                applications::Dct8FixedGraph::
                    createSparseApproximateBaselineConfig(
                        {
                            10,
                            13,
                            16,
                            19,
                            22,
                            25,
                            28,
                            31
                        },
                        approximate::ApproxUnitId::Add12se5RP
                    )
            },

            {
                "Balanced-12",
                {
                    8,
                    10,
                    11,
                    13,
                    14,
                    16,
                    17,
                    19,
                    22,
                    25,
                    28,
                    31
                },
                applications::Dct8FixedGraph::
                    createSparseApproximateBaselineConfig(
                        {
                            8,
                            10,
                            11,
                            13,
                            14,
                            16,
                            17,
                            19,
                            22,
                            25,
                            28,
                            31
                        },
                        approximate::ApproxUnitId::Add12se5RP
                    )
            },

            {
                "All-5RP",
                {},
                applications::Dct8FixedGraph::
                    createAllApproximateBaselineConfig(
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


        const auto detailPath =
            outputDirectory
            /
            "dct_baseline_sweep.csv";


        const auto summaryPath =
            outputDirectory
            /
            "dct_baseline_sweep_summary.csv";


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
                "Unable to open baseline-sweep CSV output."
            );
        }


        detailCsv
            << std::setprecision(12)
            << "image_index,baseline,approx_node_count,"
            << "approx_nodes,global_psnr,roi_psnr,non_roi_psnr\n";


        summaryCsv
            << std::setprecision(12)
            << "baseline,approx_node_count,approx_nodes,"
            << "mean_global_psnr,min_global_psnr,"
            << "mean_roi_psnr,min_roi_psnr,"
            << "mean_non_roi_psnr,min_non_roi_psnr\n";


        std::vector<AggregateMetrics>
            aggregates(
                experiments.size()
            );


        std::cout
            << "DCT baseline quality sweep\n"
            << "==========================\n"
            << "Stage 1 images: 10\n"
            << "Approximate implementation: 5RP\n"
            << "No interval search / no joint search\n"
            << "Reference: exact DCT reconstruction\n\n";


        std::cout
            << std::left
            << std::setw(8)
            << "Image"
            << std::setw(15)
            << "Baseline"
            << std::setw(10)
            << "Approx"
            << std::setw(14)
            << "Global"
            << std::setw(14)
            << "ROI"
            << "NonROI\n";


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
                        experiment.config
                    );


                const auto metrics =
                    analysis::
                        DctIntervalFullValidator::
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
                    metrics
                );


                const std::size_t approximateNodeCount =
                    experiment.name == "All-5RP"
                    ?
                    static_cast<std::size_t>(
                        applications::
                            Dct8FixedGraph::
                                kAddNodeCount
                    )
                    :
                    experiment.
                        approximateNodeIds.
                            size();


                const std::string nodes =
                    experiment.name == "All-5RP"
                    ?
                    "ALL"
                    :
                    nodeListText(
                        experiment.
                            approximateNodeIds
                    );


                detailCsv
                    << imageIndex
                    << ","
                    << experiment.name
                    << ","
                    << approximateNodeCount
                    << ","
                    << nodes
                    << ","
                    << metrics.globalPsnr
                    << ","
                    << metrics.roiPsnr
                    << ","
                    << metrics.nonRoiPsnr
                    << "\n";


                std::cout
                    << std::left
                    << std::setw(8)
                    << imageIndex
                    << std::setw(15)
                    << experiment.name
                    << std::setw(10)
                    << approximateNodeCount
                    << std::setw(14)
                    << metrics.globalPsnr
                    << std::setw(14)
                    << metrics.roiPsnr
                    << metrics.nonRoiPsnr
                    << "\n";
            }
        }


        std::cout
            << "\nSummary over 10 Stage 1 images\n"
            << "--------------------------------\n"
            << std::left
            << std::setw(15)
            << "Baseline"
            << std::setw(10)
            << "Approx"
            << std::setw(14)
            << "MeanGlobal"
            << std::setw(14)
            << "MinGlobal"
            << std::setw(14)
            << "MeanROI"
            << std::setw(14)
            << "MinROI"
            << std::setw(14)
            << "MeanNonROI"
            << "MinNonROI\n";


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


            const std::size_t approximateNodeCount =
                experiment.name == "All-5RP"
                ?
                static_cast<std::size_t>(
                    applications::
                        Dct8FixedGraph::
                            kAddNodeCount
                )
                :
                experiment.
                    approximateNodeIds.
                        size();


            const std::string nodes =
                experiment.name == "All-5RP"
                ?
                "ALL"
                :
                nodeListText(
                    experiment.
                        approximateNodeIds
                );


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


            summaryCsv
                << experiment.name
                << ","
                << approximateNodeCount
                << ","
                << nodes
                << ","
                << meanGlobal
                << ","
                << aggregate.minimumGlobal
                << ","
                << meanRoi
                << ","
                << aggregate.minimumRoi
                << ","
                << meanNonRoi
                << ","
                << aggregate.minimumNonRoi
                << "\n";


            std::cout
                << std::left
                << std::setw(15)
                << experiment.name
                << std::setw(10)
                << approximateNodeCount
                << std::setw(14)
                << meanGlobal
                << std::setw(14)
                << aggregate.minimumGlobal
                << std::setw(14)
                << meanRoi
                << std::setw(14)
                << aggregate.minimumRoi
                << std::setw(14)
                << meanNonRoi
                << aggregate.minimumNonRoi
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
            << "DCT baseline sweep failed: "
            << exception.what()
            << "\n";

        return 1;
    }
}

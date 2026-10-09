#include "analysis/dct_interval_full_validator.hpp"

#include "applications/conv.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace
{

struct ImageCase
{
    cv::Mat input;
    cv::Mat roiMask;
};


struct UnitChoice
{
    approximate::ApproxUnitId unit;
    std::string name;
};


struct BaselineResult
{
    std::vector<int> approximateNodeIds;

    approximate::ApproxUnitId unit =
        approximate::ApproxUnitId::Add12se5RP;

    std::string unitName;


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


    double meanGlobal() const
    {
        return
            globalSum
            /
            static_cast<double>(
                imageCount
            );
    }


    double meanRoi() const
    {
        return
            roiSum
            /
            static_cast<double>(
                imageCount
            );
    }


    double meanNonRoi() const
    {
        return
            nonRoiSum
            /
            static_cast<double>(
                imageCount
            );
    }
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
    std::ostringstream stream;


    for (std::size_t index = 0;
         index < nodeIds.size();
         ++index)
    {
        if (index != 0)
        {
            stream
                << ";";
        }


        stream
            << nodeIds[index];
    }


    return stream.str();
}


std::string namedNodeListText(
    const std::vector<int>& nodeIds
)
{
    std::ostringstream stream;


    for (std::size_t index = 0;
         index < nodeIds.size();
         ++index)
    {
        if (index != 0)
        {
            stream
                << " ";
        }


        const int nodeId =
            nodeIds[index];


        stream
            << nodeId
            << "("
            << applications::ConvApplication::nodeName(
                nodeId
            )
            << ")";
    }


    return stream.str();
}


void generateCombinations(
    int startNode,
    int remaining,
    std::vector<int>& current,
    const std::function<void(const std::vector<int>&)>& callback
)
{
    if (remaining == 0)
    {
        callback(
            current
        );

        return;
    }


    const int maximumStart =
        applications::ConvApplication::kAddNodeCount
        -
        remaining;


    for (int nodeId = startNode;
         nodeId <= maximumStart;
         ++nodeId)
    {
        current.push_back(
            nodeId
        );


        generateCombinations(
            nodeId + 1,
            remaining - 1,
            current,
            callback
        );


        current.pop_back();
    }
}


BaselineResult evaluateBaseline(
    const std::vector<int>& nodeIds,
    const UnitChoice& unitChoice,
    const std::vector<ImageCase>& images
)
{
    const auto config =
        applications::ConvApplication::
            createSparseApproximateBaselineConfig(
                nodeIds,
                unitChoice.unit
            );


    const applications::ConvApplication
        application(
            config
        );


    BaselineResult
        result;


    result.approximateNodeIds =
        nodeIds;


    result.unit =
        unitChoice.unit;


    result.unitName =
        unitChoice.name;


    for (std::size_t imageIndex = 0;
         imageIndex < images.size();
         ++imageIndex)
    {
        const auto metrics =
            analysis::DctIntervalFullValidator::
                evaluateConfiguration(
                    application,
                    images[imageIndex].input,
                    images[imageIndex].roiMask,
                    {}
                );


        result.globalSum +=
            metrics.globalPsnr;


        result.roiSum +=
            metrics.roiPsnr;


        result.nonRoiSum +=
            metrics.nonRoiPsnr;


        if (
            metrics.globalPsnr
            <
            result.minimumGlobal
        )
        {
            result.minimumGlobal =
                metrics.globalPsnr;


            result.minimumGlobalImageIndex =
                static_cast<int>(
                    imageIndex
                )
                +
                1;
        }


        result.minimumRoi =
            std::min(
                result.minimumRoi,
                metrics.roiPsnr
            );


        result.minimumNonRoi =
            std::min(
                result.minimumNonRoi,
                metrics.nonRoiPsnr
            );


        ++result.imageCount;
    }


    return result;
}


double targetDistance(
    const BaselineResult& result
)
{
    // Baseline 目标区间：
    // Min Global >= 33 dB，
    // Mean Global 尽量接近 35 dB。
    //
    // 这里仅用于终端推荐排序，不改变 CSV 中的原始结果。
    if (
        result.minimumGlobal
        <
        33.0
    )
    {
        return
            1000.0
            +
            (
                33.0
                -
                result.minimumGlobal
            );
    }


    const double difference =
        result.meanGlobal()
        -
        35.0;


    return
        difference >= 0.0
        ?
        difference
        :
        -difference;
}


bool recommendedBaselineOrder(
    const BaselineResult& first,
    const BaselineResult& second
)
{
    const bool firstFeasible =
        first.minimumGlobal
        >=
        33.0;


    const bool secondFeasible =
        second.minimumGlobal
        >=
        33.0;


    if (
        firstFeasible
        !=
        secondFeasible
    )
    {
        return
            firstFeasible;
    }


    if (
        firstFeasible
        &&
        secondFeasible
    )
    {
        const double firstDistance =
            targetDistance(
                first
            );


        const double secondDistance =
            targetDistance(
                second
            );


        if (
            firstDistance
            !=
            secondDistance
        )
        {
            return
                firstDistance
                <
                secondDistance;
        }


        // 质量接近时优先近似节点更多的 Baseline。
        if (
            first.approximateNodeIds.size()
            !=
            second.approximateNodeIds.size()
        )
        {
            return
                first.approximateNodeIds.size()
                >
                second.approximateNodeIds.size();
        }


        if (
            first.minimumGlobal
            !=
            second.minimumGlobal
        )
        {
            return
                first.minimumGlobal
                >
                second.minimumGlobal;
        }


        return
            first.approximateNodeIds
            <
            second.approximateNodeIds;
    }


    if (
        first.minimumGlobal
        !=
        second.minimumGlobal
    )
    {
        return
            first.minimumGlobal
            >
            second.minimumGlobal;
    }


    return
        first.meanGlobal()
        >
        second.meanGlobal();
}


void printTop(
    std::vector<BaselineResult> results,
    std::size_t topCount
)
{
    std::sort(
        results.begin(),
        results.end(),
        recommendedBaselineOrder
    );


    const std::size_t showCount =
        std::min(
            topCount,
            results.size()
        );


    std::cout
        << "\nTop "
        << showCount
        << " recommended Conv baselines\n"
        << "---------------------------------------------------------------\n"
        << std::left
        << std::setw(7)
        << "Rank"
        << std::setw(8)
        << "Count"
        << std::setw(10)
        << "Unit"
        << std::setw(22)
        << "Nodes"
        << std::setw(14)
        << "MeanGlobal"
        << std::setw(14)
        << "MinGlobal"
        << std::setw(10)
        << "WorstImg"
        << "MeanROI\n";


    for (std::size_t index = 0;
         index < showCount;
         ++index)
    {
        const auto& result =
            results[index];


        std::cout
            << std::left
            << std::setw(7)
            << (index + 1)
            << std::setw(8)
            << result.approximateNodeIds.size()
            << std::setw(10)
            << result.unitName
            << std::setw(22)
            << nodeListText(
                result.approximateNodeIds
            )
            << std::setw(14)
            << result.meanGlobal()
            << std::setw(14)
            << result.minimumGlobal
            << std::setw(10)
            << (
                "image_"
                +
                twoDigit(
                    result.minimumGlobalImageIndex
                )
            )
            << result.meanRoi()
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


        const std::vector<UnitChoice>
            units =
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


        std::vector<ImageCase>
            images;


        images.reserve(
            10
        );


        for (int imageIndex = 1;
             imageIndex <= 10;
             ++imageIndex)
        {
            cv::Mat inputImage =
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


            cv::Mat roiMask =
                region_mask::resizeMaskToImage(
                    sharedRoiMask,
                    inputImage
                );


            images.push_back(
                ImageCase{
                    std::move(
                        inputImage
                    ),
                    std::move(
                        roiMask
                    )
                }
            );
        }


        constexpr std::size_t subsetCount =
            (1u << applications::ConvApplication::kAddNodeCount)
            -
            1u;


        const std::size_t totalConfigurationCount =
            subsetCount
            *
            units.size();


        std::cout
            << "Conv exhaustive baseline sweep\n"
            << "==============================\n"
            << "Kernel: [1 2 1; 2 4 2; 1 2 1] / 16\n"
            << "ADD nodes: "
            << applications::ConvApplication::kAddNodeCount
            << "\n"
            << "Node subsets: all non-empty subsets = "
            << subsetCount
            << "\n"
            << "Uniform approximate units: "
            << units.size()
            << "\n"
            << "Total configurations: "
            << totalConfigurationCount
            << "\n"
            << "Stage-1 images per configuration: 10\n"
            << "Reference: exact Conv output\n"
            << "Each selected subset uses one common approximate unit; "
            << "unselected nodes remain exact.\n\n";


        std::vector<BaselineResult>
            allResults;


        allResults.reserve(
            totalConfigurationCount
        );


        std::size_t completed =
            0;


        for (const auto& unitChoice : units)
        {
            for (int approximateNodeCount = 1;
                 approximateNodeCount
                    <=
                    applications::ConvApplication::kAddNodeCount;
                 ++approximateNodeCount)
            {
                std::vector<int>
                    current;


                generateCombinations(
                    0,
                    approximateNodeCount,
                    current,

                    [&](
                        const std::vector<int>& nodeIds
                    )
                    {
                        allResults.push_back(
                            evaluateBaseline(
                                nodeIds,
                                unitChoice,
                                images
                            )
                        );


                        ++completed;


                        if (
                            completed == 1
                            ||
                            completed % 25 == 0
                            ||
                            completed
                                ==
                                totalConfigurationCount
                        )
                        {
                            std::cout
                                << "\r[conv-baseline] "
                                << completed
                                << "/"
                                << totalConfigurationCount
                                << " configurations"
                                << "                    "
                                << std::flush;
                        }
                    }
                );
            }
        }


        std::cout
            << "\n";


        const auto summaryPath =
            outputDirectory
            /
            "conv_baseline_exhaustive_summary.csv";


        std::ofstream summaryCsv(
            summaryPath
        );


        if (!summaryCsv.is_open())
        {
            throw std::runtime_error(
                "Unable to open Conv exhaustive baseline CSV."
            );
        }


        summaryCsv
            << std::setprecision(
                12
            )
            << "approx_node_count,unit,approx_nodes,approx_node_names,"
            << "mean_global_psnr,min_global_psnr,worst_global_image,"
            << "mean_roi_psnr,min_roi_psnr,"
            << "mean_non_roi_psnr,min_non_roi_psnr,"
            << "min_global_ge_30,min_global_ge_33,"
            << "mean_global_33_to_37\n";


        std::vector<BaselineResult>
            csvResults =
                allResults;


        std::sort(
            csvResults.begin(),
            csvResults.end(),
            recommendedBaselineOrder
        );


        for (const auto& result : csvResults)
        {
            summaryCsv
                << result.approximateNodeIds.size()
                << ","
                << result.unitName
                << ","
                << nodeListText(
                    result.approximateNodeIds
                )
                << ",\""
                << namedNodeListText(
                    result.approximateNodeIds
                )
                << "\","
                << result.meanGlobal()
                << ","
                << result.minimumGlobal
                << ","
                << result.minimumGlobalImageIndex
                << ","
                << result.meanRoi()
                << ","
                << result.minimumRoi
                << ","
                << result.meanNonRoi()
                << ","
                << result.minimumNonRoi
                << ","
                << (
                    result.minimumGlobal >= 30.0
                    ?
                    1
                    :
                    0
                )
                << ","
                << (
                    result.minimumGlobal >= 33.0
                    ?
                    1
                    :
                    0
                )
                << ","
                << (
                    result.meanGlobal() >= 33.0
                    &&
                    result.meanGlobal() <= 37.0
                    ?
                    1
                    :
                    0
                )
                << "\n";
        }


        std::cout
            << std::fixed
            << std::setprecision(
                6
            );


        printTop(
            allResults,
            30
        );


        std::cout
            << "\nSummary CSV: "
            << summaryPath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Conv exhaustive baseline sweep failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

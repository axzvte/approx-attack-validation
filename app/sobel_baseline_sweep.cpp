#include "analysis/dct_interval_full_validator.hpp"

#include "applications/sobel.hpp"
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


struct BaselineResult
{
    std::vector<int>
        approximateNodeIds;


    double globalSum = 0.0;
    double roiSum = 0.0;
    double nonRoiSum = 0.0;


    double minimumGlobal =
        std::numeric_limits<double>::infinity();

    double minimumRoi =
        std::numeric_limits<double>::infinity();

    double minimumNonRoi =
        std::numeric_limits<double>::infinity();


    int minimumGlobalImageIndex =
        -1;


    int imagesAtLeast30 = 0;
    int imagesAtLeast33 = 0;


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
            << applications::SobelApplication::nodeName(
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
        applications::SobelApplication::kAddNodeCount
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
    const std::vector<ImageCase>& images
)
{
    const auto config =
        applications::SobelApplication::
            createSparseApproximateBaselineConfig(
                nodeIds,
                approximate::ApproxUnitId::Add12se5RP
            );


    const applications::SobelApplication
        application(
            config
        );


    BaselineResult
        result;


    result.approximateNodeIds =
        nodeIds;


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


        if (
            metrics.globalPsnr
            >=
            30.0
        )
        {
            ++result.imagesAtLeast30;
        }


        if (
            metrics.globalPsnr
            >=
            33.0
        )
        {
            ++result.imagesAtLeast33;
        }


        ++result.imageCount;
    }


    return result;
}


bool betterBaseline(
    const BaselineResult& first,
    const BaselineResult& second
)
{
    // Baseline 首先要求跨图片最差质量尽可能高，
    // 避免平均值掩盖个别图片质量不足。
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


    if (
        first.meanGlobal()
        !=
        second.meanGlobal()
    )
    {
        return
            first.meanGlobal()
            >
            second.meanGlobal();
    }


    return
        first.approximateNodeIds
        <
        second.approximateNodeIds;
}


void printTopGroup(
    const std::vector<BaselineResult>& allResults,
    std::size_t approximateNodeCount,
    std::size_t topCount
)
{
    std::vector<BaselineResult>
        group;


    for (const auto& result : allResults)
    {
        if (
            result.approximateNodeIds.size()
            ==
            approximateNodeCount
        )
        {
            group.push_back(
                result
            );
        }
    }


    std::sort(
        group.begin(),
        group.end(),
        betterBaseline
    );


    const std::size_t showCount =
        std::min(
            topCount,
            group.size()
        );


    std::cout
        << "\nTop "
        << showCount
        << " configurations with "
        << approximateNodeCount
        << " approximate nodes\n"
        << "------------------------------------------------------------\n"
        << std::left
        << std::setw(7)
        << "Rank"
        << std::setw(28)
        << "Nodes"
        << std::setw(14)
        << "MeanGlobal"
        << std::setw(14)
        << "MinGlobal"
        << std::setw(10)
        << "WorstImg"
        << std::setw(14)
        << "MeanROI"
        << "Min>=33\n";


    for (std::size_t index = 0;
         index < showCount;
         ++index)
    {
        const auto& result =
            group[index];


        std::cout
            << std::left
            << std::setw(7)
            << (index + 1)
            << std::setw(28)
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
            << std::setw(14)
            << result.meanRoi()
            << (
                result.minimumGlobal >= 33.0
                ?
                "YES"
                :
                "NO"
            )
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


        constexpr std::size_t totalConfigurationCount =
            165
            +
            330
            +
            462;


        std::cout
            << "Sobel exhaustive baseline sweep\n"
            << "===============================\n"
            << "ADD/SUB nodes: 11\n"
            << "Approximate implementation: 5RP\n"
            << "Approximate-node counts: 3, 4, 5\n"
            << "Total configurations: "
            << totalConfigurationCount
            << "\n"
            << "Stage-1 images per configuration: 10\n"
            << "Reference: exact Sobel output\n"
            << "Ranking: Min Global PSNR first, Mean Global PSNR second\n"
            << "ROI / Non-ROI are recorded only; they do not affect baseline ranking.\n\n";


        std::vector<BaselineResult>
            allResults;


        allResults.reserve(
            totalConfigurationCount
        );


        std::size_t completed =
            0;


        for (const int approximateNodeCount :
             {
                 3,
                 4,
                 5
             })
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
                            images
                        )
                    );


                    ++completed;


                    if (
                        completed == 1
                        ||
                        completed % 20 == 0
                        ||
                        completed
                            ==
                            totalConfigurationCount
                    )
                    {
                        std::cout
                            << "\r[sobel-baseline] "
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


        std::cout
            << "\n";


        const auto summaryPath =
            outputDirectory
            /
            "sobel_baseline_sweep_summary.csv";


        std::ofstream summaryCsv(
            summaryPath
        );


        if (!summaryCsv.is_open())
        {
            throw std::runtime_error(
                "Unable to open Sobel baseline sweep CSV."
            );
        }


        summaryCsv
            << std::setprecision(
                12
            )
            << "approx_node_count,approx_nodes,approx_node_names,"
            << "mean_global_psnr,min_global_psnr,worst_global_image,"
            << "mean_roi_psnr,min_roi_psnr,"
            << "mean_non_roi_psnr,min_non_roi_psnr,"
            << "images_global_ge_30,images_global_ge_33,"
            << "min_global_ge_30,min_global_ge_33\n";


        std::vector<BaselineResult>
            csvResults =
                allResults;


        std::sort(
            csvResults.begin(),
            csvResults.end(),

            [](
                const BaselineResult& first,
                const BaselineResult& second
            )
            {
                if (
                    first.approximateNodeIds.size()
                    !=
                    second.approximateNodeIds.size()
                )
                {
                    return
                        first.approximateNodeIds.size()
                        <
                        second.approximateNodeIds.size();
                }


                return
                    betterBaseline(
                        first,
                        second
                    );
            }
        );


        for (const auto& result : csvResults)
        {
            summaryCsv
                << result.approximateNodeIds.size()
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
                << result.imagesAtLeast30
                << ","
                << result.imagesAtLeast33
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
                << "\n";
        }


        std::cout
            << std::fixed
            << std::setprecision(
                6
            );


        printTopGroup(
            allResults,
            3,
            10
        );


        printTopGroup(
            allResults,
            4,
            10
        );


        printTopGroup(
            allResults,
            5,
            10
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
            << "Sobel baseline sweep failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

#include "analysis/dct_node_sensitivity.hpp"

#include "applications/sobel.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
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

    return "unknown";
}


void writeSummaryCsv(
    const std::filesystem::path& path,
    const std::vector<analysis::DctNodeSensitivitySummary>& summaries
)
{
    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open Sobel sensitivity summary CSV."
        );
    }


    file
        << "rank,node_id,node_name,mean_sensitivity,max_sensitivity,"
        << "max_unit,mean_roi_sensitivity,mean_roi_to_non_roi_ratio,"
        << "mean_image_sensitivity,std_image_sensitivity,cv_image_sensitivity\n";


    file
        << std::setprecision(
            12
        );


    for (std::size_t index = 0;
         index < summaries.size();
         ++index)
    {
        const auto& summary =
            summaries[index];


        file
            << (index + 1) << ","
            << summary.nodeId << ","
            << applications::SobelApplication::nodeName(
                summary.nodeId
            )
            << ","
            << summary.meanSensitivity << ","
            << summary.maxSensitivity << ","
            << unitName(
                summary.maxSensitivityUnit
            )
            << ","
            << summary.meanRoiSensitivity << ","
            << summary.meanRoiToNonRoiRatio << ","
            << summary.meanImageSensitivity << ","
            << summary.stdImageSensitivity << ","
            << summary.cvImageSensitivity
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


        std::vector<cv::Mat>
            inputImages;


        std::vector<cv::Mat>
            roiMasks;


        for (int index = 1;
             index <= 10;
             ++index)
        {
            cv::Mat image =
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
                                index
                            )
                            +
                            ".jpg"
                        )
                    ).string()
                );


            roiMasks.push_back(
                region_mask::resizeMaskToImage(
                    sharedRoiMask,
                    image
                )
            );


            inputImages.push_back(
                std::move(
                    image
                )
            );
        }


        applications::SobelApplication
            application;


        const std::vector<approximate::ApproxUnitId>
            attackUnits =
        {
            approximate::ApproxUnitId::Add12se5L8,
            approximate::ApproxUnitId::Add12se5PD,
            approximate::ApproxUnitId::Add12se5PN,
            approximate::ApproxUnitId::Add12se5QC,
            approximate::ApproxUnitId::Add12se5QT,
            approximate::ApproxUnitId::Add12se5RP,
            approximate::ApproxUnitId::Add12se5TE,
            approximate::ApproxUnitId::Add12se5SB,
            approximate::ApproxUnitId::Add12se5Z0
        };


        const auto progressCallback =
            [](
                const analysis::DctNodeSensitivityProgress& progress
            )
            {
                if (
                    progress.phase
                    ==
                    analysis::DctNodeSensitivityPhase::Baseline
                )
                {
                    std::cout
                        << "\r[sobel-sensitivity] baseline image "
                        << progress.completed
                        << "/"
                        << progress.total
                        << "                    "
                        << std::flush;
                }
                else
                {
                    std::cout
                        << "\r[sobel-sensitivity] node "
                        << progress.nodeId
                        << " / image "
                        << progress.imageIndex
                        << "/"
                        << progress.imageCount
                        << "                    "
                        << std::flush;
                }
            };


        const auto report =
            analysis::DctNodeSensitivityAnalyzer::
                analyze(
                    application,
                    inputImages,
                    roiMasks,
                    attackUnits,
                    progressCallback
                );


        std::cout
            << "\n";


        auto ranked =
            report.nodeSummaries;


        std::sort(
            ranked.begin(),
            ranked.end(),

            [](
                const auto& first,
                const auto& second
            )
            {
                if (
                    first.meanRoiSensitivity
                    !=
                    second.meanRoiSensitivity
                )
                {
                    return
                        first.meanRoiSensitivity
                        >
                        second.meanRoiSensitivity;
                }


                if (
                    first.meanSensitivity
                    !=
                    second.meanSensitivity
                )
                {
                    return
                        first.meanSensitivity
                        >
                        second.meanSensitivity;
                }


                return
                    first.nodeId
                    <
                    second.nodeId;
            }
        );


        std::cout
            << "Sobel single-node sensitivity analysis\n"
            << "======================================\n"
            << "Baseline: all 11 ADD/SUB nodes = 5RP (provisional)\n"
            << "Ranking: mean ROI sensitivity\n"
            << "Stage-1 images: 10\n"
            << "Attack units: 9\n\n"
            << std::left
            << std::setw(7) << "Rank"
            << std::setw(7) << "Node"
            << std::setw(12) << "Name"
            << std::setw(16) << "ROI S"
            << std::setw(16) << "Mean S"
            << "ImgCV\n"
            << std::fixed
            << std::setprecision(
                6
            );


        for (std::size_t index = 0;
             index < ranked.size();
             ++index)
        {
            const auto& summary =
                ranked[index];


            std::cout
                << std::left
                << std::setw(7) << (index + 1)
                << std::setw(7) << summary.nodeId
                << std::setw(12)
                << applications::SobelApplication::nodeName(
                    summary.nodeId
                )
                << std::setw(16) << summary.meanRoiSensitivity
                << std::setw(16) << summary.meanSensitivity
                << summary.cvImageSensitivity
                << "\n";
        }


        const auto summaryPath =
            outputDirectory
            /
            "sobel_node_sensitivity_summary.csv";


        writeSummaryCsv(
            summaryPath,
            ranked
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
            << "Sobel sensitivity analysis failed: "
            << exception.what()
            << "\n";

        return 1;
    }
}

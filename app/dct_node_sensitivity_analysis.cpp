#include "analysis/dct_node_sensitivity.hpp"

#include "applications/dct.hpp"
#include "applications/dct8_fixed_graph.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
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


std::string formatDuration(
    double seconds
)
{
    if (
        !std::isfinite(seconds)
        ||
        seconds < 0.0
    )
    {
        return "--:--:--";
    }


    const long long totalSeconds =
        static_cast<long long>(
            seconds
        );


    const long long hours =
        totalSeconds / 3600;


    const long long minutes =
        (totalSeconds % 3600) / 60;


    const long long remainingSeconds =
        totalSeconds % 60;


    std::ostringstream stream;


    stream
        << std::setfill('0')
        << std::setw(2)
        << hours
        << ":"
        << std::setw(2)
        << minutes
        << ":"
        << std::setw(2)
        << remainingSeconds;


    return stream.str();
}


void writeDetailCsv(
    const std::filesystem::path& path,
    const analysis::DctNodeSensitivityReport& report
)
{
    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open DCT sensitivity detail CSV."
        );
    }


    file
        << "node_id,node_name,attack_unit,"
        << "local_mse,output_mse,roi_mse,non_roi_mse,"
        << "sensitivity,roi_sensitivity,roi_to_non_roi_ratio,"
        << "local_sample_count\n";


    file
        << std::setprecision(12);


    for (const auto& result : report.unitResults)
    {
        file
            << result.nodeId
            << ","
            << applications::Dct8FixedGraph::nodeName(
                result.nodeId
            )
            << ","
            << unitName(
                result.attackUnit
            )
            << ","
            << result.localMse
            << ","
            << result.outputMse
            << ","
            << result.roiMse
            << ","
            << result.nonRoiMse
            << ","
            << result.sensitivity
            << ","
            << result.roiSensitivity
            << ","
            << result.roiToNonRoiRatio
            << ","
            << result.localSampleCount
            << "\n";
    }
}


void writeSummaryCsv(
    const std::filesystem::path& path,
    const std::vector<
        analysis::DctNodeSensitivitySummary
    >& rankedSummaries
)
{
    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open DCT sensitivity summary CSV."
        );
    }


    file
        << "rank,node_id,node_name,"
        << "mean_sensitivity,max_sensitivity,max_unit,"
        << "mean_roi_sensitivity,mean_roi_to_non_roi_ratio\n";


    file
        << std::setprecision(12);


    for (std::size_t index = 0;
         index < rankedSummaries.size();
         ++index)
    {
        const auto& summary =
            rankedSummaries[
                index
            ];


        file
            << (index + 1)
            << ","
            << summary.nodeId
            << ","
            << applications::Dct8FixedGraph::nodeName(
                summary.nodeId
            )
            << ","
            << summary.meanSensitivity
            << ","
            << summary.maxSensitivity
            << ","
            << unitName(
                summary.maxSensitivityUnit
            )
            << ","
            << summary.meanRoiSensitivity
            << ","
            << summary.meanRoiToNonRoiRatio
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


        inputImages.reserve(
            10
        );


        roiMasks.reserve(
            10
        );


        for (int i = 1;
             i <= 10;
             ++i)
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
                            twoDigit(i)
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


            inputImages.push_back(
                std::move(
                    inputImage
                )
            );


            roiMasks.push_back(
                std::move(
                    roiMask
                )
            );
        }


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


        using Clock =
            std::chrono::steady_clock;


        const auto analysisStart =
            Clock::now();


        auto lastPrinted =
            analysisStart
            -
            std::chrono::seconds(
                2
            );


        bool attackPhaseStarted =
            false;


        Clock::time_point attackStart =
            analysisStart;


        const auto progressCallback =
            [&](
                const analysis::DctNodeSensitivityProgress& progress
            )
            {
                const auto now =
                    Clock::now();


                const bool phaseFinished =
                    progress.completed
                    ==
                    progress.total;


                // 最多约每秒刷新一次，避免终端输出本身拖慢程序。
                if (
                    !phaseFinished
                    &&
                    now - lastPrinted
                        <
                        std::chrono::seconds(
                            1
                        )
                )
                {
                    return;
                }


                lastPrinted =
                    now;


                const double totalElapsed =
                    std::chrono::duration<double>(
                        now - analysisStart
                    ).count();


                if (
                    progress.phase
                    ==
                    analysis::DctNodeSensitivityPhase::Baseline
                )
                {
                    std::cout
                        << "\r[Baseline] "
                        << progress.completed
                        << "/"
                        << progress.total
                        << " image(s)"
                        << " | elapsed "
                        << formatDuration(
                            totalElapsed
                        )
                        << "                    "
                        << std::flush;


                    if (phaseFinished)
                    {
                        std::cout
                            << "\n";
                    }


                    return;
                }


                if (!attackPhaseStarted)
                {
                    attackPhaseStarted =
                        true;


                    attackStart =
                        now;
                }


                const double attackElapsed =
                    std::chrono::duration<double>(
                        now - attackStart
                    ).count();


                const double percentage =
                    progress.total == 0
                    ?
                    0.0
                    :
                    100.0
                    *
                    static_cast<double>(
                        progress.completed
                    )
                    /
                    static_cast<double>(
                        progress.total
                    );


                double etaSeconds =
                    0.0;


                if (
                    progress.completed > 0
                    &&
                    progress.completed < progress.total
                )
                {
                    etaSeconds =
                        attackElapsed
                        *
                        static_cast<double>(
                            progress.total
                            -
                            progress.completed
                        )
                        /
                        static_cast<double>(
                            progress.completed
                        );
                }


                std::cout
                    << "\r[Attack] "
                    << progress.completed
                    << "/"
                    << progress.total
                    << " ("
                    << std::fixed
                    << std::setprecision(1)
                    << percentage
                    << "%)"
                    << " | Node "
                    << progress.nodeId
                    << " "
                    << applications::Dct8FixedGraph::nodeName(
                        progress.nodeId
                    )
                    << " | "
                    << unitName(
                        progress.attackUnit
                    )
                    << " | image "
                    << progress.imageIndex
                    << "/"
                    << progress.imageCount
                    << " | elapsed "
                    << formatDuration(
                        totalElapsed
                    )
                    << " | ETA "
                    << (
                        phaseFinished
                        ?
                        std::string(
                            "00:00:00"
                        )
                        :
                        formatDuration(
                            etaSeconds
                        )
                    )
                    << "                    "
                    << std::flush;


                if (phaseFinished)
                {
                    std::cout
                        << "\n";
                }
            };


        const auto report =
            analysis::DctNodeSensitivityAnalyzer::analyze(
                application,
                inputImages,
                roiMasks,
                attackUnits,
                progressCallback
            );


        const double analysisElapsed =
            std::chrono::duration<double>(
                Clock::now()
                -
                analysisStart
            ).count();


        std::cout
            << "\nSensitivity analysis completed in "
            << formatDuration(
                analysisElapsed
            )
            << "\n\n";


        std::vector<
            analysis::DctNodeSensitivitySummary
        >
            rankedSummaries =
                report.nodeSummaries;


        std::sort(
            rankedSummaries.begin(),
            rankedSummaries.end(),

            [](
                const auto& first,
                const auto& second
            )
            {
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
            << "DCT single-node sensitivity analysis\n"
            << "====================================\n"
            << "Baseline: 5RP\n"
            << "Stage 1 images: "
            << inputImages.size()
            << "\n"
            << "Attack units: "
            << attackUnits.size()
            << "\n"
            << "Trigger: full signed-12 Input1 range [-2048, 2047]\n"
            << "Output reference: normal 5RP baseline image\n\n";


        std::cout
            << std::left
            << std::setw(6)
            << "Rank"
            << std::setw(8)
            << "Node"
            << std::setw(22)
            << "Name"
            << std::setw(16)
            << "Mean S"
            << std::setw(16)
            << "Max S"
            << std::setw(10)
            << "Unit"
            << std::setw(16)
            << "ROI S"
            << "ROI/NIR"
            << "\n";


        std::cout
            << std::fixed
            << std::setprecision(6);


        for (std::size_t index = 0;
             index < rankedSummaries.size();
             ++index)
        {
            const auto& summary =
                rankedSummaries[
                    index
                ];


            std::cout
                << std::left
                << std::setw(6)
                << (index + 1)
                << std::setw(8)
                << summary.nodeId
                << std::setw(22)
                << applications::Dct8FixedGraph::nodeName(
                    summary.nodeId
                )
                << std::setw(16)
                << summary.meanSensitivity
                << std::setw(16)
                << summary.maxSensitivity
                << std::setw(10)
                << unitName(
                    summary.maxSensitivityUnit
                )
                << std::setw(16)
                << summary.meanRoiSensitivity
                << summary.meanRoiToNonRoiRatio
                << "\n";
        }


        const std::filesystem::path detailPath =
            outputDirectory
            /
            "dct_node_sensitivity_details.csv";


        const std::filesystem::path summaryPath =
            outputDirectory
            /
            "dct_node_sensitivity_summary.csv";


        writeDetailCsv(
            detailPath,
            report
        );


        writeSummaryCsv(
            summaryPath,
            rankedSummaries
        );


        std::cout
            << "\nDetail CSV: "
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
            << "DCT sensitivity analysis failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

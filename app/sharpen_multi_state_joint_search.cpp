#include "analysis/dct_multi_state_joint_search.hpp"

#include "applications/sharpen.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

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


std::string signalName(
    core::MonitorSignal signal
)
{
    switch (signal)
    {
        case core::MonitorSignal::Input1:
            return "input1";

        case core::MonitorSignal::Input2:
            return "input2";

        case core::MonitorSignal::BaselineOutput:
            return "baseline_output";
    }

    return "unknown";
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


void writeBestCsv(
    const std::filesystem::path& path,
    const analysis::DctMultiStateSearchState& state
)
{
    std::ofstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open sharpening best-result CSV."
        );
    }


    file
        << "node_id,node_name,monitor,implementation,lower,upper,"
        << "global_psnr,roi_psnr,non_roi_psnr\n";


    file
        << std::setprecision(
            12
        );


    for (const auto& config : state.configuration)
    {
        file
            << config.nodeId << ","
            << applications::SharpenApplication::nodeName(
                config.nodeId
            )
            << ","
            << signalName(
                config.monitorInput
            )
            << ","
            << unitName(
                config.unit
            )
            << ","
            << config.lower << ","
            << config.upper << ","
            << state.metrics.globalPsnr << ","
            << state.metrics.roiPsnr << ","
            << state.metrics.nonRoiPsnr
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


        const int imageIndex =
            argc >= 3
            ?
            std::stoi(
                argv[2]
            )
            :
            1;


        const std::filesystem::path outputDirectory =
            argc >= 4
            ?
            std::filesystem::path(
                argv[3]
            )
            :
            std::filesystem::path(
                "."
            );


        const std::size_t representativeCount =
            argc >= 5
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[4]
                )
            )
            :
            4;


        const std::size_t beamWidth =
            argc >= 6
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[5]
                )
            )
            :
            10;


        const std::size_t refinementRounds =
            argc >= 7
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[6]
                )
            )
            :
            0;


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


        std::filesystem::create_directories(
            outputDirectory
        );


        applications::SharpenApplication
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


        // Sharpening 只有 4 个 ADD/SUB 节点，因此全部进入联合搜索。
        // monitor signal 不再预筛，直接在联合搜索中共同搜索。
        const std::vector<int>
            candidateNodes =
        {
            0,
            1,
            2,
            3
        };


        analysis::AttackStructure
            structure;


        std::cout
            << "Sharpening candidate nodes\n"
            << "--------------------------\n";


        for (const int nodeId : candidateNodes)
        {
            structure.push_back(
                analysis::AttackStructureNode{
                    nodeId,
                    approximate::ApproxUnitId::Add12se5RP,
                    core::MonitorSignal::Input1
                }
            );


            std::cout
                << "  Node "
                << nodeId
                << " / "
                << applications::SharpenApplication::nodeName(
                    nodeId
                )
                << "\n";
        }


        analysis::DctMultiStateJointSearchOptions
            options;


        options.redistributionUnits =
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


        options.monitorSignals =
        {
            core::MonitorSignal::Input1,
            core::MonitorSignal::Input2,
            core::MonitorSignal::BaselineOutput
        };


        options.representativeIntervalCount =
            representativeCount;

        options.beamWidth =
            beamWidth;

        options.refinementRounds =
            refinementRounds;

        options.globalPsnrThreshold =
            30.0;


        std::cout
            << "Search objective: Global >= "
            << options.globalPsnrThreshold
            << " dB; then minimize ROI PSNR.\n"
            << "Monitor signals searched jointly: input1 / input2 / baseline_output\n\n";


        const analysis::DctMultiStateJointSearchProgressCallback
            progressCallback =
                [](
                    const analysis::DctMultiStateJointSearchProgress& progress
                )
                {
                    std::cout
                        << "\r[joint-search] pass "
                        << progress.passIndex
                        << " / node "
                        << progress.nodeId
                        << " / "
                        << (
                            progress.refinement
                            ?
                            "refine"
                            :
                            "forward"
                        )
                        << " / state "
                        << progress.completedStates
                        << "/"
                        << progress.totalStates
                        << "                    "
                        << std::flush;


                    if (
                        progress.completedStates
                        ==
                        progress.totalStates
                    )
                    {
                        std::cout
                            << "\n";
                    }
                };


        const auto result =
            analysis::DctMultiStateJointSearch::
                search(
                    application,
                    inputImage,
                    roiMask,
                    structure,
                    options,
                    progressCallback
                );


        std::cout
            << "\nSharpening initial metrics\n"
            << "---------------------------\n"
            << std::fixed
            << std::setprecision(
                6
            )
            << "Global: "
            << result.initialMetrics.globalPsnr
            << "\n"
            << "ROI: "
            << result.initialMetrics.roiPsnr
            << "\n"
            << "Non-ROI: "
            << result.initialMetrics.nonRoiPsnr
            << "\n";


        if (!result.hasBestFeasibleState)
        {
            std::cout
                << "\nNo feasible sharpening state satisfies "
                << "Global >= 30 dB and ROI < baseline ROI.\n";

            return 0;
        }


        const auto& best =
            result.bestFeasibleState;


        std::cout
            << "\nBest feasible sharpening state\n"
            << "--------------------------------\n"
            << "Global PSNR: "
            << best.metrics.globalPsnr
            << "\n"
            << "ROI PSNR: "
            << best.metrics.roiPsnr
            << "\n"
            << "Non-ROI PSNR: "
            << best.metrics.nonRoiPsnr
            << "\n"
            << "Active redistribution nodes: "
            << best.configuration.size()
            << "\n";


        for (const auto& config : best.configuration)
        {
            std::cout
                << "  Node "
                << config.nodeId
                << " / "
                << applications::SharpenApplication::nodeName(
                    config.nodeId
                )
                << " / "
                << signalName(
                    config.monitorInput
                )
                << " / "
                << unitName(
                    config.unit
                )
                << " / ["
                << config.lower
                << ", "
                << config.upper
                << "]\n";
        }


        const auto csvPath =
            outputDirectory
            /
            (
                "sharpen_joint_search_best_image_"
                +
                twoDigit(
                    imageIndex
                )
                +
                ".csv"
            );


        writeBestCsv(
            csvPath,
            best
        );


        const cv::Mat baselineImage =
            application.runApprox(
                inputImage,
                {}
            );


        const cv::Mat bestImage =
            application.runApprox(
                inputImage,
                best.configuration
            );


        const auto baselineImagePath =
            outputDirectory
            /
            (
                "sharpen_baseline_image_"
                +
                twoDigit(
                    imageIndex
                )
                +
                ".png"
            );


        const auto bestImagePath =
            outputDirectory
            /
            (
                "sharpen_joint_search_best_image_"
                +
                twoDigit(
                    imageIndex
                )
                +
                ".png"
            );


        image_io::saveImage(
            baselineImagePath.string(),
            baselineImage
        );


        image_io::saveImage(
            bestImagePath.string(),
            bestImage
        );


        std::cout
            << "\nBest-result CSV: "
            << csvPath.string()
            << "\n"
            << "Baseline image: "
            << baselineImagePath.string()
            << "\n"
            << "Best final image: "
            << bestImagePath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Sharpening joint search failed: "
            << exception.what()
            << "\n";

        return 1;
    }
}

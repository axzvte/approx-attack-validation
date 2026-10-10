#include "analysis/dct_multi_state_joint_search.hpp"

#include "applications/conv.hpp"
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
        case approximate::ApproxUnitId::Add12se5L8: return "5L8";
        case approximate::ApproxUnitId::Add12se5PD: return "5PD";
        case approximate::ApproxUnitId::Add12se5PN: return "5PN";
        case approximate::ApproxUnitId::Add12se5QC: return "5QC";
        case approximate::ApproxUnitId::Add12se5QT: return "5QT";
        case approximate::ApproxUnitId::Add12se5RP: return "5RP";
        case approximate::ApproxUnitId::Add12se5SB: return "5SB";
        case approximate::ApproxUnitId::Add12se5TE: return "5TE";
        case approximate::ApproxUnitId::Add12se5Z0: return "5Z0";
    }

    return "unknown";
}


std::vector<std::string> splitCsvLine(
    const std::string& line
)
{
    std::vector<std::string>
        fields;


    std::stringstream stream(
        line
    );


    std::string field;


    while (
        std::getline(
            stream,
            field,
            ','
        )
    )
    {
        fields.push_back(
            field
        );
    }


    return fields;
}


std::vector<int> loadRankedNodeIds(
    const std::filesystem::path& path,
    std::size_t count
)
{
    std::ifstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open Conv node-sensitivity summary CSV: "
            +
            path.string()
            +
            ". Run conv_node_sensitivity_analysis first."
        );
    }


    std::string line;


    if (!std::getline(file, line))
    {
        throw std::runtime_error(
            "Conv node-sensitivity summary CSV is empty."
        );
    }


    std::vector<int>
        nodes;


    nodes.reserve(
        count
    );


    while (
        nodes.size() < count
        &&
        std::getline(
            file,
            line
        )
    )
    {
        if (line.empty())
        {
            continue;
        }


        const auto fields =
            splitCsvLine(
                line
            );


        if (fields.size() < 2)
        {
            throw std::runtime_error(
                "Malformed Conv node-sensitivity summary row."
            );
        }


        const int nodeId =
            std::stoi(
                fields[1]
            );


        if (
            nodeId < 0
            ||
            nodeId
                >=
                applications::ConvApplication::kAddNodeCount
        )
        {
            throw std::runtime_error(
                "Invalid node ID in Conv node-sensitivity summary."
            );
        }


        nodes.push_back(
            nodeId
        );
    }


    if (nodes.size() < count)
    {
        throw std::runtime_error(
            "Conv node-sensitivity summary does not contain enough nodes."
        );
    }


    return nodes;
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
            "Unable to open Conv best-result CSV."
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
            << applications::ConvApplication::nodeName(
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
            7;


        const int nodeCount =
            argc >= 4
            ?
            std::stoi(
                argv[3]
            )
            :
            5;


        const std::filesystem::path outputDirectory =
            argc >= 5
            ?
            std::filesystem::path(
                argv[4]
            )
            :
            std::filesystem::path(
                "."
            );


        const std::size_t representativeCount =
            argc >= 6
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[5]
                )
            )
            :
            4;


        const std::size_t beamWidth =
            argc >= 7
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[6]
                )
            )
            :
            10;


        const std::size_t refinementRounds =
            argc >= 8
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[7]
                )
            )
            :
            0;


        const std::size_t candidateBoundaryCount =
            argc >= 9
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[8]
                )
            )
            :
            24;


        const std::size_t fullValidationCandidateCount =
            argc >= 10
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[9]
                )
            )
            :
            24;


        if (
            imageIndex < 1
            ||
            imageIndex > 10
        )
        {
            throw std::runtime_error(
                "Stage-1 image index must be in [1, 10]."
            );
        }


        if (
            nodeCount < 1
            ||
            nodeCount > 5
        )
        {
            throw std::runtime_error(
                "Conv joint-search node count must be in [1, 5]."
            );
        }


        std::filesystem::create_directories(
            outputDirectory
        );


        const auto sensitivityPath =
            outputDirectory
            /
            "conv_node_sensitivity_summary.csv";


        const auto nodeIds =
            loadRankedNodeIds(
                sensitivityPath,
                static_cast<std::size_t>(
                    nodeCount
                )
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


        applications::ConvApplication
            application;


        analysis::AttackStructure
            structure;


        for (const int nodeId : nodeIds)
        {
            structure.push_back(
                analysis::AttackStructureNode{
                    nodeId,
                    approximate::ApproxUnitId::Add12se5RP,
                    core::MonitorSignal::Input1
                }
            );
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


        options.candidateBoundaryCount =
            candidateBoundaryCount;


        options.representativeIntervalCount =
            representativeCount;


        options.fullValidationCandidateCount =
            fullValidationCandidateCount;


        options.beamWidth =
            beamWidth;


        options.refinementRounds =
            refinementRounds;


        options.globalPsnrThreshold =
            30.0;


        std::cout
            << "Conv error-redistribution joint search\n"
            << "======================================\n"
            << "Kernel: [-1 -1 -1; -1 8 -1; -1 -1 -1]\n"
            << "Image: image_"
            << twoDigit(
                imageIndex
            )
            << ".jpg\n"
            << "Baseline: all 8 ADD nodes = 5RP (provisional)\n"
            << "Candidate nodes used: "
            << structure.size()
            << "\n"
            << "Adaptive candidate boundaries / monitor: "
            << options.candidateBoundaryCount
            << "\n"
            << "Local representatives / monitor / implementation: "
            << options.representativeIntervalCount
            << "\n"
            << "Full-validation candidates / state / node: "
            << options.fullValidationCandidateCount
            << "\n"
            << "Beam width: "
            << options.beamWidth
            << "\n"
            << "Global PSNR threshold: 30 dB\n"
            << "Final objective: Global >= 30 dB, DeltaMSE_ROI > DeltaMSE_NonROI, then minimize ROI PSNR\n\n"
            << "Candidate nodes\n"
            << "---------------\n";


        for (std::size_t index = 0;
             index < structure.size();
             ++index)
        {
            std::cout
                << "  "
                << (index + 1)
                << ". Node "
                << structure[index].nodeId
                << " / "
                << applications::ConvApplication::nodeName(
                    structure[index].nodeId
                )
                << "\n";
        }


        const auto result =
            analysis::DctMultiStateJointSearch::
                search(
                    application,
                    inputImage,
                    roiMask,
                    structure,
                    options,

                    [](
                        const analysis::DctMultiStateJointSearchProgress& progress
                    )
                    {
                        std::cout
                            << "\r[conv-joint] pass "
                            << progress.passIndex
                            << " / node "
                            << progress.nodeId
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
                    }
                );


        std::cout
            << "\nConv initial metrics\n"
            << "--------------------\n"
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
                << "\nNo feasible Conv state satisfies "
                << "Global >= 30 dB, ROI < baseline ROI, and DeltaMSE_ROI > DeltaMSE_NonROI.\n";

            return 0;
        }


        const auto& best =
            result.bestFeasibleState;


        const double roiMseIncrease =
            best.metrics.roiMse
            -
            result.initialMetrics.roiMse;


        const double nonRoiMseIncrease =
            best.metrics.nonRoiMse
            -
            result.initialMetrics.nonRoiMse;


        const double redistributionAdvantage =
            roiMseIncrease
            -
            nonRoiMseIncrease;


        std::cout
            << "\nBest feasible Conv state\n"
            << "------------------------\n"
            << "Global PSNR: "
            << best.metrics.globalPsnr
            << "\n"
            << "ROI PSNR: "
            << best.metrics.roiPsnr
            << "\n"
            << "Non-ROI PSNR: "
            << best.metrics.nonRoiPsnr
            << "\n"
            << "ROI MSE increase: "
            << roiMseIncrease
            << "\n"
            << "Non-ROI MSE increase: "
            << nonRoiMseIncrease
            << "\n"
            << "Redistribution advantage: "
            << redistributionAdvantage
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
                << applications::ConvApplication::nodeName(
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


        const auto bestCsvPath =
            outputDirectory
            /
            (
                "conv_joint_search_best_image_"
                +
                twoDigit(
                    imageIndex
                )
                +
                ".csv"
            );


        writeBestCsv(
            bestCsvPath,
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
                "conv_baseline_image_"
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
                "conv_joint_search_best_image_"
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
            << bestCsvPath.string()
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
            << "Conv joint search failed: "
            << exception.what()
            << "\n";

        return 1;
    }
}

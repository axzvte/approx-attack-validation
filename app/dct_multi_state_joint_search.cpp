#include "analysis/dct_multi_state_joint_search.hpp"

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
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{

std::string twoDigit(int value)
{
    std::ostringstream stream;
    stream << std::setw(2) << std::setfill('0') << value;
    return stream.str();
}


std::string signalName(core::MonitorSignal signal)
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


core::MonitorSignal parseSignal(const std::string& value)
{
    if (value == "input1")
    {
        return core::MonitorSignal::Input1;
    }

    if (value == "input2")
    {
        return core::MonitorSignal::Input2;
    }

    if (
        value == "baseline_output"
        ||
        value == "5RP_output"
    )
    {
        return core::MonitorSignal::BaselineOutput;
    }

    throw std::runtime_error(
        "Unknown monitor signal in CSV: " + value
    );
}


std::string unitName(approximate::ApproxUnitId unit)
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


std::vector<std::string> splitCsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;

    while (std::getline(stream, field, ','))
    {
        fields.push_back(field);
    }

    return fields;
}


std::vector<int> loadRankedNodeIds(
    const std::filesystem::path& path,
    std::size_t count
)
{
    std::ifstream file(path);

    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open node-sensitivity summary CSV: "
            + path.string()
            + ". Run dct_node_sensitivity_analysis first."
        );
    }

    std::string line;

    if (!std::getline(file, line))
    {
        throw std::runtime_error(
            "Node-sensitivity summary CSV is empty."
        );
    }

    std::vector<int> nodes;
    nodes.reserve(count);

    while (
        nodes.size() < count
        &&
        std::getline(file, line)
    )
    {
        if (line.empty())
        {
            continue;
        }

        const auto fields =
            splitCsvLine(line);

        if (fields.size() < 2)
        {
            throw std::runtime_error(
                "Malformed node-sensitivity summary row."
            );
        }

        const int nodeId =
            std::stoi(fields[1]);

        if (
            nodeId < 0
            ||
            nodeId >= applications::Dct8FixedGraph::kAddNodeCount
        )
        {
            throw std::runtime_error(
                "Invalid node ID in node-sensitivity summary."
            );
        }

        nodes.push_back(nodeId);
    }

    if (nodes.size() < count)
    {
        throw std::runtime_error(
            "Node-sensitivity summary does not contain enough ranked nodes."
        );
    }

    return nodes;
}


std::map<int, core::MonitorSignal> loadBestRoiSignals(
    const std::filesystem::path& path,
    const std::vector<int>& nodeIds
)
{
    struct Choice
    {
        bool found = false;
        double meanGap = -std::numeric_limits<double>::infinity();
        core::MonitorSignal signal = core::MonitorSignal::Input1;
    };


    std::map<int, Choice> choices;

    for (const int nodeId : nodeIds)
    {
        choices[nodeId] = Choice{};
    }


    std::ifstream file(path);

    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open monitor-signal summary CSV: "
            + path.string()
            + ". Run dct_monitor_signal_analysis first."
        );
    }


    std::string line;

    if (!std::getline(file, line))
    {
        throw std::runtime_error(
            "Monitor-signal summary CSV is empty."
        );
    }


    while (std::getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }

        const auto fields =
            splitCsvLine(line);

        if (fields.size() < 5)
        {
            throw std::runtime_error(
                "Malformed monitor-signal summary row."
            );
        }

        const int nodeId =
            std::stoi(fields[0]);

        const auto iterator =
            choices.find(nodeId);

        if (iterator == choices.end())
        {
            continue;
        }

        if (fields[3] != "ROI")
        {
            continue;
        }

        const double meanGap =
            std::stod(fields[4]);

        if (
            !iterator->second.found
            ||
            meanGap > iterator->second.meanGap
        )
        {
            iterator->second.found = true;
            iterator->second.meanGap = meanGap;
            iterator->second.signal = parseSignal(fields[2]);
        }
    }


    std::map<int, core::MonitorSignal> result;

    for (const int nodeId : nodeIds)
    {
        const auto& choice =
            choices.at(nodeId);

        if (!choice.found)
        {
            throw std::runtime_error(
                "No ROI monitor-signal result for node "
                + std::to_string(nodeId)
            );
        }

        result[nodeId] =
            choice.signal;
    }

    return result;
}


void writeBestCsv(
    const std::filesystem::path& path,
    const analysis::DctMultiStateSearchState& state
)
{
    std::ofstream file(path);

    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open joint-search best-result CSV."
        );
    }

    file
        << "node_id,node_name,monitor,implementation,lower,upper,"
        << "global_psnr,roi_psnr,non_roi_psnr\n";

    file << std::setprecision(12);

    for (const auto& config : state.configuration)
    {
        file
            << config.nodeId << ","
            << applications::Dct8FixedGraph::nodeName(config.nodeId) << ","
            << signalName(config.monitorInput) << ","
            << unitName(config.unit) << ","
            << config.lower << ","
            << config.upper << ","
            << state.metrics.globalPsnr << ","
            << state.metrics.roiPsnr << ","
            << state.metrics.nonRoiPsnr << "\n";
    }
}

}


int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path dataRoot =
            argc >= 2
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("data");


        const int imageIndex =
            argc >= 3
            ? std::stoi(argv[2])
            : 1;


        const int nodeCount =
            argc >= 4
            ? std::stoi(argv[3])
            : 5;


        const std::filesystem::path analysisDirectory =
            argc >= 5
            ? std::filesystem::path(argv[4])
            : std::filesystem::path(".");


        const std::size_t representativeCount =
            argc >= 6
            ? static_cast<std::size_t>(std::stoul(argv[5]))
            : 8;


        const std::size_t beamWidth =
            argc >= 7
            ? static_cast<std::size_t>(std::stoul(argv[6]))
            : 20;


        const std::size_t refinementRounds =
            argc >= 8
            ? static_cast<std::size_t>(std::stoul(argv[7]))
            : 1;


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


        if (
            nodeCount < 1
            ||
            nodeCount > 5
        )
        {
            throw std::runtime_error(
                "Joint-search node count must be in [1, 5]."
            );
        }


        const auto sensitivitySummaryPath =
            analysisDirectory
            /
            "dct_node_sensitivity_summary.csv";


        const auto monitorSummaryPath =
            analysisDirectory
            /
            "dct_monitor_signal_summary.csv";


        const auto nodeIds =
            loadRankedNodeIds(
                sensitivitySummaryPath,
                static_cast<std::size_t>(nodeCount)
            );


        const auto bestSignals =
            loadBestRoiSignals(
                monitorSummaryPath,
                nodeIds
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
                        twoDigit(imageIndex)
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


        const auto workingBaseline =
            applications::Dct8FixedGraph::
                createDefaultBaselineConfig();


        applications::DctApplication
            application(
                workingBaseline
            );


        analysis::AttackStructure
            structure;


        for (const int nodeId : nodeIds)
        {
            structure.push_back(
                analysis::AttackStructureNode{
                    nodeId,
                    approximate::ApproxUnitId::Add12se5RP,
                    bestSignals.at(nodeId)
                }
            );
        }


        const std::vector<approximate::ApproxUnitId>
            redistributionUnits =
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


        analysis::DctMultiStateJointSearchOptions
            options;


        options.redistributionUnits =
            redistributionUnits;


        options.representativeIntervalCount =
            representativeCount;


        options.beamWidth =
            beamWidth;


        options.refinementRounds =
            refinementRounds;


        options.globalPsnrThreshold =
            30.0;


        std::cout
            << "DCT error-redistribution interval + joint search\n"
            << "===============================================\n"
            << "Image: image_"
            << twoDigit(imageIndex)
            << ".jpg\n"
            << "Baseline: Balanced-10 (nodes 8, 10, 11, 13, 16, 19, 22, 25, 28, 31 = 5RP; others exact)\n"
            << "Node source: "
            << sensitivitySummaryPath.string()
            << "\n"
            << "Monitor source: "
            << monitorSummaryPath.string()
            << "\n"
            << "Candidate nodes used: "
            << structure.size()
            << "\n"
            << "Redistribution implementations: "
            << redistributionUnits.size()
            << "\n"
            << "Representative intervals / implementation / state / node: "
            << options.representativeIntervalCount
            << "\n"
            << "Beam width: "
            << options.beamWidth
            << "\n"
            << "Refinement rounds: "
            << options.refinementRounds
            << "\n"
            << "Final Global PSNR threshold: "
            << options.globalPsnrThreshold
            << " dB\n\n";


        std::cout
            << "Automatically selected node / monitor pairs\n"
            << "-------------------------------------------\n";

        for (std::size_t index = 0;
             index < structure.size();
             ++index)
        {
            const auto& node =
                structure[index];

            std::cout
                << "  "
                << (index + 1)
                << ". Node "
                << node.nodeId
                << " / "
                << applications::Dct8FixedGraph::nodeName(node.nodeId)
                << " / "
                << signalName(node.monitorInput)
                << "\n";
        }


        std::cout
            << "\nRedistribution implementations searched: ";

        for (std::size_t i = 0;
             i < redistributionUnits.size();
             ++i)
        {
            if (i != 0)
            {
                std::cout << ", ";
            }

            std::cout
                << unitName(redistributionUnits[i]);
        }

        std::cout << "\n\n";


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
                            ? "refine"
                            : "forward"
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
                        std::cout << "\n";
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
            << "\nInitial metrics\n"
            << "---------------\n"
            << std::fixed
            << std::setprecision(6)
            << "Global: "
            << result.initialMetrics.globalPsnr
            << "\n"
            << "ROI: "
            << result.initialMetrics.roiPsnr
            << "\n"
            << "Non-ROI: "
            << result.initialMetrics.nonRoiPsnr
            << "\n\n";


        std::cout
            << "Layer summary\n"
            << "-------------\n"
            << std::left
            << std::setw(8) << "Pass"
            << std::setw(8) << "Node"
            << std::setw(12) << "Mode"
            << std::setw(12) << "Input"
            << std::setw(12) << "Expanded"
            << std::setw(12) << "Pareto"
            << "Kept\n";


        for (const auto& layer : result.layers)
        {
            std::cout
                << std::left
                << std::setw(8) << layer.passIndex
                << std::setw(8) << layer.nodeId
                << std::setw(12)
                << (
                    layer.refinement
                    ? "refine"
                    : "forward"
                )
                << std::setw(12) << layer.inputStateCount
                << std::setw(12) << layer.expandedStateCount
                << std::setw(12) << layer.paretoStateCount
                << layer.retainedStateCount
                << "\n";
        }


        std::vector<analysis::DctMultiStateSearchState>
            finalStates =
                result.finalStates;


        std::sort(
            finalStates.begin(),
            finalStates.end(),
            [](
                const auto& first,
                const auto& second
            )
            {
                return
                    first.metrics.roiPsnr
                    <
                    second.metrics.roiPsnr;
            }
        );


        std::cout
            << "\nFinal beam states (lowest ROI first)\n"
            << "------------------------------------\n"
            << std::left
            << std::setw(7) << "Rank"
            << std::setw(14) << "Global"
            << std::setw(14) << "ROI"
            << std::setw(14) << "NonROI"
            << "ActiveNodes\n";


        for (std::size_t index = 0;
             index < finalStates.size();
             ++index)
        {
            const auto& state =
                finalStates[index];

            std::cout
                << std::left
                << std::setw(7) << (index + 1)
                << std::setw(14) << state.metrics.globalPsnr
                << std::setw(14) << state.metrics.roiPsnr
                << std::setw(14) << state.metrics.nonRoiPsnr
                << state.configuration.size()
                << "\n";
        }


        if (!result.hasBestFeasibleState)
        {
            std::cout
                << "\nNo final state satisfies Global >= 30 dB "
                << "and ROI < baseline ROI.\n";

            return 0;
        }


        const auto& best =
            result.bestFeasibleState;


        std::cout
            << "\nBest feasible redistribution state\n"
            << "----------------------------------\n"
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
                << signalName(config.monitorInput)
                << " / "
                << unitName(config.unit)
                << " / ["
                << config.lower
                << ", "
                << config.upper
                << "]\n";
        }


        const auto bestCsvPath =
            analysisDirectory
            /
            (
                "dct_joint_search_best_image_"
                +
                twoDigit(imageIndex)
                +
                ".csv"
            );


        writeBestCsv(
            bestCsvPath,
            best
        );


        std::cout
            << "\nBest-result CSV: "
            << bestCsvPath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "DCT error-redistribution joint search failed: "
            << exception.what()
            << "\n";

        return 1;
    }
}

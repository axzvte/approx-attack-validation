#include "analysis/dct_multi_state_joint_search.hpp"

#include "applications/dct.hpp"
#include "applications/dct8_fixed_graph.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <filesystem>
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


        const int imageIndex =
            (
                argc >= 3
            )
            ?
            std::stoi(
                argv[2]
            )
            :
            1;


        const int nodeCount =
            (
                argc >= 4
            )
            ?
            std::stoi(
                argv[3]
            )
            :
            3;


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
                "Demo node count must be in [1, 5]."
            );
        }


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


        // 当前跑通整条逻辑用的工作 Baseline。
        const auto sparseBaseline =
            applications::Dct8FixedGraph::
                createSparseApproximateBaselineConfig(
                    {
                        25,
                        19,
                        13
                    },
                    approximate::ApproxUnitId::Add12se5RP
                );


        applications::DctApplication
            application(
                sparseBaseline
            );


        // 这里只是为了验证 Module 3-B 的 1~5 节点扩展流程。
        //
        // 节点来自前面敏感性筛选，
        // monitor 来自之前的区域区分实验，
        // unit 暂时选用已经观察到具有攻击/补偿能力的型号。
        //
        // 它们不是最终硬件配置。
        const analysis::AttackStructure
            candidateStructure =
        {
            {
                6,
                approximate::ApproxUnitId::Add12se5Z0,
                core::MonitorSignal::Input1
            },

            {
                20,
                approximate::ApproxUnitId::Add12se5QC,
                core::MonitorSignal::Input2
            },

            {
                0,
                approximate::ApproxUnitId::Add12se5SB,
                core::MonitorSignal::BaselineOutput
            },

            {
                7,
                approximate::ApproxUnitId::Add12se5Z0,
                core::MonitorSignal::Input1
            },

            {
                26,
                approximate::ApproxUnitId::Add12se5L8,
                core::MonitorSignal::Input2
            }
        };


        analysis::AttackStructure
            structure(
                candidateStructure.begin(),
                candidateStructure.begin()
                +
                nodeCount
            );


        analysis::DctMultiStateJointSearchOptions
            options;


        options.representativeIntervalCount =
            20;


        options.beamWidth =
            20;


        options.refinementRounds =
            1;


        options.globalPsnrThreshold =
            30.0;


        std::cout
            << "DCT multi-state joint search (Module 3-B)\n"
            << "=========================================\n"
            << "Image: image_"
            << twoDigit(
                imageIndex
            )
            << ".jpg\n"
            << "Baseline: Sparse-3 (nodes 25,19,13 = 5RP; others exact)\n"
            << "Structure nodes used: "
            << nodeCount
            << "\n"
            << "Representative intervals/state/node: "
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
                << signalName(
                    node.monitorInput
                )
                << " / "
                << unitName(
                    node.unit
                )
                << "\n";
        }


        const auto result =
            analysis::
                DctMultiStateJointSearch::
                    search(
                        application,
                        inputImage,
                        roiMask,
                        structure,
                        options
                    );


        std::cout
            << "\nInitial metrics\n"
            << "---------------\n"
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
            << "\n\n";


        std::cout
            << "Layer summary\n"
            << "-------------\n"
            << std::left
            << std::setw(8)
            << "Pass"
            << std::setw(8)
            << "Node"
            << std::setw(12)
            << "Mode"
            << std::setw(12)
            << "Input"
            << std::setw(12)
            << "Expanded"
            << std::setw(12)
            << "Pareto"
            << "Kept"
            << "\n";


        for (const auto& layer : result.layers)
        {
            std::cout
                << std::left
                << std::setw(8)
                << layer.passIndex
                << std::setw(8)
                << layer.nodeId
                << std::setw(12)
                << (
                    layer.refinement
                    ?
                    "refine"
                    :
                    "forward"
                )
                << std::setw(12)
                << layer.inputStateCount
                << std::setw(12)
                << layer.expandedStateCount
                << std::setw(12)
                << layer.paretoStateCount
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
            << std::setw(7)
            << "Rank"
            << std::setw(14)
            << "Global"
            << std::setw(14)
            << "ROI"
            << std::setw(14)
            << "NonROI"
            << "ActiveNodes"
            << "\n";


        for (std::size_t index = 0;
             index < finalStates.size();
             ++index)
        {
            const auto& state =
                finalStates[index];


            std::cout
                << std::left
                << std::setw(7)
                << (index + 1)
                << std::setw(14)
                << state.metrics.globalPsnr
                << std::setw(14)
                << state.metrics.roiPsnr
                << std::setw(14)
                << state.metrics.nonRoiPsnr
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
            << "\nBest feasible state\n"
            << "-------------------\n"
            << "Global PSNR: "
            << best.metrics.globalPsnr
            << "\n"
            << "ROI PSNR: "
            << best.metrics.roiPsnr
            << "\n"
            << "Non-ROI PSNR: "
            << best.metrics.nonRoiPsnr
            << "\n"
            << "Active attack nodes: "
            << best.configuration.size()
            << "\n";


        for (const auto& config : best.configuration)
        {
            std::cout
                << "  Node "
                << config.nodeId
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


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "DCT multi-state joint search failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

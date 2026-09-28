#include "analysis/search_space_statistics.hpp"
#include "applications/dct.hpp"

#include "approximate/evoapprox_adapter.hpp"

#include <filesystem>
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

}


int main(
    int argc,
    char** argv
)
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


    analysis::TwoStageDataset
        dataset;


    dataset.roiMaskPath =
        (
            dataRoot
            /
            "mask"
            /
            "roi_mask.jpg"
        ).string();


    for (int i = 1; i <= 10; ++i)
    {
        dataset.stage1Images.push_back(
            {
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
            }
        );
    }


    for (int i = 11; i <= 20; ++i)
    {
        dataset.stage2Images.push_back(
            {
                (
                    dataRoot
                    /
                    "stage2"
                    /
                    "input"
                    /
                    (
                        "image_"
                        +
                        twoDigit(i)
                        +
                        ".png"
                    )
                ).string()
            }
        );
    }


    // Baseline 当前默认使用 5RP。
    //
    // 攻击单元使用其余 8 个近似加法器，
    // 避免“切换后仍然是 5RP”的无效配置。
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


    constexpr std::size_t
        maxAttackNodes =
            5;


    const auto statistics =
        analysis::SearchSpaceStatistics::analyze(
            application,
            dataset,
            attackUnits,
            maxAttackNodes
        );


    std::cout
        << "DCT Stage 1 baseline range statistics\n"
        << "Baseline: 5RP\n"
        << "Attack units: "
        << attackUnits.size()
        << "\n"
        << "Monitor inputs per node: 2\n"
        << "Maximum attacked nodes: "
        << maxAttackNodes
        << "\n\n";


    for (
        const auto& node :
        statistics.nodes
    )
    {
        std::cout
            << "Node "
            << node.nodeId
            << "\n";


        std::cout
            << "  input1 range: ["
            << node.input1Range.minimum
            << ", "
            << node.input1Range.maximum
            << "]\n";


        std::cout
            << "  input1 intervals: "
            << node.input1IntervalCount
            << "\n";


        std::cout
            << "  input2 range: ["
            << node.input2Range.minimum
            << ", "
            << node.input2Range.maximum
            << "]\n";


        std::cout
            << "  input2 intervals: "
            << node.input2IntervalCount
            << "\n";


        std::cout
            << "  node candidate count: "
            << node.candidateCount
            << "\n\n";
    }


    std::cout
        << "Configurations by attacked-node count:\n";


    for (
        const auto& item :
        statistics.byAttackNodeCount
    )
    {
        std::cout
            << "  "
            << item.attackNodeCount
            << " node(s): "
            << item.configurationCount
            << "\n";
    }


    std::cout
        << "\nTotal configurations (1-"
        << maxAttackNodes
        << " nodes): "
        << statistics.totalConfigurations
        << "\n";


    return 0;
}

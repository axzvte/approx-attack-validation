#include "analysis/dct_multi_state_joint_search.hpp"

#include "applications/dct.hpp"
#include "applications/sobel.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>


namespace
{

struct Stage1Row
{
    int nodeId = -1;

    std::string nodeName;

    std::string monitor;

    std::string unit;
};


using TupleKey =
    std::tuple<
        int,
        std::string,
        std::string
    >;


struct HardwareCandidate
{
    std::string name;

    analysis::AttackStructure structure;
};


struct CandidateSummary
{
    std::string name;

    std::size_t nodeCount = 0;

    std::size_t successCount = 0;

    double meanGlobal = 0.0;

    double meanRoi = 0.0;

    double meanNonRoi = 0.0;

    double meanAdvantage = 0.0;

    double minGlobal = 1.0e30;

    analysis::AttackStructure structure;
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


core::MonitorSignal parseMonitor(
    const std::string& value
)
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
        value == "baseline"
    )
    {
        return core::MonitorSignal::BaselineOutput;
    }


    throw std::runtime_error(
        "Unknown monitor signal: "
        +
        value
    );
}


approximate::ApproxUnitId parseUnit(
    const std::string& value
)
{
    if (value == "5L8") return approximate::ApproxUnitId::Add12se5L8;
    if (value == "5PD") return approximate::ApproxUnitId::Add12se5PD;
    if (value == "5PN") return approximate::ApproxUnitId::Add12se5PN;
    if (value == "5QC") return approximate::ApproxUnitId::Add12se5QC;
    if (value == "5QT") return approximate::ApproxUnitId::Add12se5QT;
    if (value == "5RP") return approximate::ApproxUnitId::Add12se5RP;
    if (value == "5SB") return approximate::ApproxUnitId::Add12se5SB;
    if (value == "5TE") return approximate::ApproxUnitId::Add12se5TE;
    if (value == "5Z0") return approximate::ApproxUnitId::Add12se5Z0;


    throw std::runtime_error(
        "Unknown approximate unit: "
        +
        value
    );
}


std::string monitorName(
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


std::vector<Stage1Row> loadStage1Csv(
    const std::filesystem::path& path
)
{
    std::ifstream file(
        path
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Unable to open Stage1 result CSV: "
            +
            path.string()
        );
    }


    std::string line;


    if (
        !std::getline(
            file,
            line
        )
    )
    {
        throw std::runtime_error(
            "Empty Stage1 result CSV: "
            +
            path.string()
        );
    }


    std::vector<Stage1Row>
        rows;


    while (
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


        if (fields.size() < 4)
        {
            throw std::runtime_error(
                "Malformed Stage1 result row."
            );
        }


        rows.push_back(
            Stage1Row{
                std::stoi(
                    fields[0]
                ),
                fields[1],
                fields[2],
                fields[3]
            }
        );
    }


    return rows;
}


std::string nodeName(
    const core::Application& application,
    int nodeId
)
{
    for (const auto& node : application.addNodes())
    {
        if (node.id == nodeId)
        {
            return node.name;
        }
    }


    return
        "node_"
        +
        std::to_string(
            nodeId
        );
}


std::filesystem::path stagedImagePath(
    const std::filesystem::path& dataRoot,
    const std::string& stage,
    int localIndex,
    int globalOffset
)
{
    const auto globalPath =
        dataRoot
        /
        stage
        /
        "input"
        /
        (
            "image_"
            +
            twoDigit(
                localIndex + globalOffset
            )
            +
            ".jpg"
        );


    if (
        std::filesystem::exists(
            globalPath
        )
    )
    {
        return globalPath;
    }


    const auto localPath =
        dataRoot
        /
        stage
        /
        "input"
        /
        (
            "image_"
            +
            twoDigit(
                localIndex
            )
            +
            ".jpg"
        );


    if (
        std::filesystem::exists(
            localPath
        )
    )
    {
        return localPath;
    }


    throw std::runtime_error(
        "Missing image for "
        +
        stage
        +
        ": "
        +
        globalPath.string()
    );
}


std::vector<HardwareCandidate> buildCandidates(
    const std::filesystem::path& resultDirectory,
    const std::string& prefix
)
{
    std::map<int, std::size_t>
        nodeFrequency;


    std::map<TupleKey, std::size_t>
        tupleFrequency;


    for (int imageIndex = 1;
         imageIndex <= 10;
         ++imageIndex)
    {
        const auto path =
            resultDirectory
            /
            (
                prefix
                +
                "_joint_search_best_image_"
                +
                twoDigit(
                    imageIndex
                )
                +
                ".csv"
            );


        const auto rows =
            loadStage1Csv(
                path
            );


        std::set<int>
            seenNodes;


        for (const auto& row : rows)
        {
            if (
                seenNodes.insert(
                    row.nodeId
                ).second
            )
            {
                ++nodeFrequency[
                    row.nodeId
                ];
            }


            ++tupleFrequency[
                TupleKey{
                    row.nodeId,
                    row.monitor,
                    row.unit
                }
            ];
        }
    }


    std::vector<
        std::pair<
            int,
            std::size_t
        >
    >
        rankedNodes(
            nodeFrequency.begin(),
            nodeFrequency.end()
        );


    std::sort(
        rankedNodes.begin(),
        rankedNodes.end(),

        [](
            const auto& first,
            const auto& second
        )
        {
            if (
                first.second
                !=
                second.second
            )
            {
                return
                    first.second
                    >
                    second.second;
            }


            return
                first.first
                <
                second.first;
        }
    );


    std::vector<int>
        coreNodes;


    for (const auto& item : rankedNodes)
    {
        if (
            item.second >= 5
            &&
            coreNodes.size() < 5
        )
        {
            coreNodes.push_back(
                item.first
            );
        }
    }


    const std::size_t desiredMinimum =
        std::min<std::size_t>(
            4,
            rankedNodes.size()
        );


    for (const auto& item : rankedNodes)
    {
        if (
            coreNodes.size()
            >=
            desiredMinimum
        )
        {
            break;
        }


        if (
            std::find(
                coreNodes.begin(),
                coreNodes.end(),
                item.first
            )
            ==
            coreNodes.end()
        )
        {
            coreNodes.push_back(
                item.first
            );
        }
    }


    if (coreNodes.empty())
    {
        throw std::runtime_error(
            "No core nodes could be derived from Stage1."
        );
    }


    struct TupleChoice
    {
        core::MonitorSignal monitor =
            core::MonitorSignal::Input1;

        approximate::ApproxUnitId unit =
            approximate::ApproxUnitId::Add12se5RP;

        std::size_t count = 0;
    };


    std::map<
        int,
        std::vector<TupleChoice>
    >
        choices;


    for (const int nodeId : coreNodes)
    {
        std::vector<TupleChoice>
            nodeChoices;


        for (const auto& item : tupleFrequency)
        {
            if (
                std::get<0>(
                    item.first
                )
                !=
                nodeId
            )
            {
                continue;
            }


            nodeChoices.push_back(
                TupleChoice{
                    parseMonitor(
                        std::get<1>(
                            item.first
                        )
                    ),
                    parseUnit(
                        std::get<2>(
                            item.first
                        )
                    ),
                    item.second
                }
            );
        }


        std::sort(
            nodeChoices.begin(),
            nodeChoices.end(),

            [](
                const TupleChoice& first,
                const TupleChoice& second
            )
            {
                return
                    first.count
                    >
                    second.count;
            }
        );


        if (nodeChoices.empty())
        {
            throw std::runtime_error(
                "Core node has no tuple choice."
            );
        }


        choices[
            nodeId
        ] =
            std::move(
                nodeChoices
            );
    }


    const auto buildBaseStructure =
        [&]()
        {
            analysis::AttackStructure
                structure;


            for (const int nodeId : coreNodes)
            {
                const auto& choice =
                    choices.at(
                        nodeId
                    ).front();


                structure.push_back(
                    analysis::AttackStructureNode{
                        nodeId,
                        choice.unit,
                        choice.monitor
                    }
                );
            }


            return structure;
        };


    std::vector<HardwareCandidate>
        candidates;


    candidates.push_back(
        HardwareCandidate{
            "A",
            buildBaseStructure()
        }
    );


    struct Ambiguity
    {
        int nodeId = -1;

        std::size_t secondCount = 0;

        double ratio = 0.0;
    };


    std::vector<Ambiguity>
        ambiguities;


    for (const int nodeId : coreNodes)
    {
        const auto& nodeChoices =
            choices.at(
                nodeId
            );


        if (nodeChoices.size() < 2)
        {
            continue;
        }


        ambiguities.push_back(
            Ambiguity{
                nodeId,
                nodeChoices[1].count,
                static_cast<double>(
                    nodeChoices[1].count
                )
                /
                static_cast<double>(
                    nodeChoices[0].count
                )
            }
        );
    }


    std::sort(
        ambiguities.begin(),
        ambiguities.end(),

        [](
            const Ambiguity& first,
            const Ambiguity& second
        )
        {
            if (
                first.secondCount
                !=
                second.secondCount
            )
            {
                return
                    first.secondCount
                    >
                    second.secondCount;
            }


            return
                first.ratio
                >
                second.ratio;
        }
    );


    const std::size_t variantCount =
        std::min<std::size_t>(
            2,
            ambiguities.size()
        );


    for (std::size_t variantIndex = 0;
         variantIndex < variantCount;
         ++variantIndex)
    {
        auto structure =
            buildBaseStructure();


        const int targetNode =
            ambiguities[
                variantIndex
            ].nodeId;


        const auto& secondChoice =
            choices.at(
                targetNode
            )[1];


        for (auto& node : structure)
        {
            if (node.nodeId == targetNode)
            {
                node.monitorInput =
                    secondChoice.monitor;


                node.unit =
                    secondChoice.unit;


                break;
            }
        }


        candidates.push_back(
            HardwareCandidate{
                std::string(
                    1,
                    static_cast<char>(
                        'B'
                        +
                        variantIndex
                    )
                ),
                std::move(
                    structure
                )
            }
        );
    }


    if (coreNodes.size() > 3)
    {
        int removeNode =
            coreNodes.back();


        std::size_t removeFrequency =
            nodeFrequency[
                removeNode
            ];


        for (const int nodeId : coreNodes)
        {
            const std::size_t frequency =
                nodeFrequency[
                    nodeId
                ];


            if (
                frequency
                <
                removeFrequency
            )
            {
                removeNode =
                    nodeId;


                removeFrequency =
                    frequency;
            }
        }


        auto reduced =
            buildBaseStructure();


        reduced.erase(
            std::remove_if(
                reduced.begin(),
                reduced.end(),

                [removeNode](
                    const analysis::AttackStructureNode& node
                )
                {
                    return
                        node.nodeId
                        ==
                        removeNode;
                }
            ),
            reduced.end()
        );


        candidates.push_back(
            HardwareCandidate{
                "D",
                std::move(
                    reduced
                )
            }
        );
    }


    return candidates;
}


bool betterSummary(
    const CandidateSummary& first,
    const CandidateSummary& second
)
{
    if (
        first.successCount
        !=
        second.successCount
    )
    {
        return
            first.successCount
            >
            second.successCount;
    }


    if (
        first.meanRoi
        !=
        second.meanRoi
    )
    {
        return
            first.meanRoi
            <
            second.meanRoi;
    }


    if (
        first.meanAdvantage
        !=
        second.meanAdvantage
    )
    {
        return
            first.meanAdvantage
            >
            second.meanAdvantage;
    }


    if (
        first.nodeCount
        !=
        second.nodeCount
    )
    {
        return
            first.nodeCount
            <
            second.nodeCount;
    }


    return
        first.name
        <
        second.name;
}

}


int main(
    int argc,
    char** argv
)
{
    try
    {
        if (argc < 4)
        {
            throw std::runtime_error(
                "Usage: automatic_fixed_hardware_pipeline <dct|sobel> <data_root> <result_dir> [representatives] [beam] [refinement] [boundaries] [full_validation]"
            );
        }


        const std::string applicationName =
            argv[1];


        const std::filesystem::path dataRoot =
            argv[2];


        const std::filesystem::path resultDirectory =
            argv[3];


        const std::size_t representatives =
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


        const std::size_t boundaryCount =
            argc >= 8
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[7]
                )
            )
            :
            24;


        const std::size_t fullValidationCount =
            argc >= 9
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[8]
                )
            )
            :
            24;


        std::unique_ptr<core::Application>
            application;


        std::string prefix;


        if (applicationName == "dct")
        {
            application =
                std::make_unique<
                    applications::DctApplication
                >();


            prefix =
                "dct";
        }
        else if (applicationName == "sobel")
        {
            application =
                std::make_unique<
                    applications::SobelApplication
                >();


            prefix =
                "sobel";
        }
        else
        {
            throw std::runtime_error(
                "Unsupported application: "
                +
                applicationName
            );
        }


        std::filesystem::create_directories(
            resultDirectory
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


        const auto candidates =
            buildCandidates(
                resultDirectory,
                prefix
            );


        std::cout
            << "\nAutomatic fixed-hardware pipeline: "
            << applicationName
            << "\n"
            << "========================================\n"
            << "Derived candidates: "
            << candidates.size()
            << "\n";


        for (const auto& candidate : candidates)
        {
            std::cout
                << "\nCandidate "
                << candidate.name
                << " ("
                << candidate.structure.size()
                << " nodes)\n";


            for (const auto& node : candidate.structure)
            {
                std::cout
                    << "  Node "
                    << node.nodeId
                    << " / "
                    << nodeName(
                        *application,
                        node.nodeId
                    )
                    << " / "
                    << monitorName(
                        node.monitorInput
                    )
                    << " / "
                    << unitName(
                        node.unit
                    )
                    << "\n";
            }
        }


        analysis::DctMultiStateJointSearchOptions
            options;


        options.redistributionUnits.clear();

        options.monitorSignals.clear();

        options.candidateBoundaryCount =
            boundaryCount;

        options.representativeIntervalCount =
            representatives;

        options.fullValidationCandidateCount =
            fullValidationCount;

        options.beamWidth =
            beamWidth;

        options.refinementRounds =
            refinementRounds;

        options.globalPsnrThreshold =
            30.0;


        std::vector<CandidateSummary>
            summaries;


        for (const auto& candidate : candidates)
        {
            CandidateSummary
                summary;


            summary.name =
                candidate.name;


            summary.nodeCount =
                candidate.structure.size();


            summary.structure =
                candidate.structure;


            std::cout
                << "\n\n========== Stage2 Candidate "
                << candidate.name
                << " ==========\n";


            for (int imageIndex = 1;
                 imageIndex <= 10;
                 ++imageIndex)
            {
                const auto imagePath =
                    stagedImagePath(
                        dataRoot,
                        "stage2",
                        imageIndex,
                        10
                    );


                const cv::Mat inputImage =
                    image_io::loadGrayImage(
                        imagePath.string()
                    );


                const cv::Mat roiMask =
                    region_mask::resizeMaskToImage(
                        sharedRoiMask,
                        inputImage
                    );


                std::cout
                    << "\n["
                    << prefix
                    << "-stage2] candidate "
                    << candidate.name
                    << " / image "
                    << imageIndex
                    << "/10 starting"
                    << std::endl;


                const auto result =
                    analysis::DctMultiStateJointSearch::
                        search(
                            *application,
                            inputImage,
                            roiMask,
                            candidate.structure,
                            options,

                            [&prefix, &candidate, imageIndex](
                                const analysis::DctMultiStateJointSearchProgress& progress
                            )
                            {
                                std::cout
                                    << "\r["
                                    << prefix
                                    << "-stage2] "
                                    << candidate.name
                                    << " / image "
                                    << imageIndex
                                    << "/10"
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
                                        << std::endl;
                                }
                            }
                        );


                if (!result.hasBestFeasibleState)
                {
                    std::cout
                        << "["
                        << prefix
                        << "-stage2] FAIL\n";


                    continue;
                }


                const auto& baseline =
                    result.initialMetrics;


                const auto& best =
                    result.bestFeasibleState;


                const double advantage =
                    (
                        best.metrics.roiMse
                        -
                        baseline.roiMse
                    )
                    -
                    (
                        best.metrics.nonRoiMse
                        -
                        baseline.nonRoiMse
                    );


                ++summary.successCount;


                summary.meanGlobal +=
                    best.metrics.globalPsnr;


                summary.meanRoi +=
                    best.metrics.roiPsnr;


                summary.meanNonRoi +=
                    best.metrics.nonRoiPsnr;


                summary.meanAdvantage +=
                    advantage;


                summary.minGlobal =
                    std::min(
                        summary.minGlobal,
                        best.metrics.globalPsnr
                    );


                std::cout
                    << std::fixed
                    << std::setprecision(
                        6
                    )
                    << "["
                    << prefix
                    << "-stage2] SUCCESS"
                    << " / G="
                    << best.metrics.globalPsnr
                    << " / ROI="
                    << best.metrics.roiPsnr
                    << " / NROI="
                    << best.metrics.nonRoiPsnr
                    << " / Adv="
                    << advantage
                    << "\n";
            }


            if (summary.successCount > 0)
            {
                const double denominator =
                    static_cast<double>(
                        summary.successCount
                    );


                summary.meanGlobal /=
                    denominator;

                summary.meanRoi /=
                    denominator;

                summary.meanNonRoi /=
                    denominator;

                summary.meanAdvantage /=
                    denominator;
            }


            summaries.push_back(
                summary
            );
        }


        std::sort(
            summaries.begin(),
            summaries.end(),
            betterSummary
        );


        std::cout
            << "\n\n"
            << applicationName
            << " Stage2 automatic hardware screening summary\n"
            << "================================================\n"
            << std::left
            << std::setw(7)
            << "Rank"
            << std::setw(8)
            << "HW"
            << std::setw(10)
            << "Nodes"
            << std::setw(12)
            << "Success"
            << std::setw(14)
            << "MeanGlobal"
            << std::setw(14)
            << "MeanROI"
            << std::setw(14)
            << "MeanNonROI"
            << std::setw(16)
            << "MeanAdv"
            << "MinGlobal\n"
            << std::fixed
            << std::setprecision(
                6
            );


        for (std::size_t index = 0;
             index < summaries.size();
             ++index)
        {
            const auto& summary =
                summaries[index];


            std::cout
                << std::setw(7)
                << (index + 1)
                << std::setw(8)
                << summary.name
                << std::setw(10)
                << summary.nodeCount
                << std::setw(12)
                << (
                    std::to_string(
                        summary.successCount
                    )
                    +
                    "/10"
                );


            if (summary.successCount > 0)
            {
                std::cout
                    << std::setw(14)
                    << summary.meanGlobal
                    << std::setw(14)
                    << summary.meanRoi
                    << std::setw(14)
                    << summary.meanNonRoi
                    << std::setw(16)
                    << summary.meanAdvantage
                    << summary.minGlobal;
            }


            std::cout
                << "\n";
        }


        if (summaries.empty())
        {
            throw std::runtime_error(
                "No Stage2 candidate summaries."
            );
        }


        const auto& selected =
            summaries.front();


        std::cout
            << "\nSelected H* = "
            << selected.name
            << "\n";


        for (const auto& node : selected.structure)
        {
            std::cout
                << "  Node "
                << node.nodeId
                << " / "
                << nodeName(
                    *application,
                    node.nodeId
                )
                << " / "
                << monitorName(
                    node.monitorInput
                )
                << " / "
                << unitName(
                    node.unit
                )
                << "\n";
        }


        std::size_t stage3Success =
            0;


        double stage3Global =
            0.0;

        double stage3Roi =
            0.0;

        double stage3NonRoi =
            0.0;

        double stage3Advantage =
            0.0;

        double stage3MinGlobal =
            1.0e30;

        double stage3MaxRoi =
            -1.0e30;


        for (int imageIndex = 1;
             imageIndex <= 10;
             ++imageIndex)
        {
            const auto imagePath =
                stagedImagePath(
                    dataRoot,
                    "stage3",
                    imageIndex,
                    20
                );


            const cv::Mat inputImage =
                image_io::loadGrayImage(
                    imagePath.string()
                );


            const cv::Mat roiMask =
                region_mask::resizeMaskToImage(
                    sharedRoiMask,
                    inputImage
                );


            std::cout
                << "\n["
                << prefix
                << "-stage3] image "
                << (imageIndex + 20)
                << " starting"
                << std::endl;


            const auto result =
                analysis::DctMultiStateJointSearch::
                    search(
                        *application,
                        inputImage,
                        roiMask,
                        selected.structure,
                        options,

                        [&prefix, imageIndex](
                            const analysis::DctMultiStateJointSearchProgress& progress
                        )
                        {
                            std::cout
                                << "\r["
                                << prefix
                                << "-stage3] image "
                                << (imageIndex + 20)
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
                                    << std::endl;
                            }
                        }
                    );


            if (!result.hasBestFeasibleState)
            {
                std::cout
                    << "["
                    << prefix
                    << "-stage3] FAIL\n";


                continue;
            }


            const auto& baseline =
                result.initialMetrics;


            const auto& best =
                result.bestFeasibleState;


            const double advantage =
                (
                    best.metrics.roiMse
                    -
                    baseline.roiMse
                )
                -
                (
                    best.metrics.nonRoiMse
                    -
                    baseline.nonRoiMse
                );


            ++stage3Success;


            stage3Global +=
                best.metrics.globalPsnr;


            stage3Roi +=
                best.metrics.roiPsnr;


            stage3NonRoi +=
                best.metrics.nonRoiPsnr;


            stage3Advantage +=
                advantage;


            stage3MinGlobal =
                std::min(
                    stage3MinGlobal,
                    best.metrics.globalPsnr
                );


            stage3MaxRoi =
                std::max(
                    stage3MaxRoi,
                    best.metrics.roiPsnr
                );


            std::cout
                << std::fixed
                << std::setprecision(
                    6
                )
                << "["
                << prefix
                << "-stage3] SUCCESS"
                << " / G="
                << best.metrics.globalPsnr
                << " / ROI="
                << best.metrics.roiPsnr
                << " / NROI="
                << best.metrics.nonRoiPsnr
                << " / Adv="
                << advantage
                << "\n";
        }


        std::cout
            << "\n\n"
            << applicationName
            << " Stage3 final validation summary\n"
            << "========================================\n"
            << "Fixed hardware: H* = "
            << selected.name
            << "\n"
            << "Success: "
            << stage3Success
            << "/10\n";


        if (stage3Success > 0)
        {
            const double denominator =
                static_cast<double>(
                    stage3Success
                );


            stage3Global /=
                denominator;

            stage3Roi /=
                denominator;

            stage3NonRoi /=
                denominator;

            stage3Advantage /=
                denominator;


            std::cout
                << std::fixed
                << std::setprecision(
                    6
                )
                << "Mean Global PSNR: "
                << stage3Global
                << "\n"
                << "Mean ROI PSNR: "
                << stage3Roi
                << "\n"
                << "Mean Non-ROI PSNR: "
                << stage3NonRoi
                << "\n"
                << "Mean Redistribution Advantage: "
                << stage3Advantage
                << "\n"
                << "Min Global PSNR: "
                << stage3MinGlobal
                << "\n"
                << "Max ROI PSNR: "
                << stage3MaxRoi
                << "\n";
        }


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Automatic fixed-hardware pipeline failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

#include "analysis/dct_multi_state_joint_search.hpp"

#include "applications/sharpen.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{

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

    double minimumGlobal =
        std::numeric_limits<double>::infinity();
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


std::filesystem::path stage2ImagePath(
    const std::filesystem::path& dataRoot,
    int index
)
{
    const auto primary =
        dataRoot
        /
        "stage2"
        /
        "input"
        /
        (
            "image_"
            +
            twoDigit(
                index + 10
            )
            +
            ".jpg"
        );


    if (
        std::filesystem::exists(
            primary
        )
    )
    {
        return primary;
    }


    const auto fallback =
        dataRoot
        /
        "stage2"
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
        );


    if (
        std::filesystem::exists(
            fallback
        )
    )
    {
        return fallback;
    }


    throw std::runtime_error(
        "Missing Sharpen Stage2 image."
    );
}


std::vector<HardwareCandidate>
buildCandidates()
{
    return
    {
        {
            "A",
            {
                {
                    0,
                    approximate::ApproxUnitId::Add12se5QT,
                    core::MonitorSignal::BaselineOutput
                },
                {
                    1,
                    approximate::ApproxUnitId::Add12se5QT,
                    core::MonitorSignal::Input1
                },
                {
                    2,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::Input2
                },
                {
                    3,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::Input2
                }
            }
        },

        {
            "B",
            {
                {
                    0,
                    approximate::ApproxUnitId::Add12se5QT,
                    core::MonitorSignal::Input1
                },
                {
                    1,
                    approximate::ApproxUnitId::Add12se5QT,
                    core::MonitorSignal::Input1
                },
                {
                    2,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::Input2
                },
                {
                    3,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::Input2
                }
            }
        },

        {
            "C",
            {
                {
                    0,
                    approximate::ApproxUnitId::Add12se5SB,
                    core::MonitorSignal::Input2
                },
                {
                    1,
                    approximate::ApproxUnitId::Add12se5SB,
                    core::MonitorSignal::Input2
                },
                {
                    2,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::Input2
                },
                {
                    3,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::Input2
                }
            }
        }
    };
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


        const std::size_t representativeCount =
            argc >= 4
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[3]
                )
            )
            :
            4;


        const std::size_t beamWidth =
            argc >= 5
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[4]
                )
            )
            :
            10;


        const std::size_t refinementRounds =
            argc >= 6
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[5]
                )
            )
            :
            0;


        const std::size_t boundaryCount =
            argc >= 7
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[6]
                )
            )
            :
            24;


        const std::size_t fullValidationCount =
            argc >= 8
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[7]
                )
            )
            :
            24;


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


        applications::SharpenApplication
            application;


        analysis::DctMultiStateJointSearchOptions
            options;


        // Stage2 的关键：
        // Node / Monitor / Unit 全部由候选硬件固定，
        // 搜索器只调整 Interval。
        options.redistributionUnits.clear();

        options.monitorSignals.clear();


        options.candidateBoundaryCount =
            boundaryCount;


        options.representativeIntervalCount =
            representativeCount;


        options.fullValidationCandidateCount =
            fullValidationCount;


        options.beamWidth =
            beamWidth;


        options.refinementRounds =
            refinementRounds;


        options.globalPsnrThreshold =
            30.0;


        const auto candidates =
            buildCandidates();


        std::cout
            << "Sharpening Stage2 fixed-hardware screening\n"
            << "=========================================\n"
            << "Stage2 images: 10\n"
            << "Fixed per candidate: Node + Monitor + Unit\n"
            << "Per-image search: Interval only\n"
            << "Constraint: Global >= 30 dB and DeltaMSE_ROI > DeltaMSE_NonROI\n"
            << "Objective: maximize Redistribution Advantage; tie-break: lower ROI PSNR\n\n";


        for (const auto& candidate : candidates)
        {
            std::cout
                << "Candidate "
                << candidate.name
                << "\n";


            for (const auto& node : candidate.structure)
            {
                std::cout
                    << "  Node "
                    << node.nodeId
                    << " / "
                    << applications::SharpenApplication::nodeName(
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


        std::vector<CandidateSummary>
            summaries;


        const auto detailPath =
            outputDirectory
            /
            "sharpen_stage2_hardware_screening_detail.csv";


        std::ofstream detailCsv(
            detailPath
        );


        if (!detailCsv.is_open())
        {
            throw std::runtime_error(
                "Unable to create Sharpen Stage2 detail CSV."
            );
        }


        detailCsv
            << "candidate,image_index,success,active_node_count,"
            << "baseline_global,baseline_roi,baseline_non_roi,"
            << "final_global,final_roi,final_non_roi,"
            << "roi_mse_increase,non_roi_mse_increase,redistribution_advantage\n";


        for (const auto& candidate : candidates)
        {
            CandidateSummary
                summary;


            summary.name =
                candidate.name;


            summary.nodeCount =
                candidate.structure.size();


            std::cout
                << "\n========== Candidate "
                << candidate.name
                << " ==========\n";


            for (int index = 1;
                 index <= 10;
                 ++index)
            {
                const auto inputPath =
                    stage2ImagePath(
                        dataRoot,
                        index
                    );


                const cv::Mat inputImage =
                    image_io::loadGrayImage(
                        inputPath.string()
                    );


                const cv::Mat roiMask =
                    region_mask::resizeMaskToImage(
                        sharedRoiMask,
                        inputImage
                    );


                std::cout
                    << "\n[sharpen-stage2] candidate "
                    << candidate.name
                    << " / image "
                    << index
                    << "/10 starting"
                    << std::endl;


                const auto result =
                    analysis::DctMultiStateJointSearch::
                        search(
                            application,
                            inputImage,
                            roiMask,
                            candidate.structure,
                            options,

                            [&candidate, index](
                                const analysis::DctMultiStateJointSearchProgress& progress
                            )
                            {
                                std::cout
                                    << "\r[sharpen-stage2] candidate "
                                    << candidate.name
                                    << " / image "
                                    << index
                                    << "/10"
                                    << " / pass "
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
                                        << std::endl;
                                }
                            }
                        );


                const auto& baseline =
                    result.initialMetrics;


                if (!result.hasBestFeasibleState)
                {
                    std::cout
                        << "[sharpen-stage2] candidate "
                        << candidate.name
                        << " / image "
                        << index
                        << " FAIL\n";


                    detailCsv
                        << candidate.name
                        << ","
                        << index
                        << ",0,0,"
                        << baseline.globalPsnr
                        << ","
                        << baseline.roiPsnr
                        << ","
                        << baseline.nonRoiPsnr
                        << ",,,,,,\n";


                    continue;
                }


                const auto& best =
                    result.bestFeasibleState;


                const double roiMseIncrease =
                    best.metrics.roiMse
                    -
                    baseline.roiMse;


                const double nonRoiMseIncrease =
                    best.metrics.nonRoiMse
                    -
                    baseline.nonRoiMse;


                const double advantage =
                    roiMseIncrease
                    -
                    nonRoiMseIncrease;


                ++summary.successCount;


                summary.meanGlobal +=
                    best.metrics.globalPsnr;


                summary.meanRoi +=
                    best.metrics.roiPsnr;


                summary.meanNonRoi +=
                    best.metrics.nonRoiPsnr;


                summary.meanAdvantage +=
                    advantage;


                summary.minimumGlobal =
                    std::min(
                        summary.minimumGlobal,
                        best.metrics.globalPsnr
                    );


                std::cout
                    << std::fixed
                    << std::setprecision(
                        6
                    )
                    << "[sharpen-stage2] candidate "
                    << candidate.name
                    << " / image "
                    << index
                    << " SUCCESS"
                    << " / G="
                    << best.metrics.globalPsnr
                    << " / ROI="
                    << best.metrics.roiPsnr
                    << " / NROI="
                    << best.metrics.nonRoiPsnr
                    << " / Adv="
                    << advantage
                    << " / Active="
                    << best.configuration.size()
                    << "\n";


                detailCsv
                    << candidate.name
                    << ","
                    << index
                    << ",1,"
                    << best.configuration.size()
                    << ","
                    << baseline.globalPsnr
                    << ","
                    << baseline.roiPsnr
                    << ","
                    << baseline.nonRoiPsnr
                    << ","
                    << best.metrics.globalPsnr
                    << ","
                    << best.metrics.roiPsnr
                    << ","
                    << best.metrics.nonRoiPsnr
                    << ","
                    << roiMseIncrease
                    << ","
                    << nonRoiMseIncrease
                    << ","
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
            << "\n\nSharpening Stage2 hardware screening summary\n"
            << "=============================================\n"
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
                    << summary.minimumGlobal;
            }


            std::cout
                << "\n";
        }


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Sharpening Stage2 hardware screening failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

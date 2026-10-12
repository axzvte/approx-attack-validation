#include "analysis/dct_multi_state_joint_search.hpp"

#include "applications/conv.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
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

struct HardwareCandidate
{
    std::string name;

    analysis::AttackStructure structure;
};


struct ImageEvaluation
{
    int imageIndex = 0;

    bool success = false;

    analysis::DctImageQualityMetrics
        baselineMetrics;

    analysis::DctImageQualityMetrics
        finalMetrics;

    double roiMseIncrease = 0.0;

    double nonRoiMseIncrease = 0.0;

    double redistributionAdvantage = 0.0;

    analysis::AttackConfiguration
        configuration;
};


struct CandidateSummary
{
    std::string name;

    std::size_t hardwareNodeCount = 0;

    std::size_t successCount = 0;

    double meanGlobal = 0.0;

    double meanRoi = 0.0;

    double meanNonRoi = 0.0;

    double meanRedistributionAdvantage = 0.0;

    double minimumGlobal =
        std::numeric_limits<double>::infinity();

    double maximumRoi =
        -std::numeric_limits<double>::infinity();

    std::vector<ImageEvaluation>
        images;
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
    int imageIndex
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
                imageIndex
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


    // 兼容少数旧数据目录直接把图片放在 stage2/ 下。
    const auto fallback =
        dataRoot
        /
        "stage2"
        /
        (
            "image_"
            +
            twoDigit(
                imageIndex
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
        "Missing Stage2 image: "
        +
        primary.string()
    );
}


std::vector<HardwareCandidate>
buildCandidates()
{
    // A/B/C/D 来自 Stage1 10 张图片的自由搜索统计。
    //
    // 这一轮：
    // - Node 固定；
    // - Monitor 固定；
    // - Approximate Unit 固定；
    // - 每张 Stage2 图片只重新搜索各节点 Interval。
    return
    {
        {
            "A",
            {
                {
                    5,
                    approximate::ApproxUnitId::Add12se5PD,
                    core::MonitorSignal::Input2
                },
                {
                    0,
                    approximate::ApproxUnitId::Add12se5SB,
                    core::MonitorSignal::Input1
                },
                {
                    1,
                    approximate::ApproxUnitId::Add12se5L8,
                    core::MonitorSignal::Input2
                },
                {
                    3,
                    approximate::ApproxUnitId::Add12se5PN,
                    core::MonitorSignal::BaselineOutput
                },
                {
                    6,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::Input1
                }
            }
        },

        {
            "B",
            {
                {
                    5,
                    approximate::ApproxUnitId::Add12se5PD,
                    core::MonitorSignal::Input2
                },
                {
                    0,
                    approximate::ApproxUnitId::Add12se5SB,
                    core::MonitorSignal::Input1
                },
                {
                    1,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::Input2
                },
                {
                    3,
                    approximate::ApproxUnitId::Add12se5PN,
                    core::MonitorSignal::BaselineOutput
                },
                {
                    6,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::Input1
                }
            }
        },

        {
            "C",
            {
                {
                    5,
                    approximate::ApproxUnitId::Add12se5PD,
                    core::MonitorSignal::Input2
                },
                {
                    0,
                    approximate::ApproxUnitId::Add12se5SB,
                    core::MonitorSignal::Input1
                },
                {
                    1,
                    approximate::ApproxUnitId::Add12se5L8,
                    core::MonitorSignal::Input2
                },
                {
                    3,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::BaselineOutput
                },
                {
                    6,
                    approximate::ApproxUnitId::Add12se5TE,
                    core::MonitorSignal::Input1
                }
            }
        },

        {
            "D",
            {
                {
                    5,
                    approximate::ApproxUnitId::Add12se5PD,
                    core::MonitorSignal::Input2
                },
                {
                    0,
                    approximate::ApproxUnitId::Add12se5SB,
                    core::MonitorSignal::Input1
                },
                {
                    1,
                    approximate::ApproxUnitId::Add12se5L8,
                    core::MonitorSignal::Input2
                },
                {
                    3,
                    approximate::ApproxUnitId::Add12se5PN,
                    core::MonitorSignal::BaselineOutput
                }
            }
        }
    };
}


void printStructure(
    const HardwareCandidate& candidate
)
{
    std::cout
        << "Candidate "
        << candidate.name
        << " ("
        << candidate.structure.size()
        << " hardware nodes)\n";


    for (const auto& node : candidate.structure)
    {
        std::cout
            << "  Node "
            << node.nodeId
            << " / "
            << applications::ConvApplication::nodeName(
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


bool betterSummary(
    const CandidateSummary& first,
    const CandidateSummary& second
)
{
    // 第二轮固定硬件筛选顺序：
    // 1. Stage2 成功图片数越多越好；
    // 2. 成功图片中的 Mean ROI PSNR 越低越好；
    // 3. Mean redistribution advantage 越大越好；
    // 4. 效果接近时硬件节点越少越好。
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
        first.successCount > 0
        &&
        second.successCount > 0
        &&
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
        first.meanRedistributionAdvantage
        !=
        second.meanRedistributionAdvantage
    )
    {
        return
            first.meanRedistributionAdvantage
            >
            second.meanRedistributionAdvantage;
    }


    if (
        first.hardwareNodeCount
        !=
        second.hardwareNodeCount
    )
    {
        return
            first.hardwareNodeCount
            <
            second.hardwareNodeCount;
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


        const std::size_t candidateBoundaryCount =
            argc >= 7
            ?
            static_cast<std::size_t>(
                std::stoul(
                    argv[6]
                )
            )
            :
            24;


        const std::size_t fullValidationCandidateCount =
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


        applications::ConvApplication
            application;


        const auto candidates =
            buildCandidates();


        analysis::DctMultiStateJointSearchOptions
            options;


        // 关键：留空。
        //
        // 这样通用搜索器不会重新枚举 monitor/unit，
        // 而只使用每个 AttackStructureNode 已固定的
        // node + monitor + unit，再搜索 interval。
        options.redistributionUnits.clear();

        options.monitorSignals.clear();


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
            << "Conv Stage2 fixed-hardware screening\n"
            << "====================================\n"
            << "Kernel: [-1 -1 -1; -1 8 -1; -1 -1 -1]\n"
            << "Stage2 images: 10\n"
            << "Candidate hardware structures: "
            << candidates.size()
            << "\n"
            << "Fixed per candidate: Node + Monitor + Unit\n"
            << "Searched per image: Interval only\n"
            << "Constraint: Global >= 30 dB and DeltaMSE_ROI > DeltaMSE_NonROI\n"
            << "Objective: minimize ROI PSNR\n"
            << "Boundary count: "
            << candidateBoundaryCount
            << "\n"
            << "Representatives: "
            << representativeCount
            << "\n"
            << "Beam width: "
            << beamWidth
            << "\n"
            << "Full validations: "
            << fullValidationCandidateCount
            << "\n\n";


        for (const auto& candidate : candidates)
        {
            printStructure(
                candidate
            );

            std::cout
                << "\n";
        }


        std::vector<CandidateSummary>
            summaries;


        summaries.reserve(
            candidates.size()
        );


        for (const auto& candidate : candidates)
        {
            CandidateSummary
                summary;


            summary.name =
                candidate.name;


            summary.hardwareNodeCount =
                candidate.structure.size();


            std::cout
                << "\n========== Candidate "
                << candidate.name
                << " ==========\n";


            for (int imageIndex = 1;
                 imageIndex <= 10;
                 ++imageIndex)
            {
                const auto inputPath =
                    stage2ImagePath(
                        dataRoot,
                        imageIndex
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


                const auto result =
                    analysis::DctMultiStateJointSearch::
                        search(
                            application,
                            inputImage,
                            roiMask,
                            candidate.structure,
                            options
                        );


                ImageEvaluation
                    imageResult;


                imageResult.imageIndex =
                    imageIndex;


                imageResult.baselineMetrics =
                    result.initialMetrics;


                if (result.hasBestFeasibleState)
                {
                    imageResult.success =
                        true;


                    imageResult.finalMetrics =
                        result.bestFeasibleState.metrics;


                    imageResult.configuration =
                        result.bestFeasibleState.configuration;


                    imageResult.roiMseIncrease =
                        imageResult.finalMetrics.roiMse
                        -
                        imageResult.baselineMetrics.roiMse;


                    imageResult.nonRoiMseIncrease =
                        imageResult.finalMetrics.nonRoiMse
                        -
                        imageResult.baselineMetrics.nonRoiMse;


                    imageResult.redistributionAdvantage =
                        imageResult.roiMseIncrease
                        -
                        imageResult.nonRoiMseIncrease;


                    ++summary.successCount;


                    summary.meanGlobal +=
                        imageResult.finalMetrics.globalPsnr;


                    summary.meanRoi +=
                        imageResult.finalMetrics.roiPsnr;


                    summary.meanNonRoi +=
                        imageResult.finalMetrics.nonRoiPsnr;


                    summary.meanRedistributionAdvantage +=
                        imageResult.redistributionAdvantage;


                    summary.minimumGlobal =
                        std::min(
                            summary.minimumGlobal,
                            imageResult.finalMetrics.globalPsnr
                        );


                    summary.maximumRoi =
                        std::max(
                            summary.maximumRoi,
                            imageResult.finalMetrics.roiPsnr
                        );
                }


                summary.images.push_back(
                    imageResult
                );


                std::cout
                    << "Candidate "
                    << candidate.name
                    << " / image_"
                    << twoDigit(
                        imageIndex
                    )
                    << " / "
                    << (
                        imageResult.success
                        ?
                        "SUCCESS"
                        :
                        "FAIL"
                    );


                if (imageResult.success)
                {
                    std::cout
                        << std::fixed
                        << std::setprecision(
                            6
                        )
                        << " / G="
                        << imageResult.finalMetrics.globalPsnr
                        << " / ROI="
                        << imageResult.finalMetrics.roiPsnr
                        << " / NROI="
                        << imageResult.finalMetrics.nonRoiPsnr
                        << " / Adv="
                        << imageResult.redistributionAdvantage
                        << " / Active="
                        << imageResult.configuration.size();
                }


                std::cout
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


                summary.meanRedistributionAdvantage /=
                    denominator;
            }


            summaries.push_back(
                std::move(
                    summary
                )
            );
        }


        auto ranked =
            summaries;


        std::sort(
            ranked.begin(),
            ranked.end(),
            betterSummary
        );


        std::cout
            << "\n\nStage2 hardware screening summary\n"
            << "=================================\n"
            << std::left
            << std::setw(7)
            << "Rank"
            << std::setw(10)
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
             index < ranked.size();
             ++index)
        {
            const auto& summary =
                ranked[index];


            std::cout
                << std::left
                << std::setw(7)
                << (index + 1)
                << std::setw(10)
                << summary.name
                << std::setw(10)
                << summary.hardwareNodeCount
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
                    << summary.meanRedistributionAdvantage
                    << summary.minimumGlobal;
            }
            else
            {
                std::cout
                    << "no successful Stage2 images";
            }


            std::cout
                << "\n";
        }


        const auto detailCsvPath =
            outputDirectory
            /
            "conv_stage2_hardware_screening_detail.csv";


        const auto summaryCsvPath =
            outputDirectory
            /
            "conv_stage2_hardware_screening_summary.csv";


        std::ofstream detailCsv(
            detailCsvPath
        );


        std::ofstream summaryCsv(
            summaryCsvPath
        );


        if (
            !detailCsv.is_open()
            ||
            !summaryCsv.is_open()
        )
        {
            throw std::runtime_error(
                "Unable to create Stage2 hardware screening CSV files."
            );
        }


        detailCsv
            << "candidate,image_index,success,hardware_node_count,"
            << "active_node_count,baseline_global,baseline_roi,baseline_non_roi,"
            << "final_global,final_roi,final_non_roi,"
            << "roi_mse_increase,non_roi_mse_increase,redistribution_advantage\n";


        for (const auto& summary : summaries)
        {
            for (const auto& image : summary.images)
            {
                detailCsv
                    << summary.name
                    << ","
                    << image.imageIndex
                    << ","
                    << (
                        image.success
                        ?
                        1
                        :
                        0
                    )
                    << ","
                    << summary.hardwareNodeCount
                    << ","
                    << image.configuration.size()
                    << ","
                    << image.baselineMetrics.globalPsnr
                    << ","
                    << image.baselineMetrics.roiPsnr
                    << ","
                    << image.baselineMetrics.nonRoiPsnr
                    << ",";


                if (image.success)
                {
                    detailCsv
                        << image.finalMetrics.globalPsnr
                        << ","
                        << image.finalMetrics.roiPsnr
                        << ","
                        << image.finalMetrics.nonRoiPsnr
                        << ","
                        << image.roiMseIncrease
                        << ","
                        << image.nonRoiMseIncrease
                        << ","
                        << image.redistributionAdvantage;
                }


                detailCsv
                    << "\n";
            }
        }


        summaryCsv
            << "rank,candidate,hardware_node_count,success_count,success_rate,"
            << "mean_global,mean_roi,mean_non_roi,mean_redistribution_advantage,"
            << "min_global,max_roi\n";


        for (std::size_t index = 0;
             index < ranked.size();
             ++index)
        {
            const auto& summary =
                ranked[index];


            summaryCsv
                << (index + 1)
                << ","
                << summary.name
                << ","
                << summary.hardwareNodeCount
                << ","
                << summary.successCount
                << ","
                << (
                    static_cast<double>(
                        summary.successCount
                    )
                    /
                    10.0
                )
                << ",";


            if (summary.successCount > 0)
            {
                summaryCsv
                    << summary.meanGlobal
                    << ","
                    << summary.meanRoi
                    << ","
                    << summary.meanNonRoi
                    << ","
                    << summary.meanRedistributionAdvantage
                    << ","
                    << summary.minimumGlobal
                    << ","
                    << summary.maximumRoi;
            }


            summaryCsv
                << "\n";
        }


        std::cout
            << "\nDetail CSV: "
            << detailCsvPath.string()
            << "\n"
            << "Summary CSV: "
            << summaryCsvPath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Conv Stage2 hardware screening failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

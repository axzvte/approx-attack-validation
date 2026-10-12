#include "analysis/dct_multi_state_joint_search.hpp"

#include "applications/conv.hpp"
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


std::filesystem::path stage3ImagePath(
    const std::filesystem::path& dataRoot,
    int testIndex
)
{
    // 首选用户当前使用的连续编号 image_21 ~ image_30。
    const int globalIndex =
        testIndex + 20;


    const auto primary =
        dataRoot
        /
        "stage3"
        /
        "input"
        /
        (
            "image_"
            +
            twoDigit(
                globalIndex
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


    // 兼容 Stage3 单独从 image_01 ~ image_10 编号。
    const auto localNumbering =
        dataRoot
        /
        "stage3"
        /
        "input"
        /
        (
            "image_"
            +
            twoDigit(
                testIndex
            )
            +
            ".jpg"
        );


    if (
        std::filesystem::exists(
            localNumbering
        )
    )
    {
        return localNumbering;
    }


    throw std::runtime_error(
        "Missing Stage3 image. Tried: "
        +
        primary.string()
        +
        " and "
        +
        localNumbering.string()
    );
}


analysis::AttackStructure fixedHardware()
{
    // Stage2 最终选定的唯一固定硬件 H* = Candidate D。
    //
    // Stage3 中 Node / Monitor / Unit 全部禁止改变；
    // 每张新图片只允许重新搜索 interval。
    return
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
    };
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


        const auto structure =
            fixedHardware();


        analysis::DctMultiStateJointSearchOptions
            options;


        // 留空意味着 Node / Monitor / Unit 全部固定，
        // 搜索器只在每个固定节点上搜索 Interval。
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
            << "Conv Stage3 final fixed-hardware validation\n"
            << "===========================================\n"
            << "Kernel: [-1 -1 -1; -1 8 -1; -1 -1 -1]\n"
            << "Final hardware: H* = Candidate D\n"
            << "Stage3 images: image_21 ~ image_30\n"
            << "Fixed: Node + Monitor + Unit\n"
            << "Per-image search: Interval only\n"
            << "Constraint: Global >= 30 dB and DeltaMSE_ROI > DeltaMSE_NonROI\n"
            << "Objective: minimize ROI PSNR\n\n"
            << "Fixed hardware structure\n"
            << "------------------------\n";


        for (const auto& node : structure)
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


        const auto detailCsvPath =
            outputDirectory
            /
            "conv_stage3_final_validation_detail.csv";


        const auto intervalCsvPath =
            outputDirectory
            /
            "conv_stage3_final_intervals.csv";


        std::ofstream detailCsv(
            detailCsvPath
        );


        std::ofstream intervalCsv(
            intervalCsvPath
        );


        if (
            !detailCsv.is_open()
            ||
            !intervalCsv.is_open()
        )
        {
            throw std::runtime_error(
                "Unable to create Conv Stage3 output CSV files."
            );
        }


        detailCsv
            << "image_index,success,active_node_count,"
            << "baseline_global,baseline_roi,baseline_non_roi,"
            << "final_global,final_roi,final_non_roi,"
            << "roi_mse_increase,non_roi_mse_increase,"
            << "redistribution_advantage\n";


        intervalCsv
            << "image_index,node_id,node_name,monitor,unit,lower,upper\n";


        std::size_t successCount =
            0;


        double meanGlobal =
            0.0;


        double meanRoi =
            0.0;


        double meanNonRoi =
            0.0;


        double meanAdvantage =
            0.0;


        double minGlobal =
            std::numeric_limits<double>::infinity();


        double maxRoi =
            -std::numeric_limits<double>::infinity();


        for (int testIndex = 1;
             testIndex <= 10;
             ++testIndex)
        {
            const int imageIndex =
                testIndex + 20;


            const auto inputPath =
                stage3ImagePath(
                    dataRoot,
                    testIndex
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
                << "\n[stage3] image_"
                << twoDigit(
                    imageIndex
                )
                << " starting"
                << std::endl;


            const auto result =
                analysis::DctMultiStateJointSearch::
                    search(
                        application,
                        inputImage,
                        roiMask,
                        structure,
                        options,

                        [imageIndex](
                            const analysis::DctMultiStateJointSearchProgress& progress
                        )
                        {
                            std::cout
                                << "\r[stage3] image "
                                << imageIndex
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
                    << "[stage3] image_"
                    << twoDigit(
                        imageIndex
                    )
                    << " FAIL: no feasible interval configuration\n";


                detailCsv
                    << imageIndex
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


            ++successCount;


            meanGlobal +=
                best.metrics.globalPsnr;


            meanRoi +=
                best.metrics.roiPsnr;


            meanNonRoi +=
                best.metrics.nonRoiPsnr;


            meanAdvantage +=
                advantage;


            minGlobal =
                std::min(
                    minGlobal,
                    best.metrics.globalPsnr
                );


            maxRoi =
                std::max(
                    maxRoi,
                    best.metrics.roiPsnr
                );


            std::cout
                << std::fixed
                << std::setprecision(
                    6
                )
                << "[stage3] image_"
                << twoDigit(
                    imageIndex
                )
                << " SUCCESS"
                << " / G="
                << best.metrics.globalPsnr
                << " / ROI="
                << best.metrics.roiPsnr
                << " / NROI="
                << best.metrics.nonRoiPsnr
                << " / ROI_dMSE="
                << roiMseIncrease
                << " / NROI_dMSE="
                << nonRoiMseIncrease
                << " / Adv="
                << advantage
                << " / Active="
                << best.configuration.size()
                << "\n";


            for (const auto& config : best.configuration)
            {
                std::cout
                    << "    Node "
                    << config.nodeId
                    << " / "
                    << applications::ConvApplication::nodeName(
                        config.nodeId
                    )
                    << " / "
                    << monitorName(
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


                intervalCsv
                    << imageIndex
                    << ","
                    << config.nodeId
                    << ","
                    << applications::ConvApplication::nodeName(
                        config.nodeId
                    )
                    << ","
                    << monitorName(
                        config.monitorInput
                    )
                    << ","
                    << unitName(
                        config.unit
                    )
                    << ","
                    << config.lower
                    << ","
                    << config.upper
                    << "\n";
            }


            detailCsv
                << imageIndex
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


        std::cout
            << "\n\nConv Stage3 final validation summary\n"
            << "====================================\n"
            << "Fixed hardware: H* = D\n"
            << "Success: "
            << successCount
            << "/10\n";


        if (successCount > 0)
        {
            const double denominator =
                static_cast<double>(
                    successCount
                );


            meanGlobal /=
                denominator;


            meanRoi /=
                denominator;


            meanNonRoi /=
                denominator;


            meanAdvantage /=
                denominator;


            std::cout
                << std::fixed
                << std::setprecision(
                    6
                )
                << "Mean Global PSNR: "
                << meanGlobal
                << "\n"
                << "Mean ROI PSNR: "
                << meanRoi
                << "\n"
                << "Mean Non-ROI PSNR: "
                << meanNonRoi
                << "\n"
                << "Mean Redistribution Advantage: "
                << meanAdvantage
                << "\n"
                << "Min Global PSNR: "
                << minGlobal
                << "\n"
                << "Max ROI PSNR: "
                << maxRoi
                << "\n";
        }


        std::cout
            << "\nDetail CSV: "
            << detailCsvPath.string()
            << "\n"
            << "Interval CSV: "
            << intervalCsvPath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Conv Stage3 final validation failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

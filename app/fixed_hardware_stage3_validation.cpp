#include "analysis/dct_multi_state_joint_search.hpp"

#include "applications/dct.hpp"
#include "applications/sobel.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>


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


std::filesystem::path stage3ImagePath(
    const std::filesystem::path& dataRoot,
    int testIndex
)
{
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


    const auto fallback =
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
            fallback
        )
    )
    {
        return fallback;
    }


    throw std::runtime_error(
        "Missing Stage3 image."
    );
}


analysis::AttackStructure dctHardware()
{
    // 旧 ROI 下 Stage2 选出的 DCT H* = C。
    // 本实验保持 Node / Monitor / Unit 完全不变，
    // 只在收缩后的 ROI 下重新搜索 Interval。
    return
    {
        {
            1,
            approximate::ApproxUnitId::Add12se5PD,
            core::MonitorSignal::Input2
        },
        {
            5,
            approximate::ApproxUnitId::Add12se5Z0,
            core::MonitorSignal::Input1
        },
        {
            7,
            approximate::ApproxUnitId::Add12se5Z0,
            core::MonitorSignal::Input2
        },
        {
            0,
            approximate::ApproxUnitId::Add12se5SB,
            core::MonitorSignal::Input2
        },
        {
            2,
            approximate::ApproxUnitId::Add12se5L8,
            core::MonitorSignal::Input1
        }
    };
}


analysis::AttackStructure sobelHardware()
{
    // 旧 ROI 下 Stage2 选出的 Sobel H* = B。
    // 本实验保持 Node / Monitor / Unit 完全不变，
    // 只在收缩后的 ROI 下重新搜索 Interval。
    return
    {
        {
            5,
            approximate::ApproxUnitId::Add12se5SB,
            core::MonitorSignal::Input1
        },
        {
            3,
            approximate::ApproxUnitId::Add12se5PD,
            core::MonitorSignal::Input1
        },
        {
            8,
            approximate::ApproxUnitId::Add12se5SB,
            core::MonitorSignal::Input1
        },
        {
            1,
            approximate::ApproxUnitId::Add12se5RP,
            core::MonitorSignal::BaselineOutput
        },
        {
            6,
            approximate::ApproxUnitId::Add12se5RP,
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
        if (argc < 2)
        {
            throw std::runtime_error(
                "Usage: fixed_hardware_stage3_validation <dct|sobel> [data_root] [representatives] [beam] [refinement] [boundaries] [full_validation]"
            );
        }


        const std::string appName =
            argv[1];


        const std::filesystem::path dataRoot =
            argc >= 3
            ?
            std::filesystem::path(
                argv[2]
            )
            :
            std::filesystem::path(
                "data"
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


        std::unique_ptr<core::Application>
            application;


        analysis::AttackStructure
            structure;


        std::string fixedName;


        if (appName == "dct")
        {
            application =
                std::make_unique<
                    applications::DctApplication
                >();


            structure =
                dctHardware();


            fixedName =
                "old H* = C";
        }
        else if (appName == "sobel")
        {
            application =
                std::make_unique<
                    applications::SobelApplication
                >();


            structure =
                sobelHardware();


            fixedName =
                "old H* = B";
        }
        else
        {
            throw std::runtime_error(
                "Unsupported application: "
                +
                appName
            );
        }


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


        analysis::DctMultiStateJointSearchOptions
            options;


        // 固定 Node / Monitor / Unit，只搜索 Interval。
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


        options.requireAllStructureNodesActive =
            true;


        options.globalPsnrThreshold =
            30.0;


        std::cout
            << appName
            << " Stage3 old-H* + new-ROI validation\n"
            << "========================================\n"
            << "Fixed hardware: "
            << fixedName
            << "\n"
            << "Stage3 images: image_21 ~ image_30\n"
            << "Only Interval is re-searched.\n\n";


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
                << "\n["
                << appName
                << "-new-roi] image_"
                << twoDigit(
                    imageIndex
                )
                << " starting"
                << std::endl;


            const auto result =
                analysis::DctMultiStateJointSearch::
                    search(
                        *application,
                        inputImage,
                        roiMask,
                        structure,
                        options,

                        [&appName, imageIndex](
                            const analysis::DctMultiStateJointSearchProgress& progress
                        )
                        {
                            std::cout
                                << "\r["
                                << appName
                                << "-new-roi] image "
                                << imageIndex
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
                    << appName
                    << "-new-roi] FAIL\n";


                continue;
            }


            const auto& baseline =
                result.initialMetrics;


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
                << "["
                << appName
                << "-new-roi] SUCCESS"
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
        }


        std::cout
            << "\n\n"
            << appName
            << " Stage3 old-H* + new-ROI summary\n"
            << "========================================\n"
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


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Fixed-hardware Stage3 validation failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

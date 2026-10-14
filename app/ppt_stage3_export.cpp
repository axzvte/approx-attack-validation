#include "analysis/dct_multi_state_joint_search.hpp"

#include "applications/conv.hpp"
#include "applications/dct.hpp"
#include "applications/sharpen.hpp"
#include "applications/sobel.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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
    const int imageIndex =
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
        "Missing Stage3 input image."
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


analysis::AttackStructure fixedHardware(
    const std::string& appName
)
{
    if (appName == "conv")
    {
        // Conv: previously selected H* = D.
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


    if (appName == "sharpen")
    {
        // Sharpening: latest formal Stage2 selected H* = C.
        return
        {
            {
                0,
                approximate::ApproxUnitId::Add12se5PN,
                core::MonitorSignal::Input1
            },
            {
                1,
                approximate::ApproxUnitId::Add12se5QT,
                core::MonitorSignal::Input1
            },
            {
                2,
                approximate::ApproxUnitId::Add12se5QT,
                core::MonitorSignal::Input1
            },
            {
                3,
                approximate::ApproxUnitId::Add12se5SB,
                core::MonitorSignal::Input2
            }
        };
    }


    if (appName == "dct")
    {
        // DCT: previously selected H* = C.
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


    if (appName == "sobel")
    {
        // Sobel: previously selected H* = B.
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


    throw std::runtime_error(
        "Unsupported application for PPT export: "
        +
        appName
    );
}


std::unique_ptr<core::Application>
makeApplication(
    const std::string& appName
)
{
    if (appName == "conv")
    {
        return
            std::make_unique<
                applications::ConvApplication
            >();
    }


    if (appName == "sharpen")
    {
        return
            std::make_unique<
                applications::SharpenApplication
            >();
    }


    if (appName == "dct")
    {
        return
            std::make_unique<
                applications::DctApplication
            >();
    }


    if (appName == "sobel")
    {
        return
            std::make_unique<
                applications::SobelApplication
            >();
    }


    throw std::runtime_error(
        "Unsupported application: "
        +
        appName
    );
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
                "Usage: ppt_stage3_export <conv|sharpen|dct|sobel> "
                "[data_root] [output_dir] [representatives] [beam] "
                "[refinement] [boundaries] [full_validation]"
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


        const std::filesystem::path outputDirectory =
            argc >= 4
            ?
            std::filesystem::path(
                argv[3]
            )
            :
            std::filesystem::path(
                "ppt_results"
            )
            /
            appName;


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


        const auto inputDirectory =
            outputDirectory
            /
            "input";


        const auto exactDirectory =
            outputDirectory
            /
            "exact";


        const auto baselineDirectory =
            outputDirectory
            /
            "baseline";


        const auto finalDirectory =
            outputDirectory
            /
            "final";


        const auto roiDirectory =
            outputDirectory
            /
            "roi";


        std::filesystem::create_directories(
            inputDirectory
        );

        std::filesystem::create_directories(
            exactDirectory
        );

        std::filesystem::create_directories(
            baselineDirectory
        );

        std::filesystem::create_directories(
            finalDirectory
        );

        std::filesystem::create_directories(
            roiDirectory
        );


        auto application =
            makeApplication(
                appName
            );


        const auto structure =
            fixedHardware(
                appName
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

        options.requireAllStructureNodesActive =
            true;


        options.globalPsnrThreshold =
            30.0;


        const auto metricsPath =
            outputDirectory
            /
            "metrics.csv";


        const auto intervalsPath =
            outputDirectory
            /
            "intervals.csv";


        std::ofstream metricsCsv(
            metricsPath
        );


        std::ofstream intervalsCsv(
            intervalsPath
        );


        if (
            !metricsCsv.is_open()
            ||
            !intervalsCsv.is_open()
        )
        {
            throw std::runtime_error(
                "Unable to create PPT export CSV files."
            );
        }


        metricsCsv
            << "image_index,success,"
            << "baseline_global,baseline_roi,baseline_non_roi,"
            << "final_global,final_roi,final_non_roi,"
            << "roi_mse_increase,non_roi_mse_increase,"
            << "redistribution_advantage\n";


        intervalsCsv
            << "image_index,node_id,node_name,monitor,unit,lower,upper\n";


        std::cout
            << "PPT Stage3 export: "
            << appName
            << "\n"
            << "========================================\n"
            << "Stage3 images: image_21 ~ image_30\n"
            << "Fixed hardware nodes: "
            << structure.size()
            << "\n"
            << "Output directory: "
            << outputDirectory.string()
            << "\n\n";


        for (const auto& node : structure)
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


            const cv::Mat exactImage =
                application->runExact(
                    inputImage
                );


            const cv::Mat baselineImage =
                application->runApprox(
                    inputImage,
                    {}
                );


            const std::string imageName =
                "image_"
                +
                twoDigit(
                    imageIndex
                )
                +
                ".png";


            image_io::saveImage(
                (
                    inputDirectory
                    /
                    imageName
                ).string(),
                inputImage
            );


            image_io::saveImage(
                (
                    exactDirectory
                    /
                    imageName
                ).string(),
                exactImage
            );


            image_io::saveImage(
                (
                    baselineDirectory
                    /
                    imageName
                ).string(),
                baselineImage
            );


            image_io::saveImage(
                (
                    roiDirectory
                    /
                    imageName
                ).string(),
                roiMask
            );


            std::cout
                << "\n["
                << appName
                << "-ppt] image_"
                << twoDigit(
                    imageIndex
                )
                << " searching intervals"
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
                                << "-ppt] image "
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


            const auto& baselineMetrics =
                result.initialMetrics;


            if (!result.hasBestFeasibleState)
            {
                metricsCsv
                    << imageIndex
                    << ",0,"
                    << baselineMetrics.globalPsnr
                    << ","
                    << baselineMetrics.roiPsnr
                    << ","
                    << baselineMetrics.nonRoiPsnr
                    << ",,,,,,\n";


                std::cout
                    << "["
                    << appName
                    << "-ppt] image_"
                    << twoDigit(
                        imageIndex
                    )
                    << " FAIL\n";


                continue;
            }


            const auto& best =
                result.bestFeasibleState;


            const cv::Mat finalImage =
                application->runApprox(
                    inputImage,
                    best.configuration
                );


            image_io::saveImage(
                (
                    finalDirectory
                    /
                    imageName
                ).string(),
                finalImage
            );


            const double roiMseIncrease =
                best.metrics.roiMse
                -
                baselineMetrics.roiMse;


            const double nonRoiMseIncrease =
                best.metrics.nonRoiMse
                -
                baselineMetrics.nonRoiMse;


            const double advantage =
                roiMseIncrease
                -
                nonRoiMseIncrease;


            metricsCsv
                << imageIndex
                << ",1,"
                << baselineMetrics.globalPsnr
                << ","
                << baselineMetrics.roiPsnr
                << ","
                << baselineMetrics.nonRoiPsnr
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


            for (const auto& config : best.configuration)
            {
                intervalsCsv
                    << imageIndex
                    << ","
                    << config.nodeId
                    << ","
                    << nodeName(
                        *application,
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


            std::cout
                << std::fixed
                << std::setprecision(
                    6
                )
                << "["
                << appName
                << "-ppt] SUCCESS"
                << " / Global="
                << best.metrics.globalPsnr
                << " / ROI="
                << best.metrics.roiPsnr
                << " / NonROI="
                << best.metrics.nonRoiPsnr
                << " / Adv="
                << advantage
                << "\n";
        }


        std::cout
            << "\nPPT export finished: "
            << outputDirectory.string()
            << "\n"
            << "Final images: "
            << finalDirectory.string()
            << "\n"
            << "Metrics: "
            << metricsPath.string()
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "PPT Stage3 export failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

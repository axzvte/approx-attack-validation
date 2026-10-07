#include "analysis/dct_multi_node_interval_optimizer.hpp"

#include "applications/dct.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
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


std::string phaseName(
    bool compensationPhase
)
{
    return
        compensationPhase
        ?
        "comp"
        :
        "attack";
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


        applications::DctApplication
            application;


        // 这里只做“多节点区间优化器”的第一次联调。
        //
        // Node 6 + 5Z0：前面实验中表现出较强 ROI 攻击能力。
        // Node 20 + 5QC：用于测试第二个节点是否能在当前真实输入下
        // 自动找到补偿或其它有价值的区间。
        //
        // 这不是最终硬件配置。
        const analysis::AttackStructure
            structure =
        {
            {
                6,
                approximate::ApproxUnitId::Add12se5Z0,
                core::MonitorInput::Input1
            },
            {
                20,
                approximate::ApproxUnitId::Add12se5QC,
                core::MonitorInput::Input2
            }
        };


        analysis::DctMultiNodeIntervalOptimizerOptions
            options;


        options.candidatesPerRole =
            3;


        options.maxAttackRounds =
            2;


        options.globalPsnrThreshold =
            30.0;


        const auto result =
            analysis::
                DctMultiNodeIntervalOptimizer::
                    optimize(
                        application,
                        inputImage,
                        roiMask,
                        structure,
                        options
                    );


        std::cout
            << "DCT multi-node interval optimization\n"
            << "====================================\n"
            << "Image: image_"
            << twoDigit(
                imageIndex
            )
            << ".jpg\n"
            << "Structure under test:\n"
            << "  Node 6  / input1 / 5Z0\n"
            << "  Node 20 / input2 / 5QC\n"
            << "Global PSNR threshold: 30 dB\n\n"
            << std::fixed
            << std::setprecision(
                6
            )
            << "Initial Global PSNR: "
            << result.initialMetrics.globalPsnr
            << "\n"
            << "Initial ROI PSNR: "
            << result.initialMetrics.roiPsnr
            << "\n"
            << "Initial Non-ROI PSNR: "
            << result.initialMetrics.nonRoiPsnr
            << "\n\n";


        std::cout
            << std::left
            << std::setw(8)
            << "Phase"
            << std::setw(8)
            << "Round"
            << std::setw(8)
            << "Node"
            << std::setw(10)
            << "Changed"
            << std::setw(10)
            << "Lower"
            << std::setw(10)
            << "Upper"
            << std::setw(15)
            << "BeforeGlobal"
            << std::setw(14)
            << "AfterGlobal"
            << std::setw(14)
            << "BeforeROI"
            << "AfterROI"
            << "\n";


        for (const auto& step : result.steps)
        {
            std::cout
                << std::left
                << std::setw(8)
                << phaseName(
                    step.compensationPhase
                )
                << std::setw(8)
                << step.round
                << std::setw(8)
                << step.nodeId
                << std::setw(10)
                << (
                    step.changed
                    ?
                    "yes"
                    :
                    "no"
                );


            if (
                step.changed
                ||
                step.hadPreviousInterval
            )
            {
                std::cout
                    << std::setw(10)
                    << step.selectedInterval.lower
                    << std::setw(10)
                    << step.selectedInterval.upper;
            }
            else
            {
                std::cout
                    << std::setw(10)
                    << "-"
                    << std::setw(10)
                    << "-";
            }


            std::cout
                << std::setw(15)
                << step.beforeMetrics.globalPsnr
                << std::setw(14)
                << step.afterMetrics.globalPsnr
                << std::setw(14)
                << step.beforeMetrics.roiPsnr
                << step.afterMetrics.roiPsnr
                << "\n";
        }


        std::cout
            << "\nFinal active intervals:\n";


        if (result.configuration.empty())
        {
            std::cout
                << "  none\n";
        }
        else
        {
            for (const auto& config : result.configuration)
            {
                std::cout
                    << "  Node "
                    << config.nodeId
                    << " -> ["
                    << config.lower
                    << ", "
                    << config.upper
                    << "]\n";
            }
        }


        std::cout
            << "\nFinal Global PSNR: "
            << result.finalMetrics.globalPsnr
            << "\n"
            << "Final ROI PSNR: "
            << result.finalMetrics.roiPsnr
            << "\n"
            << "Final Non-ROI PSNR: "
            << result.finalMetrics.nonRoiPsnr
            << "\n"
            << "Completed attack rounds: "
            << result.completedAttackRounds
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "DCT multi-node interval optimization failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

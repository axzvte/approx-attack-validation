#include "analysis/dct_interval_fast_evaluator.hpp"

#include "applications/dct.hpp"
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


    return "UNKNOWN";
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


        std::vector<core::AddSample>
            samples;


        // 这里先用 Baseline 作为 Module 2-A 的第一轮验证。
        //
        // 后续 Module 2 正式实现时，会在“其他节点当前区间固定”
        // 的状态下调用 collectConfiguredAddSamples(...)
        // 重新获取同样格式的真实动态样本。
        application.collectBaselineAddSamples(
            inputImage,
            roiMask,
            samples
        );


        constexpr int nodeId =
            6;


        constexpr core::MonitorInput monitorInput =
            core::MonitorInput::Input1;


        constexpr approximate::ApproxUnitId attackUnit =
            approximate::ApproxUnitId::Add12se5L8;


        auto evaluation =
            analysis::
                DctIntervalFastEvaluator::
                    evaluateFromSamples(
                        samples,
                        nodeId,
                        monitorInput,
                        attackUnit
                    );


        // 这里只为了检查快速分数的分布，打印前 20 名。
        // 核心模块本身没有做 Top-K 删除。
        std::sort(
            evaluation.metrics.begin(),
            evaluation.metrics.end(),

            [](
                const auto& first,
                const auto& second
            )
            {
                if (
                    first.redistributionScore
                    !=
                    second.redistributionScore
                )
                {
                    return
                        first.redistributionScore
                        >
                        second.redistributionScore;
                }


                if (
                    first.roiErrorChange
                    !=
                    second.roiErrorChange
                )
                {
                    return
                        first.roiErrorChange
                        >
                        second.roiErrorChange;
                }


                return
                    first.nonRoiErrorChange
                    <
                    second.nonRoiErrorChange;
            }
        );


        const std::size_t showCount =
            std::min<std::size_t>(
                20,
                evaluation.metrics.size()
            );


        std::cout
            << "DCT fast interval evaluation (Module 2-A core)\n"
            << "==============================================\n"
            << "Image: image_"
            << twoDigit(
                imageIndex
            )
            << ".jpg\n"
            << "Node: "
            << nodeId
            << "\n"
            << "Monitor: input1\n"
            << "Attack unit: "
            << unitName(
                attackUnit
            )
            << "\n"
            << "Samples: "
            << evaluation.sampleCount
            << "\n"
            << "Distinct monitor values: "
            << evaluation.distinctMonitorValueCount
            << "\n"
            << "Intervals evaluated without full DCT: "
            << evaluation.metrics.size()
            << "\n\n";


        std::cout
            << std::left
            << std::setw(7)
            << "Rank"
            << std::setw(10)
            << "Lower"
            << std::setw(10)
            << "Upper"
            << std::setw(14)
            << "ROITrig"
            << std::setw(14)
            << "NonROITrig"
            << std::setw(18)
            << "ROIErrorChange"
            << std::setw(20)
            << "NonROIErrorChange"
            << "Redistribution"
            << "\n";


        std::cout
            << std::fixed
            << std::setprecision(6);


        for (std::size_t index = 0;
             index < showCount;
             ++index)
        {
            const auto& metric =
                evaluation.metrics[
                    index
                ];


            std::cout
                << std::left
                << std::setw(7)
                << (index + 1)
                << std::setw(10)
                << metric.interval.lower
                << std::setw(10)
                << metric.interval.upper
                << std::setw(14)
                << metric.roiTriggerRate
                << std::setw(14)
                << metric.nonRoiTriggerRate
                << std::setw(18)
                << metric.roiErrorChange
                << std::setw(20)
                << metric.nonRoiErrorChange
                << metric.redistributionScore
                << "\n";
        }


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "DCT fast interval evaluation failed: "
            << exception.what()
            << "\n";


        return 1;
    }
}

#include "analysis/dct_interval_fast_evaluator.hpp"

#include "applications/dct.hpp"
#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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


void printGroup(
    const std::string& title,
    const std::vector<analysis::DctIntervalFastMetric>& metrics
)
{
    std::cout
        << "\n"
        << title
        << "\n"
        << std::string(
            title.size(),
            '-'
        )
        << "\n";


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


    for (std::size_t index = 0;
         index < metrics.size();
         ++index)
    {
        const auto& metric =
            metrics[index];


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


        // 第一轮仍以 Baseline 作为当前状态。
        // 后续正式的多节点区间优化会改用
        // collectConfiguredAddSamples(...) 获取当前配置下的真实样本。
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


        const auto evaluation =
            analysis::
                DctIntervalFastEvaluator::
                    evaluateFromSamples(
                        samples,
                        nodeId,
                        monitorInput,
                        attackUnit
                    );


        constexpr std::size_t
            topCount =
                10;


        const auto roiAttack =
            analysis::
                DctIntervalFastEvaluator::
                    selectTopMetrics(
                        evaluation,
                        analysis::DctIntervalFastRanking::RoiAttack,
                        topCount
                    );


        const auto nonRoiCompensation =
            analysis::
                DctIntervalFastEvaluator::
                    selectTopMetrics(
                        evaluation,
                        analysis::DctIntervalFastRanking::NonRoiCompensation,
                        topCount
                    );


        const auto redistribution =
            analysis::
                DctIntervalFastEvaluator::
                    selectTopMetrics(
                        evaluation,
                        analysis::DctIntervalFastRanking::Redistribution,
                        topCount
                    );


        std::set<
            std::pair<int, int>
        >
            uniqueIntervals;


        const auto addToUnion =
            [&uniqueIntervals](
                const std::vector<
                    analysis::DctIntervalFastMetric
                >& metrics
            )
            {
                for (const auto& metric : metrics)
                {
                    uniqueIntervals.insert(
                        {
                            metric.interval.lower,
                            metric.interval.upper
                        }
                    );
                }
            };


        addToUnion(
            roiAttack
        );


        addToUnion(
            nonRoiCompensation
        );


        addToUnion(
            redistribution
        );


        std::cout
            << "DCT fast interval evaluation (Module 2-A)\n"
            << "=========================================\n"
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
            << "\n"
            << "Top intervals per ranking: "
            << topCount
            << "\n"
            << "Unique intervals in three-group union: "
            << uniqueIntervals.size()
            << "\n";


        std::cout
            << std::fixed
            << std::setprecision(6);


        printGroup(
            "A. ROI attack candidates (largest ROIErrorChange)",
            roiAttack
        );


        printGroup(
            "B. Non-ROI compensation candidates (smallest NonROIErrorChange)",
            nonRoiCompensation
        );


        printGroup(
            "C. Redistribution candidates (largest ROIErrorChange - NonROIErrorChange)",
            redistribution
        );


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

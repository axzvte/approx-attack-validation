#include "analysis/dct_interval_search_space_generator.hpp"

#include "applications/dct.hpp"
#include "approximate/evoapprox_adapter.hpp"

#include <opencv2/core.hpp>

#include <iostream>
#include <vector>


namespace
{

bool sameInterval(
    const analysis::TriggerInterval& interval,
    int lower,
    int upper
)
{
    return
        interval.lower == lower
        &&
        interval.upper == upper;
}


bool sameSearchSpace(
    const analysis::DctIntervalSearchSpace& first,
    const analysis::DctIntervalSearchSpace& second
)
{
    if (
        first.nodeId != second.nodeId
        ||
        first.monitorInput != second.monitorInput
        ||
        first.sampleCount != second.sampleCount
        ||
        first.observedValues != second.observedValues
        ||
        first.intervals.size() != second.intervals.size()
    )
    {
        return false;
    }


    for (std::size_t index = 0;
         index < first.intervals.size();
         ++index)
    {
        if (
            !sameInterval(
                first.intervals[index],
                second.intervals[index].lower,
                second.intervals[index].upper
            )
        )
        {
            return false;
        }
    }


    return true;
}

}


int main()
{
    // =====================================================
    // 纯区间构造
    // =====================================================

    const std::vector<int>
        rawValues =
    {
        5,
        1,
        3,
        1,
        5
    };


    const auto intervals =
        analysis::
            DctIntervalSearchSpaceGenerator::
                buildIntervals(
                    rawValues
                );


    if (
        analysis::
            DctIntervalSearchSpaceGenerator::
                countIntervals(
                    3
                )
        !=
        6
        ||
        intervals.size()
        !=
        6
    )
    {
        std::cerr
            << "Unexpected DCT interval count.\n";


        return 1;
    }


    if (
        !sameInterval(intervals[0], 1, 1)
        ||
        !sameInterval(intervals[1], 1, 3)
        ||
        !sameInterval(intervals[2], 1, 5)
        ||
        !sameInterval(intervals[3], 3, 3)
        ||
        !sameInterval(intervals[4], 3, 5)
        ||
        !sameInterval(intervals[5], 5, 5)
    )
    {
        std::cerr
            << "Generated DCT interval ordering/content is incorrect.\n";


        return 1;
    }


    // =====================================================
    // 样本驱动核心接口
    // =====================================================

    const std::vector<core::AddSample>
        syntheticSamples =
    {
        {
            6,
            5,
            100,
            0,
            0.0
        },
        {
            6,
            1,
            101,
            0,
            0.0
        },
        {
            6,
            3,
            102,
            0,
            0.0
        },
        {
            6,
            3,
            103,
            0,
            0.0
        }
    };


    const std::vector<
        analysis::DctIntervalSearchTarget
    >
        syntheticTargets =
    {
        {
            6,
            core::MonitorInput::Input1
        }
    };


    const auto syntheticSpaces =
        analysis::
            DctIntervalSearchSpaceGenerator::
                generateFromSamples(
                    syntheticSamples,
                    syntheticTargets
                );


    if (
        syntheticSpaces.size() != 1
        ||
        syntheticSpaces[0].sampleCount != 4
        ||
        syntheticSpaces[0].observedValues
            !=
            std::vector<int>(
                {
                    1,
                    3,
                    5
                }
            )
        ||
        syntheticSpaces[0].intervals.size() != 6
    )
    {
        std::cerr
            << "DCT sample-driven interval generation is incorrect.\n";


        return 1;
    }


    // =====================================================
    // DCT 当前状态采样接口
    // =====================================================

    applications::DctApplication
        application;


    cv::Mat inputImage(
        16,
        16,
        CV_8UC1
    );


    for (int row = 0;
         row < inputImage.rows;
         ++row)
    {
        for (int col = 0;
             col < inputImage.cols;
             ++col)
        {
            inputImage.at<unsigned char>(
                row,
                col
            ) =
                static_cast<unsigned char>(
                    70
                    +
                    3 * row
                    +
                    2 * col
                );
        }
    }


    cv::Mat roiMask =
        cv::Mat::zeros(
            inputImage.size(),
            CV_8UC1
        );


    for (int row = 0;
         row < roiMask.rows;
         ++row)
    {
        for (int col = 0;
             col < roiMask.cols / 2;
             ++col)
        {
            roiMask.at<unsigned char>(
                row,
                col
            ) =
                255;
        }
    }


    const std::vector<
        analysis::DctIntervalSearchTarget
    >
        targets =
    {
        {
            20,
            core::MonitorInput::Input2
        }
    };


    const auto baselineSpaces =
        analysis::
            DctIntervalSearchSpaceGenerator::
                generateBaseline(
                    application,
                    inputImage,
                    roiMask,
                    targets
                );


    const std::vector<core::AttackConfig>
        emptyConfiguration;


    const auto emptyConfiguredSpaces =
        analysis::
            DctIntervalSearchSpaceGenerator::
                generateForCurrentConfiguration(
                    application,
                    inputImage,
                    roiMask,
                    emptyConfiguration,
                    targets
                );


    if (
        baselineSpaces.size() != 1
        ||
        emptyConfiguredSpaces.size() != 1
        ||
        !sameSearchSpace(
            baselineSpaces[0],
            emptyConfiguredSpaces[0]
        )
    )
    {
        std::cerr
            << "Baseline and empty current-configuration interval spaces differ.\n";


        return 1;
    }


    // 非空当前配置：
    // 使用上游 Node 0 全范围触发一个近似单元，
    // 然后在这一真实运行状态下重新采集 Node 20 输入。
    //
    // 不强制要求输入集合一定改变，因为具体变化取决于
    // 图像与近似单元；这里只验证这条动态采样路径合法可用。
    const std::vector<core::AttackConfig>
        currentConfiguration =
    {
        {
            0,
            approximate::ApproxUnitId::Add12se5L8,
            core::MonitorInput::Input1,
            -2048,
            2047
        }
    };


    const auto configuredSpaces =
        analysis::
            DctIntervalSearchSpaceGenerator::
                generateForCurrentConfiguration(
                    application,
                    inputImage,
                    roiMask,
                    currentConfiguration,
                    targets
                );


    if (
        configuredSpaces.size() != 1
        ||
        configuredSpaces[0].sampleCount == 0
        ||
        configuredSpaces[0].observedValues.empty()
        ||
        configuredSpaces[0].intervals.empty()
    )
    {
        std::cerr
            << "Current-configuration interval generation failed.\n";


        return 1;
    }


    std::cout
        << "DCT interval search-space generator test passed.\n";


    return 0;
}

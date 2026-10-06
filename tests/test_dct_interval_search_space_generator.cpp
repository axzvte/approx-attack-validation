#include "analysis/dct_interval_search_space_generator.hpp"

#include "applications/dct.hpp"

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

}


int main()
{
    // =====================================================
    // 纯区间构造测试
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
    )
    {
        std::cerr
            << "Unexpected DCT interval count.\n";


        return 1;
    }


    if (intervals.size() != 6)
    {
        std::cerr
            << "Unexpected generated DCT interval count.\n";


        return 1;
    }


    const bool intervalsAreExpected =
        sameInterval(
            intervals[0],
            1,
            1
        )
        &&
        sameInterval(
            intervals[1],
            1,
            3
        )
        &&
        sameInterval(
            intervals[2],
            1,
            5
        )
        &&
        sameInterval(
            intervals[3],
            3,
            3
        )
        &&
        sameInterval(
            intervals[4],
            3,
            5
        )
        &&
        sameInterval(
            intervals[5],
            5,
            5
        );


    if (!intervalsAreExpected)
    {
        std::cerr
            << "Generated DCT interval ordering/content is incorrect.\n";


        return 1;
    }


    // =====================================================
    // DCT Baseline 集成测试
    // =====================================================

    applications::DctApplication
        application;


    cv::Mat inputImage(
        8,
        8,
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
                    100
                    +
                    2 * row
                    +
                    col
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
            0,
            core::MonitorInput::Input1
        },
        {
            6,
            core::MonitorInput::Input2
        }
    };


    const auto spaces =
        analysis::
            DctIntervalSearchSpaceGenerator::
                generate(
                    application,
                    inputImage,
                    roiMask,
                    targets
                );


    if (spaces.size() != targets.size())
    {
        std::cerr
            << "Unexpected DCT interval search-space count.\n";


        return 1;
    }


    for (std::size_t index = 0;
         index < spaces.size();
         ++index)
    {
        const auto& space =
            spaces[index];


        if (
            space.nodeId
            !=
            targets[index].nodeId
            ||
            space.monitorInput
            !=
            targets[index].monitorInput
        )
        {
            std::cerr
                << "DCT interval search-space target mismatch.\n";


            return 1;
        }


        if (
            space.sampleCount == 0
            ||
            space.observedValues.empty()
            ||
            space.intervals.empty()
        )
        {
            std::cerr
                << "DCT interval search space is unexpectedly empty.\n";


            return 1;
        }


        const std::uint64_t expectedIntervalCount =
            analysis::
                DctIntervalSearchSpaceGenerator::
                    countIntervals(
                        space.observedValues.size()
                    );


        if (
            space.intervals.size()
            !=
            expectedIntervalCount
        )
        {
            std::cerr
                << "DCT interval search-space size does not match n(n+1)/2.\n";


            return 1;
        }


        for (std::size_t valueIndex = 1;
             valueIndex < space.observedValues.size();
             ++valueIndex)
        {
            if (
                space.observedValues[
                    valueIndex - 1
                ]
                >=
                space.observedValues[
                    valueIndex
                ]
            )
            {
                std::cerr
                    << "DCT observed monitor values are not sorted/unique.\n";


                return 1;
            }
        }
    }


    std::cout
        << "DCT interval search-space generator test passed.\n";


    return 0;
}

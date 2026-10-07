#include "analysis/dct_interval_search_space_generator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>


namespace analysis
{

namespace
{

std::vector<int> sortedUniqueValues(
    const std::vector<int>& values
)
{
    std::vector<int>
        result =
            values;


    std::sort(
        result.begin(),
        result.end()
    );


    result.erase(
        std::unique(
            result.begin(),
            result.end()
        ),
        result.end()
    );


    return result;
}


std::vector<TriggerInterval>
buildFromSortedUniqueValues(
    const std::vector<int>& values
)
{
    if (values.empty())
    {
        return {};
    }


    const std::uint64_t intervalCount =
        DctIntervalSearchSpaceGenerator::
            countIntervals(
                values.size()
            );


    if (
        intervalCount
        >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )
    )
    {
        throw std::overflow_error(
            "DCT interval search space is too large for this platform."
        );
    }


    std::vector<TriggerInterval>
        intervals;


    intervals.reserve(
        static_cast<std::size_t>(
            intervalCount
        )
    );


    for (std::size_t lowerIndex = 0;
         lowerIndex < values.size();
         ++lowerIndex)
    {
        for (std::size_t upperIndex = lowerIndex;
             upperIndex < values.size();
             ++upperIndex)
        {
            intervals.push_back(
                {
                    values[lowerIndex],
                    values[upperIndex]
                }
            );
        }
    }


    return intervals;
}


int monitoredValue(
    const core::AddSample& sample,
    core::MonitorSignal monitorSignal
)
{
    switch (monitorSignal)
    {
        case core::MonitorSignal::Input1:
            return sample.input1;

        case core::MonitorSignal::Input2:
            return sample.input2;

        case core::MonitorSignal::BaselineOutput:
            return sample.baselineOutput;
    }

    throw std::runtime_error(
        "Unknown DCT monitor signal."
    );
}


void validateTargets(
    const std::vector<DctIntervalSearchTarget>& targets
)
{
    if (targets.empty())
    {
        throw std::runtime_error(
            "DCT interval search target list is empty."
        );
    }


    std::set<
        std::pair<int, int>
    >
        uniqueTargets;


    for (const auto& target : targets)
    {
        if (target.nodeId < 0)
        {
            throw std::runtime_error(
                "DCT interval search target contains a negative node ID."
            );
        }


        const std::pair<int, int>
            key =
            {
                target.nodeId,
                static_cast<int>(
                    target.monitorInput
                )
            };


        if (
            !uniqueTargets.insert(
                key
            ).second
        )
        {
            throw std::runtime_error(
                "Duplicate DCT interval search target."
            );
        }
    }
}


void validateTargetsAgainstApplication(
    const applications::DctApplication& application,
    const std::vector<DctIntervalSearchTarget>& targets
)
{
    validateTargets(
        targets
    );


    const auto& nodes =
        application.addNodes();


    for (const auto& target : targets)
    {
        const bool found =
            std::find_if(
                nodes.begin(),
                nodes.end(),

                [&target](
                    const core::AddNode& node
                )
                {
                    return
                        node.id
                        ==
                        target.nodeId;
                }
            )
            !=
            nodes.end();


        if (!found)
        {
            throw std::runtime_error(
                "DCT interval search target contains an invalid node ID."
            );
        }
    }
}

}


// =========================================================
// 区间数量
// =========================================================

std::uint64_t
DctIntervalSearchSpaceGenerator::countIntervals(
    std::size_t distinctValueCount
)
{
    if (distinctValueCount == 0)
    {
        return 0;
    }


    const std::uint64_t n =
        static_cast<std::uint64_t>(
            distinctValueCount
        );


    if (
        n
        ==
        std::numeric_limits<std::uint64_t>::max()
    )
    {
        throw std::overflow_error(
            "DCT interval-count addition overflow."
        );
    }


    std::uint64_t left =
        n;


    std::uint64_t right =
        n + 1;


    if ((left % 2) == 0)
    {
        left /=
            2;
    }
    else
    {
        right /=
            2;
    }


    if (
        left != 0
        &&
        right
        >
        std::numeric_limits<std::uint64_t>::max()
        /
        left
    )
    {
        throw std::overflow_error(
            "DCT interval-count multiplication overflow."
        );
    }


    return
        left
        *
        right;
}


// =========================================================
// 直接由观测值构造区间
// =========================================================

std::vector<TriggerInterval>
DctIntervalSearchSpaceGenerator::buildIntervals(
    const std::vector<int>& observedValues
)
{
    return
        buildFromSortedUniqueValues(
            sortedUniqueValues(
                observedValues
            )
        );
}


// =========================================================
// 模块 1 核心：由当前真实动态样本生成区间空间
// =========================================================

std::vector<DctIntervalSearchSpace>
DctIntervalSearchSpaceGenerator::generateFromSamples(
    const std::vector<core::AddSample>& samples,
    const std::vector<DctIntervalSearchTarget>& targets
)
{
    validateTargets(
        targets
    );


    if (samples.empty())
    {
        throw std::runtime_error(
            "DCT interval search received no dynamic samples."
        );
    }


    std::map<
        int,
        std::vector<std::size_t>
    >
        targetIndicesByNode;


    for (std::size_t targetIndex = 0;
         targetIndex < targets.size();
         ++targetIndex)
    {
        targetIndicesByNode[
            targets[
                targetIndex
            ].nodeId
        ].push_back(
            targetIndex
        );
    }


    std::vector<
        std::vector<int>
    >
        valuesByTarget(
            targets.size()
        );


    std::vector<std::size_t>
        sampleCounts(
            targets.size(),
            0
        );


    for (const auto& sample : samples)
    {
        const auto iterator =
            targetIndicesByNode.find(
                sample.nodeId
            );


        if (
            iterator
            ==
            targetIndicesByNode.end()
        )
        {
            continue;
        }


        for (
            const std::size_t targetIndex :
            iterator->second
        )
        {
            ++sampleCounts[
                targetIndex
            ];


            valuesByTarget[
                targetIndex
            ].push_back(
                monitoredValue(
                    sample,
                    targets[
                        targetIndex
                    ].monitorInput
                )
            );
        }
    }


    std::vector<DctIntervalSearchSpace>
        result;


    result.reserve(
        targets.size()
    );


    for (std::size_t targetIndex = 0;
         targetIndex < targets.size();
         ++targetIndex)
    {
        if (
            sampleCounts[
                targetIndex
            ]
            ==
            0
        )
        {
            throw std::runtime_error(
                "DCT interval search target has no samples in the current circuit state."
            );
        }


        DctIntervalSearchSpace
            searchSpace;


        searchSpace.nodeId =
            targets[
                targetIndex
            ].nodeId;


        searchSpace.monitorInput =
            targets[
                targetIndex
            ].monitorInput;


        searchSpace.sampleCount =
            sampleCounts[
                targetIndex
            ];


        searchSpace.observedValues =
            sortedUniqueValues(
                valuesByTarget[
                    targetIndex
                ]
            );


        searchSpace.intervals =
            buildFromSortedUniqueValues(
                searchSpace.observedValues
            );


        result.push_back(
            std::move(
                searchSpace
            )
        );
    }


    return result;
}


// =========================================================
// 当前攻击配置下重新采样，再生成区间空间
// =========================================================

std::vector<DctIntervalSearchSpace>
DctIntervalSearchSpaceGenerator::generateForCurrentConfiguration(
    const applications::DctApplication& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const std::vector<core::AttackConfig>& currentConfiguration,
    const std::vector<DctIntervalSearchTarget>& targets
)
{
    validateTargetsAgainstApplication(
        application,
        targets
    );


    std::vector<core::AddSample>
        samples;


    application.collectConfiguredAddSamples(
        inputImage,
        roiMask,
        currentConfiguration,
        samples
    );


    return
        generateFromSamples(
            samples,
            targets
        );
}


// =========================================================
// Baseline 初始化
// =========================================================

std::vector<DctIntervalSearchSpace>
DctIntervalSearchSpaceGenerator::generateBaseline(
    const applications::DctApplication& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const std::vector<DctIntervalSearchTarget>& targets
)
{
    const std::vector<core::AttackConfig>
        emptyConfiguration;


    return
        generateForCurrentConfiguration(
            application,
            inputImage,
            roiMask,
            emptyConfiguration,
            targets
        );
}


// =========================================================
// 旧接口：继续保持 Baseline 语义
// =========================================================

std::vector<DctIntervalSearchSpace>
DctIntervalSearchSpaceGenerator::generate(
    const applications::DctApplication& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
    const std::vector<DctIntervalSearchTarget>& targets
)
{
    return
        generateBaseline(
            application,
            inputImage,
            roiMask,
            targets
        );
}

}

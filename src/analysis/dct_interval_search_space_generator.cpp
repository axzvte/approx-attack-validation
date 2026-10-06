#include "analysis/dct_interval_search_space_generator.hpp"

#include "core/add_sample.hpp"

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


    // 每一对 i <= j 对应一个连续闭区间：
    //
    // [values[i], values[j]]
    //
    // 因为边界只取实际出现过的值，
    // 不会重复枚举那些触发集合完全相同的空档边界。
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


bool containsNodeId(
    const applications::DctApplication& application,
    int nodeId
)
{
    const auto& nodes =
        application.addNodes();


    return
        std::find_if(
            nodes.begin(),
            nodes.end(),

            [nodeId](
                const core::AddNode& node
            )
            {
                return
                    node.id
                    ==
                    nodeId;
            }
        )
        !=
        nodes.end();
}


int monitoredValue(
    const core::AddSample& sample,
    core::MonitorInput monitorInput
)
{
    return
        monitorInput
            ==
            core::MonitorInput::Input1
        ?
        sample.input1
        :
        sample.input2;
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


    if (
        distinctValueCount
        >
        static_cast<std::size_t>(
            std::numeric_limits<std::uint64_t>::max()
        )
    )
    {
        throw std::overflow_error(
            "Distinct DCT monitor-value count exceeds uint64_t."
        );
    }


    std::uint64_t left =
        static_cast<std::uint64_t>(
            distinctValueCount
        );


    std::uint64_t right =
        left
        +
        1;


    if (right == 0)
    {
        throw std::overflow_error(
            "DCT interval-count addition overflow."
        );
    }


    // n * (n + 1) / 2
    //
    // 先除以 2，降低乘法溢出的风险。
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
// 模块 1：一张图片上的区间搜索空间生成
// =========================================================

std::vector<DctIntervalSearchSpace>
DctIntervalSearchSpaceGenerator::generate(
    const applications::DctApplication& application,
    const cv::Mat& inputImage,
    const cv::Mat& roiMask,
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


    // node ID -> 当前节点对应的 target 下标。
    //
    // 通常一个节点只会有一个已经确定的 monitor input，
    // 但这里仍允许同一节点分别生成 input1 / input2，
    // 便于独立测试。
    std::map<
        int,
        std::vector<std::size_t>
    >
        targetIndicesByNode;


    for (std::size_t targetIndex = 0;
         targetIndex < targets.size();
         ++targetIndex)
    {
        const auto& target =
            targets[targetIndex];


        if (
            !containsNodeId(
                application,
                target.nodeId
            )
        )
        {
            throw std::runtime_error(
                "DCT interval search target contains an invalid node ID."
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


        targetIndicesByNode[
            target.nodeId
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


    std::vector<core::AddSample>
        samples;


    // 直接复用现有 Baseline 采样逻辑。
    //
    // roiMask 在模块 1 中不参与区间好坏判断，
    // 这里只是 collectBaselineAddSamples 的现有接口所需。
    application.collectBaselineAddSamples(
        inputImage,
        roiMask,
        samples
    );


    for (const auto& sample : samples)
    {
        const auto targetIterator =
            targetIndicesByNode.find(
                sample.nodeId
            );


        if (
            targetIterator
            ==
            targetIndicesByNode.end()
        )
        {
            continue;
        }


        for (
            const std::size_t targetIndex :
            targetIterator->second
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
                "DCT interval search target has no Baseline samples."
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

}

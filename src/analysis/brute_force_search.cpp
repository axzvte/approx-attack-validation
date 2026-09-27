#include "analysis/brute_force_search.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>


namespace analysis
{

namespace
{

std::uint64_t checkedAdd(
    std::uint64_t first,
    std::uint64_t second
)
{
    if (
        second
        >
        std::numeric_limits<std::uint64_t>::max()
        -
        first
    )
    {
        throw std::overflow_error(
            "Brute-force configuration count overflow."
        );
    }


    return
        first
        +
        second;
}


std::uint64_t checkedMultiply(
    std::uint64_t first,
    std::uint64_t second
)
{
    if (
        first != 0
        &&
        second
        >
        std::numeric_limits<std::uint64_t>::max()
        /
        first
    )
    {
        throw std::overflow_error(
            "Brute-force configuration count overflow."
        );
    }


    return
        first
        *
        second;
}


std::uint64_t nodeCandidateCount(
    const NodeSearchSpace& node
)
{
    std::uint64_t monitorIntervalCount =
        0;


    for (const auto& monitorSpace : node.monitorSpaces)
    {
        monitorIntervalCount =
            checkedAdd(
                monitorIntervalCount,
                static_cast<std::uint64_t>(
                    monitorSpace.intervals.size()
                )
            );
    }


    return
        checkedMultiply(
            static_cast<std::uint64_t>(
                node.attackUnits.size()
            ),
            monitorIntervalCount
        );
}


void enumerateConfigurationProduct(
    const BruteForceSearchSpace& searchSpace,
    const std::vector<std::size_t>& selectedNodeIndices,
    std::size_t selectedPosition,
    AttackConfiguration& currentConfiguration,
    const ConfigurationCallback& callback
)
{
    if (
        selectedPosition
        ==
        selectedNodeIndices.size()
    )
    {
        callback(
            currentConfiguration
        );

        return;
    }


    const NodeSearchSpace& node =
        searchSpace.nodes[
            selectedNodeIndices[
                selectedPosition
            ]
        ];


    for (
        approximate::ApproxUnitId unit :
        node.attackUnits
    )
    {
        for (
            const auto& monitorSpace :
            node.monitorSpaces
        )
        {
            for (
                const auto& interval :
                monitorSpace.intervals
            )
            {
                currentConfiguration.push_back(
                    {
                        node.nodeId,
                        unit,
                        monitorSpace.monitorInput,
                        interval.lower,
                        interval.upper
                    }
                );


                enumerateConfigurationProduct(
                    searchSpace,
                    selectedNodeIndices,
                    selectedPosition + 1,
                    currentConfiguration,
                    callback
                );


                currentConfiguration.pop_back();
            }
        }
    }
}


void enumerateNodeCombinations(
    const BruteForceSearchSpace& searchSpace,
    std::size_t targetNodeCount,
    std::size_t nextNodeIndex,
    std::vector<std::size_t>& selectedNodeIndices,
    AttackConfiguration& currentConfiguration,
    const ConfigurationCallback& callback
)
{
    if (
        selectedNodeIndices.size()
        ==
        targetNodeCount
    )
    {
        enumerateConfigurationProduct(
            searchSpace,
            selectedNodeIndices,
            0,
            currentConfiguration,
            callback
        );

        return;
    }


    const std::size_t remainingNeeded =
        targetNodeCount
        -
        selectedNodeIndices.size();


    if (
        searchSpace.nodes.size()
        -
        nextNodeIndex
        <
        remainingNeeded
    )
    {
        return;
    }


    const std::size_t lastPossibleIndex =
        searchSpace.nodes.size()
        -
        remainingNeeded;


    for (
        std::size_t nodeIndex =
            nextNodeIndex;

        nodeIndex
            <=
            lastPossibleIndex;

        ++nodeIndex
    )
    {
        selectedNodeIndices.push_back(
            nodeIndex
        );


        enumerateNodeCombinations(
            searchSpace,
            targetNodeCount,
            nodeIndex + 1,
            selectedNodeIndices,
            currentConfiguration,
            callback
        );


        selectedNodeIndices.pop_back();
    }
}

}


// =========================================================
// 搜索空间合法性检查
// =========================================================

void BruteForceSearch::validate(
    const BruteForceSearchSpace& searchSpace
)
{
    if (searchSpace.nodes.empty())
    {
        throw std::runtime_error(
            "Brute-force search space contains no nodes."
        );
    }


    if (searchSpace.minAttackNodes == 0)
    {
        throw std::runtime_error(
            "minAttackNodes must be at least 1."
        );
    }


    if (
        searchSpace.maxAttackNodes
        <
        searchSpace.minAttackNodes
    )
    {
        throw std::runtime_error(
            "maxAttackNodes is smaller than minAttackNodes."
        );
    }


    if (
        searchSpace.maxAttackNodes
        >
        searchSpace.nodes.size()
    )
    {
        throw std::runtime_error(
            "maxAttackNodes exceeds the number of candidate nodes."
        );
    }


    std::set<int>
        nodeIds;


    for (const auto& node : searchSpace.nodes)
    {
        if (node.nodeId < 0)
        {
            throw std::runtime_error(
                "Node ID must be non-negative."
            );
        }


        if (
            !nodeIds.insert(
                node.nodeId
            ).second
        )
        {
            throw std::runtime_error(
                "Duplicate node ID in brute-force search space."
            );
        }


        if (node.attackUnits.empty())
        {
            throw std::runtime_error(
                "A candidate node has no attack approximate units."
            );
        }


        std::set<int>
            unitIds;


        for (
            approximate::ApproxUnitId unit :
            node.attackUnits
        )
        {
            if (
                !unitIds.insert(
                    static_cast<int>(
                        unit
                    )
                ).second
            )
            {
                throw std::runtime_error(
                    "Duplicate attack approximate unit for a node."
                );
            }
        }


        if (node.monitorSpaces.empty())
        {
            throw std::runtime_error(
                "A candidate node has no monitor input search space."
            );
        }


        std::set<int>
            monitorInputs;


        for (
            const auto& monitorSpace :
            node.monitorSpaces
        )
        {
            if (
                !monitorInputs.insert(
                    static_cast<int>(
                        monitorSpace.monitorInput
                    )
                ).second
            )
            {
                throw std::runtime_error(
                    "Duplicate monitor input for a node."
                );
            }


            if (monitorSpace.intervals.empty())
            {
                throw std::runtime_error(
                    "A monitor input has no trigger intervals."
                );
            }


            std::set<std::pair<int, int>>
                intervals;


            for (
                const auto& interval :
                monitorSpace.intervals
            )
            {
                if (
                    interval.lower
                    >
                    interval.upper
                )
                {
                    throw std::runtime_error(
                        "Trigger interval lower is greater than upper."
                    );
                }


                if (
                    !intervals.insert(
                        {
                            interval.lower,
                            interval.upper
                        }
                    ).second
                )
                {
                    throw std::runtime_error(
                        "Duplicate trigger interval for a monitor input."
                    );
                }
            }
        }
    }
}


// =========================================================
// 搜索空间数量统计
// =========================================================

std::uint64_t BruteForceSearch::countConfigurations(
    const BruteForceSearchSpace& searchSpace
)
{
    validate(
        searchSpace
    );


    // dp[k]：
    // 从已经处理的节点中选择 k 个节点时，
    // 所有节点内部配置笛卡尔积的总数量。
    std::vector<std::uint64_t>
        dp(
            searchSpace.maxAttackNodes + 1,
            0
        );


    dp[0] =
        1;


    std::size_t processedNodes =
        0;


    for (const auto& node : searchSpace.nodes)
    {
        const std::uint64_t candidateCount =
            nodeCandidateCount(
                node
            );


        const std::size_t maximumK =
            std::min(
                searchSpace.maxAttackNodes,
                processedNodes + 1
            );


        for (
            std::size_t k = maximumK;
            k >= 1;
            --k
        )
        {
            const std::uint64_t added =
                checkedMultiply(
                    dp[k - 1],
                    candidateCount
                );


            dp[k] =
                checkedAdd(
                    dp[k],
                    added
                );
        }


        ++processedNodes;
    }


    std::uint64_t total =
        0;


    for (
        std::size_t k =
            searchSpace.minAttackNodes;

        k
            <=
            searchSpace.maxAttackNodes;

        ++k
    )
    {
        total =
            checkedAdd(
                total,
                dp[k]
            );
    }


    return total;
}


// =========================================================
// 完整暴力枚举
// =========================================================

void BruteForceSearch::enumerate(
    const BruteForceSearchSpace& searchSpace,
    const ConfigurationCallback& callback
)
{
    validate(
        searchSpace
    );


    if (!callback)
    {
        throw std::runtime_error(
            "Brute-force search callback is empty."
        );
    }


    std::vector<std::size_t>
        selectedNodeIndices;


    selectedNodeIndices.reserve(
        searchSpace.maxAttackNodes
    );


    AttackConfiguration
        currentConfiguration;


    currentConfiguration.reserve(
        searchSpace.maxAttackNodes
    );


    // 先枚举攻击节点数量：
    // 1 个节点、2 个节点、...、maxAttackNodes。
    //
    // 每一种节点组合内部，再对：
    // attack unit × monitor input × interval
    // 做完整笛卡尔积。
    for (
        std::size_t attackNodeCount =
            searchSpace.minAttackNodes;

        attackNodeCount
            <=
            searchSpace.maxAttackNodes;

        ++attackNodeCount
    )
    {
        enumerateNodeCombinations(
            searchSpace,
            attackNodeCount,
            0,
            selectedNodeIndices,
            currentConfiguration,
            callback
        );
    }
}

}

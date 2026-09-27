#include "analysis/two_stage_search.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>


namespace analysis
{

namespace
{

const NodeSearchSpace&
findNodeSearchSpace(
    const BruteForceSearchSpace& searchSpace,
    int nodeId
)
{
    const auto iterator =
        std::find_if(
            searchSpace.nodes.begin(),
            searchSpace.nodes.end(),

            [nodeId](
                const NodeSearchSpace& node
            )
            {
                return
                    node.nodeId
                    ==
                    nodeId;
            }
        );


    if (
        iterator
        ==
        searchSpace.nodes.end()
    )
    {
        throw std::runtime_error(
            "Selected Stage 2 node does not exist in Stage 1 search space."
        );
    }


    return
        *iterator;
}


const MonitorSearchSpace&
findMonitorSearchSpace(
    const NodeSearchSpace& node,
    core::MonitorInput monitorInput
)
{
    const auto iterator =
        std::find_if(
            node.monitorSpaces.begin(),
            node.monitorSpaces.end(),

            [monitorInput](
                const MonitorSearchSpace& monitorSpace
            )
            {
                return
                    monitorSpace.monitorInput
                    ==
                    monitorInput;
            }
        );


    if (
        iterator
        ==
        node.monitorSpaces.end()
    )
    {
        throw std::runtime_error(
            "Selected Stage 2 monitor input does not exist in Stage 1 search space."
        );
    }


    return
        *iterator;
}


bool containsUnit(
    const NodeSearchSpace& node,
    approximate::ApproxUnitId unit
)
{
    return
        std::find(
            node.attackUnits.begin(),
            node.attackUnits.end(),
            unit
        )
        !=
        node.attackUnits.end();
}

}


// =========================================================
// 数据集检查
// =========================================================

void TwoStageSearch::validateDataset(
    const TwoStageDataset& dataset,
    std::size_t expectedImagesPerStage
)
{
    if (expectedImagesPerStage == 0)
    {
        throw std::runtime_error(
            "Expected image count per stage must be greater than zero."
        );
    }


    if (
        dataset.stage1Images.size()
        !=
        expectedImagesPerStage
    )
    {
        throw std::runtime_error(
            "Stage 1 image count does not match the expected count."
        );
    }


    if (
        dataset.stage2Images.size()
        !=
        expectedImagesPerStage
    )
    {
        throw std::runtime_error(
            "Stage 2 image count does not match the expected count."
        );
    }


    if (dataset.roiMaskPath.empty())
    {
        throw std::runtime_error(
            "Shared ROI mask path is empty."
        );
    }


    std::set<std::string>
        allInputPaths;


    const auto validateImageCase =
        [&allInputPaths](
            const ImageCase& imageCase
        )
        {
            if (imageCase.inputPath.empty())
            {
                throw std::runtime_error(
                    "Input image path is empty."
                );
            }


            if (
                !allInputPaths.insert(
                    imageCase.inputPath
                ).second
            )
            {
                throw std::runtime_error(
                    "The same input image appears more than once across the two stages."
                );
            }
        };


    for (
        const auto& imageCase :
        dataset.stage1Images
    )
    {
        validateImageCase(
            imageCase
        );
    }


    for (
        const auto& imageCase :
        dataset.stage2Images
    )
    {
        validateImageCase(
            imageCase
        );
    }
}


// =========================================================
// Stage 1
// =========================================================

std::uint64_t
TwoStageSearch::countStage1Configurations(
    const BruteForceSearchSpace& stage1SearchSpace
)
{
    return
        BruteForceSearch::
            countConfigurations(
                stage1SearchSpace
            );
}


void TwoStageSearch::enumerateStage1(
    const BruteForceSearchSpace& stage1SearchSpace,
    const ConfigurationCallback& callback
)
{
    BruteForceSearch::enumerate(
        stage1SearchSpace,
        callback
    );
}


void TwoStageSearch::runStage1(
    const TwoStageDataset& dataset,
    const BruteForceSearchSpace& stage1SearchSpace,
    const StageImageCallback& callback
)
{
    validateDataset(
        dataset
    );


    if (!callback)
    {
        throw std::runtime_error(
            "Stage 1 image callback is empty."
        );
    }


    std::uint64_t configurationIndex =
        0;


    BruteForceSearch::enumerate(
        stage1SearchSpace,

        [&](
            const AttackConfiguration&
                configuration
        )
        {
            for (
                std::size_t imageIndex = 0;
                imageIndex
                    <
                    dataset.stage1Images.size();
                ++imageIndex
            )
            {
                callback(
                    configurationIndex,
                    configuration,
                    imageIndex,
                    dataset.stage1Images[
                        imageIndex
                    ]
                );
            }


            ++configurationIndex;
        }
    );
}


// =========================================================
// 从完整配置提取固定硬件结构
// =========================================================

AttackStructure
TwoStageSearch::extractStructure(
    const AttackConfiguration& configuration
)
{
    if (configuration.empty())
    {
        throw std::runtime_error(
            "Cannot extract an attack structure from an empty configuration."
        );
    }


    AttackStructure
        structure;


    structure.reserve(
        configuration.size()
    );


    std::set<int>
        nodeIds;


    for (
        const auto& config :
        configuration
    )
    {
        if (
            !nodeIds.insert(
                config.nodeId
            ).second
        )
        {
            throw std::runtime_error(
                "Duplicate node ID in attack configuration."
            );
        }


        structure.push_back(
            {
                config.nodeId,
                config.unit,
                config.monitorInput
            }
        );
    }


    return structure;
}


// =========================================================
// Stage 2 搜索空间
// =========================================================

BruteForceSearchSpace
TwoStageSearch::buildStage2SearchSpace(
    const BruteForceSearchSpace& stage1SearchSpace,
    const AttackStructure& structure
)
{
    // 先利用底层框架验证 Stage 1 搜索空间。
    //
    // countConfigurations 不会真正执行搜索，
    // 这里只用于保证原始搜索空间本身合法。
    BruteForceSearch::
        countConfigurations(
            stage1SearchSpace
        );


    if (structure.empty())
    {
        throw std::runtime_error(
            "Stage 2 attack structure is empty."
        );
    }


    if (
        structure.size()
        <
        stage1SearchSpace.minAttackNodes
        ||
        structure.size()
        >
        stage1SearchSpace.maxAttackNodes
    )
    {
        throw std::runtime_error(
            "Stage 2 structure node count is outside the Stage 1 search range."
        );
    }


    BruteForceSearchSpace
        stage2SearchSpace;


    stage2SearchSpace.minAttackNodes =
        structure.size();


    stage2SearchSpace.maxAttackNodes =
        structure.size();


    stage2SearchSpace.nodes.reserve(
        structure.size()
    );


    std::set<int>
        selectedNodeIds;


    for (
        const auto& selectedNode :
        structure
    )
    {
        if (
            !selectedNodeIds.insert(
                selectedNode.nodeId
            ).second
        )
        {
            throw std::runtime_error(
                "Duplicate node ID in Stage 2 attack structure."
            );
        }


        const NodeSearchSpace&
            stage1Node =
                findNodeSearchSpace(
                    stage1SearchSpace,
                    selectedNode.nodeId
                );


        if (
            !containsUnit(
                stage1Node,
                selectedNode.unit
            )
        )
        {
            throw std::runtime_error(
                "Selected Stage 2 attack unit does not exist in Stage 1 search space."
            );
        }


        const MonitorSearchSpace&
            stage1Monitor =
                findMonitorSearchSpace(
                    stage1Node,
                    selectedNode.monitorInput
                );


        NodeSearchSpace
            stage2Node;


        stage2Node.nodeId =
            selectedNode.nodeId;


        // Stage 2 固定攻击近似加法器
        stage2Node.attackUnits =
        {
            selectedNode.unit
        };


        // Stage 2 固定 monitor input，
        // 只保留该输入对应的全部区间候选。
        stage2Node.monitorSpaces =
        {
            {
                selectedNode.monitorInput,
                stage1Monitor.intervals
            }
        };


        stage2SearchSpace.nodes.push_back(
            std::move(
                stage2Node
            )
        );
    }


    // 再次检查过滤后的 Stage 2 搜索空间。
    BruteForceSearch::
        countConfigurations(
            stage2SearchSpace
        );


    return
        stage2SearchSpace;
}


std::uint64_t
TwoStageSearch::countStage2Configurations(
    const BruteForceSearchSpace& stage1SearchSpace,
    const AttackStructure& structure
)
{
    const BruteForceSearchSpace
        stage2SearchSpace =
            buildStage2SearchSpace(
                stage1SearchSpace,
                structure
            );


    return
        BruteForceSearch::
            countConfigurations(
                stage2SearchSpace
            );
}


void TwoStageSearch::enumerateStage2(
    const BruteForceSearchSpace& stage1SearchSpace,
    const AttackStructure& structure,
    const ConfigurationCallback& callback
)
{
    const BruteForceSearchSpace
        stage2SearchSpace =
            buildStage2SearchSpace(
                stage1SearchSpace,
                structure
            );


    BruteForceSearch::enumerate(
        stage2SearchSpace,
        callback
    );
}


void TwoStageSearch::runStage2(
    const TwoStageDataset& dataset,
    const BruteForceSearchSpace& stage1SearchSpace,
    const AttackStructure& structure,
    const StageImageCallback& callback
)
{
    validateDataset(
        dataset
    );


    if (!callback)
    {
        throw std::runtime_error(
            "Stage 2 image callback is empty."
        );
    }


    const BruteForceSearchSpace
        stage2SearchSpace =
            buildStage2SearchSpace(
                stage1SearchSpace,
                structure
            );


    std::uint64_t configurationIndex =
        0;


    BruteForceSearch::enumerate(
        stage2SearchSpace,

        [&](
            const AttackConfiguration&
                configuration
        )
        {
            for (
                std::size_t imageIndex = 0;
                imageIndex
                    <
                    dataset.stage2Images.size();
                ++imageIndex
            )
            {
                callback(
                    configurationIndex,
                    configuration,
                    imageIndex,
                    dataset.stage2Images[
                        imageIndex
                    ]
                );
            }


            ++configurationIndex;
        }
    );
}

}

#include "core/dfg_graph.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>


namespace core
{

void DfgGraph::addNode(
    const DfgNode& node
)
{
    if (containsNode(node.id))
    {
        throw std::runtime_error(
            "DFG contains duplicate node ID."
        );
    }


    nodes_.push_back(
        node
    );
}


void DfgGraph::addEdge(
    const DfgEdge& edge
)
{
    edges_.push_back(
        edge
    );
}


void DfgGraph::addOutputNode(
    int nodeId
)
{
    if (
        std::find(
            outputNodeIds_.begin(),
            outputNodeIds_.end(),
            nodeId
        )
        !=
        outputNodeIds_.end()
    )
    {
        throw std::runtime_error(
            "DFG contains duplicate output node."
        );
    }


    outputNodeIds_.push_back(
        nodeId
    );
}


const std::vector<DfgNode>&
DfgGraph::nodes() const
{
    return nodes_;
}


const std::vector<DfgEdge>&
DfgGraph::edges() const
{
    return edges_;
}


const std::vector<int>&
DfgGraph::outputNodeIds() const
{
    return outputNodeIds_;
}


bool DfgGraph::containsNode(
    int nodeId
) const
{
    for (const auto& node : nodes_)
    {
        if (node.id == nodeId)
        {
            return true;
        }
    }


    return false;
}


const DfgNode&
DfgGraph::nodeById(
    int nodeId
) const
{
    for (const auto& node : nodes_)
    {
        if (node.id == nodeId)
        {
            return node;
        }
    }


    throw std::runtime_error(
        "DFG node does not exist."
    );
}


std::vector<int>
DfgGraph::approximationCandidateIds() const
{
    std::vector<int>
        result;


    for (const auto& node : nodes_)
    {
        if (node.approximationCandidate)
        {
            result.push_back(
                node.id
            );
        }
    }


    return result;
}


void DfgGraph::validate() const
{
    if (nodes_.empty())
    {
        throw std::runtime_error(
            "DFG contains no nodes."
        );
    }


    if (outputNodeIds_.empty())
    {
        throw std::runtime_error(
            "DFG contains no output nodes."
        );
    }


    // =====================================================
    // 建立 ID -> index
    // =====================================================

    std::unordered_map<int, std::size_t>
        nodeIndex;


    for (std::size_t i = 0;
         i < nodes_.size();
         ++i)
    {
        const auto inserted =
            nodeIndex.emplace(
                nodes_[i].id,
                i
            );


        if (!inserted.second)
        {
            throw std::runtime_error(
                "DFG contains duplicate node ID."
            );
        }


        if (
            nodes_[i].approximationCandidate
            &&
            nodes_[i].operation
                != DfgOperation::Add
            &&
            nodes_[i].operation
                != DfgOperation::Subtract
        )
        {
            throw std::runtime_error(
                "Only ADD/SUB nodes can currently "
                "be approximation candidates."
            );
        }


        if (
            nodes_[i].operation
                == DfgOperation::MultiplyConstant
            &&
            !std::isfinite(
                nodes_[i].constant
            )
        )
        {
            throw std::runtime_error(
                "MultiplyConstant contains "
                "an invalid constant."
            );
        }
    }


    // =====================================================
    // 检查输出节点
    // =====================================================

    for (const int nodeId : outputNodeIds_)
    {
        if (
            nodeIndex.find(nodeId)
            ==
            nodeIndex.end()
        )
        {
            throw std::runtime_error(
                "DFG output node does not exist."
            );
        }
    }


    // =====================================================
    // 检查边
    // =====================================================

    std::vector<int>
        indegree(
            nodes_.size(),
            0
        );


    std::vector<
        std::vector<std::size_t>
    >
        adjacency(
            nodes_.size()
        );


    // 用于检查：
    //
    // 同一个目标节点的同一个 inputIndex
    // 不能被连接两次。
    std::set<
        std::pair<int, int>
    >
        usedInputSlots;


    for (const auto& edge : edges_)
    {
        const auto fromIterator =
            nodeIndex.find(
                edge.fromNodeId
            );


        const auto toIterator =
            nodeIndex.find(
                edge.toNodeId
            );


        if (
            fromIterator == nodeIndex.end()
            ||
            toIterator == nodeIndex.end()
        )
        {
            throw std::runtime_error(
                "DFG edge references unknown node."
            );
        }


        if (edge.inputIndex < 0)
        {
            throw std::runtime_error(
                "DFG edge contains invalid input index."
            );
        }


        const auto slot =
            std::make_pair(
                edge.toNodeId,
                edge.inputIndex
            );


        if (
            !usedInputSlots.insert(
                slot
            ).second
        )
        {
            throw std::runtime_error(
                "DFG target input is connected more than once."
            );
        }


        const std::size_t fromIndex =
            fromIterator->second;


        const std::size_t toIndex =
            toIterator->second;


        adjacency[fromIndex].push_back(
            toIndex
        );


        ++indegree[toIndex];
    }


    // =====================================================
    // 检查每种节点的输入数量
    // =====================================================

    std::vector<int>
        inputCounts(
            nodes_.size(),
            0
        );


    for (const auto& edge : edges_)
    {
        const std::size_t index =
            nodeIndex.at(
                edge.toNodeId
            );


        ++inputCounts[index];
    }


    for (std::size_t i = 0;
         i < nodes_.size();
         ++i)
    {
        int expectedInputs =
            -1;


        switch (nodes_[i].operation)
        {
            case DfgOperation::Input:
                expectedInputs = 0;
                break;

            case DfgOperation::Add:
            case DfgOperation::Subtract:
            case DfgOperation::Multiply:
            case DfgOperation::Max:
            case DfgOperation::Min:
                expectedInputs = 2;
                break;

            case DfgOperation::MultiplyConstant:
            case DfgOperation::Relu:
            case DfgOperation::Clip:
                expectedInputs = 1;
                break;
        }


        if (
            expectedInputs >= 0
            &&
            inputCounts[i] != expectedInputs
        )
        {
            throw std::runtime_error(
                "DFG node has incorrect number of inputs."
            );
        }
    }


    // =====================================================
    // 拓扑排序
    //
    // DFG 必须是有向无环图。
    // =====================================================

    std::queue<std::size_t>
        ready;


    for (std::size_t i = 0;
         i < indegree.size();
         ++i)
    {
        if (indegree[i] == 0)
        {
            ready.push(i);
        }
    }


    std::size_t visitedCount =
        0;


    while (!ready.empty())
    {
        const std::size_t current =
            ready.front();

        ready.pop();


        ++visitedCount;


        for (const std::size_t target
             : adjacency[current])
        {
            --indegree[target];


            if (indegree[target] == 0)
            {
                ready.push(
                    target
                );
            }
        }
    }


    if (
        visitedCount
        !=
        nodes_.size()
    )
    {
        throw std::runtime_error(
            "DFG contains a cycle."
        );
    }
}

}
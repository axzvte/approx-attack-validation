#include "analysis/propagation_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <vector>


namespace analysis
{

namespace
{

// =========================================================
// 根据目标节点的运算类型，计算一条输入边上的
// 误差传播系数。
// =========================================================

double propagationGain(
    const core::DfgNode& targetNode,
    int inputIndex
)
{
    switch (targetNode.operation)
    {
        // -------------------------------------------------
        // y = a + b
        //
        // δy = δa + δb
        // -------------------------------------------------

        case core::DfgOperation::Add:
        {
            return 1.0;
        }


        // -------------------------------------------------
        // y = a - b
        //
        // δy = δa - δb
        // -------------------------------------------------

        case core::DfgOperation::Subtract:
        {
            if (inputIndex == 0)
            {
                return 1.0;
            }


            if (inputIndex == 1)
            {
                return -1.0;
            }


            throw std::runtime_error(
                "Subtract node contains invalid input index."
            );
        }


        // -------------------------------------------------
        // y = c * a
        //
        // δy = c * δa
        // -------------------------------------------------

        case core::DfgOperation::MultiplyConstant:
        {
            if (inputIndex != 0)
            {
                throw std::runtime_error(
                    "MultiplyConstant node contains "
                    "invalid input index."
                );
            }


            return targetNode.constant;
        }


        // -------------------------------------------------
        // Input 节点理论上不应该有输入边
        // -------------------------------------------------

        case core::DfgOperation::Input:
        {
            throw std::runtime_error(
                "Input node cannot receive a propagation edge."
            );
        }


        // -------------------------------------------------
        // 以下节点的传播系数依赖真实运行数据。
        //
        // 第一阶段的纯结构分析暂时不处理。
        // -------------------------------------------------

        case core::DfgOperation::Multiply:
        case core::DfgOperation::Relu:
        case core::DfgOperation::Max:
        case core::DfgOperation::Min:
        case core::DfgOperation::Clip:
        {
            throw std::runtime_error(
                "Propagation reaches a data-dependent operation."
            );
        }
    }


    throw std::runtime_error(
        "Unknown DFG operation."
    );
}

}


// =========================================================
// 分析单个节点
// =========================================================

PropagationResult
PropagationAnalyzer::analyze(
    const core::DfgGraph& graph,
    int sourceNodeId
) const
{
    // 先保证 DFG 本身合法
    graph.validate();


    if (!graph.containsNode(sourceNodeId))
    {
        throw std::runtime_error(
            "Propagation source node does not exist."
        );
    }


    // =====================================================
    // 建立：
    //
    // nodeId -> 数组下标
    // =====================================================

    std::unordered_map<int, std::size_t>
        nodeIndex;


    for (std::size_t i = 0;
         i < graph.nodes().size();
         ++i)
    {
        nodeIndex[
            graph.nodes()[i].id
        ] =
            i;
    }


    // =====================================================
    // 建立拓扑排序所需的数据
    // =====================================================

    struct OutgoingEdge
    {
        std::size_t targetIndex;

        int inputIndex;
    };


    std::vector<
        std::vector<OutgoingEdge>
    >
        adjacency(
            graph.nodes().size()
        );


    std::vector<int>
        indegree(
            graph.nodes().size(),
            0
        );


    for (const auto& edge
         : graph.edges())
    {
        const std::size_t fromIndex =
            nodeIndex.at(
                edge.fromNodeId
            );


        const std::size_t toIndex =
            nodeIndex.at(
                edge.toNodeId
            );


        adjacency[fromIndex].push_back(
            {
                toIndex,
                edge.inputIndex
            }
        );


        ++indegree[toIndex];
    }


    // =====================================================
    // 拓扑排序
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


    std::vector<std::size_t>
        topologicalOrder;


    topologicalOrder.reserve(
        graph.nodes().size()
    );


    while (!ready.empty())
    {
        const std::size_t current =
            ready.front();

        ready.pop();


        topologicalOrder.push_back(
            current
        );


        for (const auto& edge
             : adjacency[current])
        {
            --indegree[
                edge.targetIndex
            ];


            if (
                indegree[
                    edge.targetIndex
                ]
                ==
                0
            )
            {
                ready.push(
                    edge.targetIndex
                );
            }
        }
    }


    // graph.validate() 已经检查过环，
    // 这里理论上不会失败。
    if (
        topologicalOrder.size()
        !=
        graph.nodes().size()
    )
    {
        throw std::runtime_error(
            "Unable to obtain DFG topological order."
        );
    }


    // =====================================================
    // 开始传播误差
    //
    // 每个节点保存的是：
    //
    // δnode / δsource
    //
    // 对源节点定义：
    //
    // δsource / δsource = 1
    // =====================================================

    std::vector<double>
        nodeErrors(
            graph.nodes().size(),
            0.0
        );


    const std::size_t sourceIndex =
        nodeIndex.at(
            sourceNodeId
        );


    nodeErrors[sourceIndex] =
        1.0;


    // =====================================================
    // 按拓扑顺序向后传播
    // =====================================================

    for (const std::size_t currentIndex
         : topologicalOrder)
    {
        const double currentError =
            nodeErrors[
                currentIndex
            ];


        // 当前节点完全没受到源误差影响，
        // 就不需要继续传播。
        if (
            std::abs(currentError)
            <=
            kEpsilon
        )
        {
            continue;
        }


        for (const auto& edge
             : adjacency[currentIndex])
        {
            const core::DfgNode& targetNode =
                graph.nodes()[
                    edge.targetIndex
                ];


            const double gain =
                propagationGain(
                    targetNode,
                    edge.inputIndex
                );


            nodeErrors[
                edge.targetIndex
            ] +=
                currentError
                *
                gain;
        }
    }


    // =====================================================
    // 收集最终输出
    // =====================================================

    PropagationResult result;

    result.sourceNodeId =
        sourceNodeId;


    result.outputGains.reserve(
        graph.outputNodeIds().size()
    );


    double squaredSum =
        0.0;


    for (const int outputNodeId
         : graph.outputNodeIds())
    {
        const std::size_t outputIndex =
            nodeIndex.at(
                outputNodeId
            );


        const double outputGain =
            nodeErrors[
                outputIndex
            ];


        result.outputGains.push_back(
            outputGain
        );


        const double absoluteGain =
            std::abs(
                outputGain
            );


        result.l1Gain +=
            absoluteGain;


        squaredSum +=
            outputGain
            *
            outputGain;


        result.maxAbsGain =
            std::max(
                result.maxAbsGain,
                absoluteGain
            );


        if (
            absoluteGain
            >
            kEpsilon
        )
        {
            ++result.affectedOutputCount;
        }
    }


    result.l2Gain =
        std::sqrt(
            squaredSum
        );


    return result;
}


// =========================================================
// 批量分析
// =========================================================

std::vector<PropagationResult>
PropagationAnalyzer::analyzeAll(
    const core::DfgGraph& graph,
    const std::vector<int>& sourceNodeIds
) const
{
    std::vector<PropagationResult>
        results;


    results.reserve(
        sourceNodeIds.size()
    );


    for (const int sourceNodeId
         : sourceNodeIds)
    {
        results.push_back(
            analyze(
                graph,
                sourceNodeId
            )
        );
    }


    return results;
}

}
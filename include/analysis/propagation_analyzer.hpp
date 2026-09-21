#pragma once

#include "core/dfg_graph.hpp"

#include <cstddef>
#include <vector>


namespace analysis
{

// =========================================================
// 单个节点的结构误差传播结果
// =========================================================

struct PropagationResult
{
    // 当前被分析的源节点
    int sourceNodeId = -1;


    // 按 DfgGraph::outputNodeIds() 的顺序，
    // 保存误差传播到每个最终输出后的增益。
    //
    // 例如：
    //
    // outputGains = {2.0, -1.0}
    //
    // 表示：
    //
    // 源节点误差 δ
    //
    // Output_0 =  2δ
    // Output_1 = -1δ
    std::vector<double>
        outputGains;


    // Σ |Gij|
    double l1Gain = 0.0;


    // sqrt(Σ Gij^2)
    double l2Gain = 0.0;


    // max |Gij|
    double maxAbsGain = 0.0;


    // 有多少最终输出真正受到影响
    std::size_t affectedOutputCount = 0;
};


// =========================================================
// DFG 节点结构误差传播分析器
// =========================================================

class PropagationAnalyzer
{
public:

    // -----------------------------------------------------
    // 对一个节点进行分析
    //
    // 在 sourceNodeId 的输出端定义：
    //
    // δsource = 1
    //
    // 这里的 1 是归一化单位误差，
    // 目的是求：
    //
    // δoutput = G * δsource
    //
    // 因此最终得到的 outputGains 就是 G。
    // -----------------------------------------------------

    PropagationResult analyze(
        const core::DfgGraph& graph,
        int sourceNodeId
    ) const;


    // 后续一次分析全部候选节点时使用
    std::vector<PropagationResult> analyzeAll(
        const core::DfgGraph& graph,
        const std::vector<int>& sourceNodeIds
    ) const;


private:

    static constexpr double kEpsilon =
        1e-12;
};

}
#include "applications/dct8_dfg_builder.hpp"

#include "analysis/propagation_analyzer.hpp"
#include "core/dfg_graph.hpp"

#include <cmath>
#include <iostream>
#include <vector>


namespace
{

bool nearlyEqual(
    double a,
    double b,
    double tolerance = 1e-9
)
{
    return
        std::abs(a - b)
        <=
        tolerance;
}

}


int main()
{
    // =====================================================
    // 构建 DCT 的通用 DFG
    // =====================================================

    applications::Dct8DfgBuilder
        builder;


    const core::DfgGraph graph =
        builder.build();


    graph.validate();


    // =====================================================
    // 1. 检查节点总数
    //
    // 8  个 Input
    // 32 个 ADD/SUB
    // 32 个 MultiplyConstant
    //
    // 总共 72 个
    // =====================================================

    if (graph.nodes().size() != 72)
    {
        std::cerr
            << "Expected 72 DFG nodes, got "
            << graph.nodes().size()
            << ".\n";

        return 1;
    }


    // =====================================================
    // 2. 检查边数量
    //
    // 第一层 ADD/SUB：
    // 8 × 2 = 16
    //
    // ADD/SUB → multiplier：
    // 32
    //
    // 每个 DCT 输出有3级累加：
    // 6条边 × 8 = 48
    //
    // 总计：
    //
    // 16 + 32 + 48 = 96
    // =====================================================

    if (graph.edges().size() != 96)
    {
        std::cerr
            << "Expected 96 DFG edges, got "
            << graph.edges().size()
            << ".\n";

        return 1;
    }


    // =====================================================
    // 3. 检查32个候选近似节点
    // =====================================================

    const auto candidates =
        graph.approximationCandidateIds();


    if (candidates.size() != 32)
    {
        std::cerr
            << "Expected 32 approximation candidates, got "
            << candidates.size()
            << ".\n";

        return 1;
    }


    for (int i = 0; i < 32; ++i)
    {
        if (candidates[i] != i)
        {
            std::cerr
                << "Unexpected candidate node ID at index "
                << i
                << ".\n";

            return 1;
        }
    }


    // =====================================================
    // 4. 检查第一层节点类型
    //
    // ADD_00 ~ ADD_03:
    //     Add
    //
    // ADD_04 ~ ADD_07:
    //     实际含义是差分，因此映射成 Subtract
    // =====================================================

    for (int nodeId = 0;
         nodeId < 4;
         ++nodeId)
    {
        if (
            graph.nodeById(nodeId).operation
            !=
            core::DfgOperation::Add
        )
        {
            std::cerr
                << "Node "
                << nodeId
                << " should be ADD.\n";

            return 1;
        }
    }


    for (int nodeId = 4;
         nodeId < 8;
         ++nodeId)
    {
        if (
            graph.nodeById(nodeId).operation
            !=
            core::DfgOperation::Subtract
        )
        {
            std::cerr
                << "Node "
                << nodeId
                << " should be SUBTRACT.\n";

            return 1;
        }
    }


    // =====================================================
    // 5. 检查一个乘法节点
    //
    // multiplier ID:
    //
    // 2000 + 4*k + n
    //
    // 2000：
    //
    // X0 的第一个常数乘法
    //
    // Q15 coefficient = 11585
    //
    // 实际传播系数：
    //
    // 11585 / 32768
    // =====================================================

    const auto& multiplier0 =
        graph.nodeById(
            2000
        );


    if (
        multiplier0.operation
        !=
        core::DfgOperation::MultiplyConstant
    )
    {
        std::cerr
            << "Node 2000 should be MultiplyConstant.\n";

        return 1;
    }


    const double expectedCoefficient =
        11585.0
        /
        32768.0;


    if (
        !nearlyEqual(
            multiplier0.constant,
            expectedCoefficient
        )
    )
    {
        std::cerr
            << "Incorrect DCT multiplier coefficient.\n";

        return 1;
    }


    // =====================================================
    // 6. 检查8个输出节点
    //
    // 每个输出使用3个累加ADD：
    //
    // X0 -> ADD_10
    // X1 -> ADD_13
    // X2 -> ADD_16
    // ...
    // X7 -> ADD_31
    // =====================================================

    const std::vector<int>
        expectedOutputs =
    {
        10,
        13,
        16,
        19,
        22,
        25,
        28,
        31
    };


    if (
        graph.outputNodeIds()
        !=
        expectedOutputs
    )
    {
        std::cerr
            << "DCT output node mapping is incorrect.\n";

        return 1;
    }


    // =====================================================
    // 7. 用真正的 PropagationAnalyzer 做一次检查
    //
    // ADD_00 = S0
    //
    // S0 只参与偶数频率：
    //
    // X0
    // X2
    // X4
    // X6
    //
    // 因此它应该影响4个最终输出。
    // =====================================================

    analysis::PropagationAnalyzer
        analyzer;


    const auto result =
        analyzer.analyze(
            graph,
            0
        );


    if (result.outputGains.size() != 8)
    {
        std::cerr
            << "Expected 8 DCT output gains.\n";

        return 1;
    }


    if (result.affectedOutputCount != 4)
    {
        std::cerr
            << "ADD_00 should affect 4 DCT outputs, got "
            << result.affectedOutputCount
            << ".\n";

        return 1;
    }


    // =====================================================
    // ADD_00 对偶数输出的传播增益
    //
    // 对应各行第一个DCT系数：
    //
    // X0: 11585
    // X2: 15137
    // X4: 11585
    // X6:  6270
    // =====================================================

    const double scale =
        32768.0;


    if (
        !nearlyEqual(
            result.outputGains[0],
            11585.0 / scale
        )
        ||
        !nearlyEqual(
            result.outputGains[1],
            0.0
        )
        ||
        !nearlyEqual(
            result.outputGains[2],
            15137.0 / scale
        )
        ||
        !nearlyEqual(
            result.outputGains[3],
            0.0
        )
        ||
        !nearlyEqual(
            result.outputGains[4],
            11585.0 / scale
        )
        ||
        !nearlyEqual(
            result.outputGains[5],
            0.0
        )
        ||
        !nearlyEqual(
            result.outputGains[6],
            6270.0 / scale
        )
        ||
        !nearlyEqual(
            result.outputGains[7],
            0.0
        )
    )
    {
        std::cerr
            << "ADD_00 propagation mapping is incorrect.\n";

        return 1;
    }


    std::cout
        << "DCT DFG nodes: "
        << graph.nodes().size()
        << "\n";


    std::cout
        << "DCT DFG edges: "
        << graph.edges().size()
        << "\n";


    std::cout
        << "Approximation candidates: "
        << candidates.size()
        << "\n";


    std::cout
        << "ADD_00 affected outputs: "
        << result.affectedOutputCount
        << "\n";


    std::cout
        << "Dct8DfgBuilder test passed.\n";


    return 0;
}
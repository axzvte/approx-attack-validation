#include "analysis/propagation_analyzer.hpp"
#include "core/dfg_graph.hpp"

#include <cmath>
#include <iostream>


namespace
{

bool nearlyEqual(
    double a,
    double b,
    double tolerance = 1e-12
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
    // 构造一个简单 DFG
    //
    //
    // A ----\
    //        ADD_LEFT ---- ×2 ----\
    // B ----/                      \
    //                               SUB ----> Output
    // C ----\                      /
    //        ADD_RIGHT -----------/
    // D ----/
    //
    //
    // SUB:
    //
    // output =
    //     input0 - input1
    //
    //
    // 因此：
    //
    // ADD_LEFT 产生单位误差：
    //
    //     1
    //      ↓ ×2
    //     2
    //      ↓ SUB input0
    //     +2
    //
    //
    // ADD_RIGHT 产生单位误差：
    //
    //     1
    //      ↓ SUB input1
    //     -1
    //
    //
    // SUB 自己产生单位误差：
    //
    //     +1
    //
    // =====================================================

    core::DfgGraph graph;


    graph.addNode(
        {
            0,
            "INPUT_A",
            core::DfgOperation::Input,
            0.0,
            false
        }
    );


    graph.addNode(
        {
            1,
            "INPUT_B",
            core::DfgOperation::Input,
            0.0,
            false
        }
    );


    graph.addNode(
        {
            2,
            "INPUT_C",
            core::DfgOperation::Input,
            0.0,
            false
        }
    );


    graph.addNode(
        {
            3,
            "INPUT_D",
            core::DfgOperation::Input,
            0.0,
            false
        }
    );


    graph.addNode(
        {
            4,
            "ADD_LEFT",
            core::DfgOperation::Add,
            0.0,
            true
        }
    );


    graph.addNode(
        {
            5,
            "ADD_RIGHT",
            core::DfgOperation::Add,
            0.0,
            true
        }
    );


    graph.addNode(
        {
            6,
            "MUL_CONST",
            core::DfgOperation::MultiplyConstant,
            2.0,
            false
        }
    );


    graph.addNode(
        {
            7,
            "SUB",
            core::DfgOperation::Subtract,
            0.0,
            true
        }
    );


    // =====================================================
    // 输入 → 两个 ADD
    // =====================================================

    graph.addEdge(
        {
            0,
            4,
            0
        }
    );


    graph.addEdge(
        {
            1,
            4,
            1
        }
    );


    graph.addEdge(
        {
            2,
            5,
            0
        }
    );


    graph.addEdge(
        {
            3,
            5,
            1
        }
    );


    // =====================================================
    // ADD_LEFT → ×2 → SUB input0
    // =====================================================

    graph.addEdge(
        {
            4,
            6,
            0
        }
    );


    graph.addEdge(
        {
            6,
            7,
            0
        }
    );


    // =====================================================
    // ADD_RIGHT → SUB input1
    // =====================================================

    graph.addEdge(
        {
            5,
            7,
            1
        }
    );


    graph.addOutputNode(
        7
    );


    graph.validate();


    analysis::PropagationAnalyzer
        analyzer;


    // =====================================================
    // 1. 分析 ADD_LEFT
    //
    // 期望传播增益：
    //
    // 1 × 2 = 2
    // =====================================================

    const auto leftResult =
        analyzer.analyze(
            graph,
            4
        );


    if (
        leftResult.outputGains.size()
        !=
        1
    )
    {
        std::cerr
            << "Unexpected number of outputs.\n";

        return 1;
    }


    if (
        !nearlyEqual(
            leftResult.outputGains[0],
            2.0
        )
    )
    {
        std::cerr
            << "ADD_LEFT propagation is incorrect. "
            << "Expected 2.0, got "
            << leftResult.outputGains[0]
            << ".\n";

        return 1;
    }


    // =====================================================
    // 2. 分析 ADD_RIGHT
    //
    // 它进入 SUB 的第二个输入：
    //
    // output = input0 - input1
    //
    // 所以：
    //
    // 1 → -1
    // =====================================================

    const auto rightResult =
        analyzer.analyze(
            graph,
            5
        );


    if (
        !nearlyEqual(
            rightResult.outputGains[0],
            -1.0
        )
    )
    {
        std::cerr
            << "ADD_RIGHT propagation is incorrect. "
            << "Expected -1.0, got "
            << rightResult.outputGains[0]
            << ".\n";

        return 1;
    }


    // =====================================================
    // 3. 分析最终 SUB
    //
    // 误差直接出现在输出端：
    //
    // gain = 1
    // =====================================================

    const auto subResult =
        analyzer.analyze(
            graph,
            7
        );


    if (
        !nearlyEqual(
            subResult.outputGains[0],
            1.0
        )
    )
    {
        std::cerr
            << "SUB propagation is incorrect. "
            << "Expected 1.0, got "
            << subResult.outputGains[0]
            << ".\n";

        return 1;
    }


    std::cout
        << "ADD_LEFT output gain: "
        << leftResult.outputGains[0]
        << "\n";


    std::cout
        << "ADD_RIGHT output gain: "
        << rightResult.outputGains[0]
        << "\n";


    std::cout
        << "SUB output gain: "
        << subResult.outputGains[0]
        << "\n";


    std::cout
        << "PropagationAnalyzer test passed.\n";


    return 0;
}
#include "core/dfg_graph.hpp"

#include <cmath>
#include <iostream>


int main()
{
    core::DfgGraph graph;


    // =====================================================
    // 构造：
    //
    // Input_A ----\
    //              ADD_0
    // Input_B ----/   |
    //                  |
    //                 ×2
    //                  |
    //                SUB_0 ----> Output
    //                  ^
    //                  |
    // Input_B ----------+
    // =====================================================


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
            "ADD_0",
            core::DfgOperation::Add,
            0.0,
            true
        }
    );


    graph.addNode(
        {
            3,
            "MUL_CONST_0",
            core::DfgOperation::MultiplyConstant,
            2.0,
            false
        }
    );


    graph.addNode(
        {
            4,
            "SUB_0",
            core::DfgOperation::Subtract,
            0.0,
            true
        }
    );


    graph.addEdge(
        {
            0,
            2,
            0
        }
    );


    graph.addEdge(
        {
            1,
            2,
            1
        }
    );


    graph.addEdge(
        {
            2,
            3,
            0
        }
    );


    graph.addEdge(
        {
            3,
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


    graph.addOutputNode(
        4
    );


    // =====================================================
    // 检查图是否合法
    // =====================================================

    graph.validate();


    // =====================================================
    // 检查候选近似节点
    // =====================================================

    const auto candidates =
        graph.approximationCandidateIds();


    if (
        candidates.size() != 2
        ||
        candidates[0] != 2
        ||
        candidates[1] != 4
    )
    {
        std::cerr
            << "Approximation candidate detection failed.\n";

        return 1;
    }


    // =====================================================
    // 检查常数乘法
    // =====================================================

    const auto& multiplyNode =
        graph.nodeById(
            3
        );


    if (
        std::abs(
            multiplyNode.constant
            -
            2.0
        )
        >
        1e-12
    )
    {
        std::cerr
            << "Multiply constant is incorrect.\n";

        return 1;
    }


    std::cout
        << "DFG nodes: "
        << graph.nodes().size()
        << "\n";


    std::cout
        << "DFG edges: "
        << graph.edges().size()
        << "\n";


    std::cout
        << "Approximation candidates: "
        << candidates.size()
        << "\n";


    std::cout
        << "DFG graph test passed.\n";


    return 0;
}
#include "applications/dct8_dfg_builder.hpp"

#include "applications/dct8_fixed_graph.hpp"

#include <array>
#include <cstdint>
#include <sstream>
#include <string>


namespace applications
{

namespace
{

// =========================================================
// 与 Dct8FixedGraph 中使用的 Q15 DCT 系数一致
//
// 每一行：
//     一个 DCT 输出 X0 ~ X7
//
// 每一列：
//     当前输出对应的4个乘法系数
// =========================================================

constexpr std::array<
    std::array<std::int32_t, 4>,
    8
> DCT_COEFFICIENTS_Q15 =
{{
    {{ 11585,  11585,  11585,  11585 }},
    {{ 16069,  13623,   9102,   3196 }},
    {{ 15137,   6270,  -6270, -15137 }},
    {{ 13623,  -3196, -16069,  -9102 }},
    {{ 11585, -11585, -11585,  11585 }},
    {{  9102, -16069,   3196,  13623 }},
    {{  6270, -15137,  15137,  -6270 }},
    {{  3196,  -9102,  13623, -16069 }}
}};


// =========================================================
// 节点 ID 规划
//
// ADD/SUB：
//
//      0 ~ 31
//
// 与 Dct8FixedGraph 完全一致。
//
// Input：
//
//      1000 ~ 1007
//
// Constant multiplier：
//
//      2000 ~ 2031
// =========================================================

constexpr int kInputNodeBase =
    1000;


constexpr int kMultiplierNodeBase =
    2000;


// Q15:
//
// realCoefficient =
//     integerCoefficient / 2^15
constexpr double kQ15Scale =
    32768.0;


// =========================================================
// 生成 Input 名称
// =========================================================

std::string inputNodeName(
    int index
)
{
    std::ostringstream name;

    name
        << "INPUT_"
        << index;

    return name.str();
}


// =========================================================
// 生成乘法节点名称
// =========================================================

std::string multiplierNodeName(
    int outputIndex,
    int coefficientIndex
)
{
    std::ostringstream name;

    name
        << "MUL_X"
        << outputIndex
        << "_"
        << coefficientIndex;

    return name.str();
}

}


// =========================================================
// 构建完整 1D 8-point DCT DFG
// =========================================================

core::DfgGraph
Dct8DfgBuilder::build() const
{
    core::DfgGraph graph;


    // =====================================================
    // 第一部分：8 个输入节点
    //
    // ID:
    //
    // 1000 -> x0
    // 1001 -> x1
    // ...
    // 1007 -> x7
    // =====================================================

    for (int i = 0;
         i < 8;
         ++i)
    {
        graph.addNode(
            {
                kInputNodeBase + i,

                inputNodeName(i),

                core::DfgOperation::Input,

                0.0,

                false
            }
        );
    }


    // =====================================================
    // 第二部分：ADD_00 ~ ADD_03
    //
    // sums:
    //
    // ADD_00 = x0 + x7
    // ADD_01 = x1 + x6
    // ADD_02 = x2 + x5
    // ADD_03 = x3 + x4
    // =====================================================

    for (int nodeId = 0;
         nodeId < 4;
         ++nodeId)
    {
        graph.addNode(
            {
                nodeId,

                Dct8FixedGraph::nodeName(
                    nodeId
                ),

                core::DfgOperation::Add,

                0.0,

                true
            }
        );
    }


    // =====================================================
    // 第三部分：ADD_04 ~ ADD_07
    //
    // Dct8FixedGraph 实际写成：
    //
    // x0 + (-x7)
    //
    // 但结构语义实际上是：
    //
    // x0 - x7
    //
    // 因此通用 DFG 中明确表示成 Subtract。
    //
    // ADD_04 = x0 - x7
    // ADD_05 = x1 - x6
    // ADD_06 = x2 - x5
    // ADD_07 = x3 - x4
    // =====================================================

    for (int nodeId = 4;
         nodeId < 8;
         ++nodeId)
    {
        graph.addNode(
            {
                nodeId,

                Dct8FixedGraph::nodeName(
                    nodeId
                ),

                core::DfgOperation::Subtract,

                0.0,

                true
            }
        );
    }


    // =====================================================
    // 第四部分：ADD_08 ~ ADD_31
    //
    // 每个 DCT 输出包含3个累加 ADD：
    //
    // X0:
    //     ADD_08
    //     ADD_09
    //     ADD_10
    //
    // X1:
    //     ADD_11
    //     ADD_12
    //     ADD_13
    //
    // ...
    //
    // X7:
    //     ADD_29
    //     ADD_30
    //     ADD_31
    // =====================================================

    for (int nodeId = 8;
         nodeId < 32;
         ++nodeId)
    {
        graph.addNode(
            {
                nodeId,

                Dct8FixedGraph::nodeName(
                    nodeId
                ),

                core::DfgOperation::Add,

                0.0,

                true
            }
        );
    }


    // =====================================================
    // 第五部分：第一层输入边
    // =====================================================


    // -------------------------
    // sums
    // -------------------------

    graph.addEdge(
        {
            1000,
            0,
            0
        }
    );

    graph.addEdge(
        {
            1007,
            0,
            1
        }
    );


    graph.addEdge(
        {
            1001,
            1,
            0
        }
    );

    graph.addEdge(
        {
            1006,
            1,
            1
        }
    );


    graph.addEdge(
        {
            1002,
            2,
            0
        }
    );

    graph.addEdge(
        {
            1005,
            2,
            1
        }
    );


    graph.addEdge(
        {
            1003,
            3,
            0
        }
    );

    graph.addEdge(
        {
            1004,
            3,
            1
        }
    );


    // -------------------------
    // differences
    //
    // 注意：
    //
    // Subtract：
    //
    // input0 - input1
    // -------------------------

    graph.addEdge(
        {
            1000,
            4,
            0
        }
    );

    graph.addEdge(
        {
            1007,
            4,
            1
        }
    );


    graph.addEdge(
        {
            1001,
            5,
            0
        }
    );

    graph.addEdge(
        {
            1006,
            5,
            1
        }
    );


    graph.addEdge(
        {
            1002,
            6,
            0
        }
    );

    graph.addEdge(
        {
            1005,
            6,
            1
        }
    );


    graph.addEdge(
        {
            1003,
            7,
            0
        }
    );

    graph.addEdge(
        {
            1004,
            7,
            1
        }
    );


    // =====================================================
    // 第六部分：
    //
    // 32 个常数乘法节点
    //
    // 对每一个输出 Xk：
    //
    // k 为偶数：
    //     使用 sums:
    //     ADD_00 ~ ADD_03
    //
    // k 为奇数：
    //     使用 differences:
    //     ADD_04 ~ ADD_07
    // =====================================================

    for (int outputIndex = 0;
         outputIndex < 8;
         ++outputIndex)
    {
        for (int coefficientIndex = 0;
             coefficientIndex < 4;
             ++coefficientIndex)
        {
            const int multiplierNodeId =
                kMultiplierNodeBase
                +
                outputIndex * 4
                +
                coefficientIndex;


            const double coefficient =
                static_cast<double>(
                    DCT_COEFFICIENTS_Q15[
                        outputIndex
                    ][
                        coefficientIndex
                    ]
                )
                /
                kQ15Scale;


            graph.addNode(
                {
                    multiplierNodeId,

                    multiplierNodeName(
                        outputIndex,
                        coefficientIndex
                    ),

                    core::DfgOperation::MultiplyConstant,

                    coefficient,

                    false
                }
            );


            // ---------------------------------------------
            // 偶数输出：
            //
            // S0 ~ S3
            //
            // ADD_00 ~ ADD_03
            //
            //
            // 奇数输出：
            //
            // D0 ~ D3
            //
            // ADD_04 ~ ADD_07
            // ---------------------------------------------

            const int sourceNodeId =
                (
                    outputIndex % 2 == 0
                )
                ?
                coefficientIndex
                :
                4 + coefficientIndex;


            graph.addEdge(
                {
                    sourceNodeId,

                    multiplierNodeId,

                    0
                }
            );
        }
    }


    // =====================================================
    // 第七部分：
    //
    // 每个输出的3级累加 ADD
    //
    //
    // product0 ----\
    //               ADD1 ----\
    // product1 ----/          \
    //                           ADD2 ----\
    // product2 ---------------/          \
    //                                       ADD3 -> Xk
    // product3 ---------------------------/
    // =====================================================

    for (int outputIndex = 0;
         outputIndex < 8;
         ++outputIndex)
    {
        const int multiplierBase =
            kMultiplierNodeBase
            +
            outputIndex * 4;


        const int addBase =
            8
            +
            outputIndex * 3;


        // ---------------------------------------------
        // ADD1:
        //
        // product0 + product1
        // ---------------------------------------------

        graph.addEdge(
            {
                multiplierBase,
                addBase,
                0
            }
        );


        graph.addEdge(
            {
                multiplierBase + 1,
                addBase,
                1
            }
        );


        // ---------------------------------------------
        // ADD2:
        //
        // ADD1 + product2
        // ---------------------------------------------

        graph.addEdge(
            {
                addBase,
                addBase + 1,
                0
            }
        );


        graph.addEdge(
            {
                multiplierBase + 2,
                addBase + 1,
                1
            }
        );


        // ---------------------------------------------
        // ADD3:
        //
        // ADD2 + product3
        //
        // 这个节点就是当前 Xk 的最终输出。
        // ---------------------------------------------

        graph.addEdge(
            {
                addBase + 1,
                addBase + 2,
                0
            }
        );


        graph.addEdge(
            {
                multiplierBase + 3,
                addBase + 2,
                1
            }
        );


        // 当前 DCT 输出节点
        graph.addOutputNode(
            addBase + 2
        );
    }


    // =====================================================
    // 最后验证一次 DFG
    // =====================================================

    graph.validate();


    return graph;
}

}
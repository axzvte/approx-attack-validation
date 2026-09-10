#include "applications/dct8_fixed_graph.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>


namespace applications
{

namespace
{

// =========================================================
// 8-point DCT 的 Q15 系数
//
// coefficient =
// round(realCoefficient * 2^15)
//
// 每一行对应一个 DCT 输出 X0 ~ X7
// 每一列对应当前输出的4个输入
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

}


// =========================================================
// 精确整数加法
//
// 当前没有位宽限制，也没有近似。
// 后面8/10/12 bit以及EvoApprox都从这里接入。
// =========================================================

Dct8FixedGraph::Value
Dct8FixedGraph::addExact(
    int nodeId,
    Value input1,
    Value input2,
    Trace* trace
) const
{
    if (
        nodeId < 0
        ||
        nodeId >= kAddNodeCount
    )
    {
        throw std::runtime_error(
            "Invalid DCT ADD node ID."
        );
    }


    const Value output =
        input1 + input2;


    if (trace != nullptr)
    {
        (*trace)[nodeId].nodeId =
            nodeId;

        (*trace)[nodeId].input1 =
            input1;

        (*trace)[nodeId].input2 =
            input2;

        (*trace)[nodeId].output =
            output;
    }


    return output;
}


// =========================================================
// 有符号舍入右移
//
// 例如Q15乘法结果需要除以2^15。
//
// 这里不是简单截断，而是进行四舍五入。
// =========================================================

std::int64_t
Dct8FixedGraph::roundShift(
    std::int64_t value,
    int shiftBits
)
{
    if (shiftBits <= 0)
    {
        return value;
    }


    const std::int64_t half =
        std::int64_t{1}
        <<
        (shiftBits - 1);


    if (value >= 0)
    {
        return
            (value + half)
            >>
            shiftBits;
    }


    return
        -
        (
            ((-value) + half)
            >>
            shiftBits
        );
}


// =========================================================
// 精确定点乘法
//
// input：整数数据
//
// coefficient：Q15定点系数
//
// 乘法：
// input × coefficient
//
// 得到Q15乘积以后，再右移15位，
// 回到普通整数数据域。
// =========================================================

Dct8FixedGraph::Value
Dct8FixedGraph::multiplyCoefficientExact(
    Value input,
    int outputIndex,
    int coefficientIndex
) const
{
    const std::int32_t coefficient =
        DCT_COEFFICIENTS_Q15[
            outputIndex
        ][
            coefficientIndex
        ];


    const std::int64_t product =
        static_cast<std::int64_t>(
            input
        )
        *
        static_cast<std::int64_t>(
            coefficient
        );


    const std::int64_t scaled =
        roundShift(
            product,
            kCoefficientFractionBits
        );


    return
        static_cast<Value>(
            scaled
        );
}


// =========================================================
// 精确整数8-point DCT
//
// 拓扑仍然是：
//
// 8 ADD/SUB
// +
// 32 MUL
// +
// 24 ADD
//
// =
//
// 32 ADD/SUB
// +
// 32 MUL
// =========================================================

Dct8FixedGraph::Vector
Dct8FixedGraph::runExact(
    const Vector& input,
    Trace* trace
) const
{
    // =====================================================
    // 第一层：
    //
    // ADD_00 ~ ADD_03：和
    //
    // ADD_04 ~ ADD_07：差
    // =====================================================

    std::array<Value, 4>
        sums{};


    std::array<Value, 4>
        differences{};


    // s0 = x0 + x7

    sums[0] =
        addExact(
            0,
            input[0],
            input[7],
            trace
        );


    // s1 = x1 + x6

    sums[1] =
        addExact(
            1,
            input[1],
            input[6],
            trace
        );


    // s2 = x2 + x5

    sums[2] =
        addExact(
            2,
            input[2],
            input[5],
            trace
        );


    // s3 = x3 + x4

    sums[3] =
        addExact(
            3,
            input[3],
            input[4],
            trace
        );


    // d0 = x0 - x7
    //
    // 统一写成：
    //
    // x0 + (-x7)

    differences[0] =
        addExact(
            4,
            input[0],
            -input[7],
            trace
        );


    // d1 = x1 - x6

    differences[1] =
        addExact(
            5,
            input[1],
            -input[6],
            trace
        );


    // d2 = x2 - x5

    differences[2] =
        addExact(
            6,
            input[2],
            -input[5],
            trace
        );


    // d3 = x3 - x4

    differences[3] =
        addExact(
            7,
            input[3],
            -input[4],
            trace
        );


    // =====================================================
    // 8个DCT输出
    // =====================================================

    Vector output{};


    for (int k = 0;
         k < 8;
         ++k)
    {
        // 偶数频率：
        // 使用s0~s3
        //
        // 奇数频率：
        // 使用d0~d3

        const std::array<Value, 4>& values =
            (
                k % 2 == 0
            )
            ?
            sums
            :
            differences;


        // =================================================
        // 4个精确定点乘法
        //
        // 每个乘积已经缩放回整数数据域
        // =================================================

        std::array<Value, 4>
            products{};


        for (int n = 0;
             n < 4;
             ++n)
        {
            products[n] =
                multiplyCoefficientExact(
                    values[n],
                    k,
                    n
                );
        }


        // =================================================
        // 每个输出对应3个ADD
        //
        // X0：
        // ADD_08 ~ ADD_10
        //
        // X1：
        // ADD_11 ~ ADD_13
        //
        // ...
        //
        // X7：
        // ADD_29 ~ ADD_31
        // =================================================

        const int firstAddNode =
            8
            +
            3 * k;


        const Value partial01 =
            addExact(
                firstAddNode,
                products[0],
                products[1],
                trace
            );


        const Value partial012 =
            addExact(
                firstAddNode + 1,
                partial01,
                products[2],
                trace
            );


        output[k] =
            addExact(
                firstAddNode + 2,
                partial012,
                products[3],
                trace
            );
    }


    return output;
}


// =========================================================
// 节点名称
// =========================================================

std::string
Dct8FixedGraph::nodeName(
    int nodeId
)
{
    if (
        nodeId < 0
        ||
        nodeId >= kAddNodeCount
    )
    {
        throw std::runtime_error(
            "Invalid DCT ADD node ID."
        );
    }


    switch (nodeId)
    {
        case 0:
            return "ADD_00_S0";

        case 1:
            return "ADD_01_S1";

        case 2:
            return "ADD_02_S2";

        case 3:
            return "ADD_03_S3";

        case 4:
            return "ADD_04_D0";

        case 5:
            return "ADD_05_D1";

        case 6:
            return "ADD_06_D2";

        case 7:
            return "ADD_07_D3";

        default:
            break;
    }


    const int relative =
        nodeId - 8;


    const int outputIndex =
        relative / 3;


    const int accumulationIndex =
        relative % 3;


    std::ostringstream name;


    name
        << "ADD_"
        << std::setw(2)
        << std::setfill('0')
        << nodeId
        << "_X"
        << outputIndex
        << "_ACC"
        << (accumulationIndex + 1);


    return name.str();
}

}
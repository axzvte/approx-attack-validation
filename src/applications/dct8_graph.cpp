#include "applications/dct8_graph.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>


namespace applications
{

// =========================================================
// 所有精确ADD/SUB统一通过这里
//
// SUB也表示成：
//
// a - b
// =
// a + (-b)
//
// 后面加入近似加法器时，只需要改这里的运算方式，
// DCT主体结构不需要改变。
// =========================================================

double Dct8Graph::addExact(
    int nodeId,
    double input1,
    double input2,
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


    const double output =
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
// 8-point DCT
//
// OpenCV采用正交归一化DCT-II：
//
// X[k] = alpha(k) *
//        sum(
//            x[n] *
//            cos((2n+1)k*pi/16)
//        )
//
// alpha(0) = sqrt(1/8)
// alpha(k) = sqrt(2/8), k != 0
//
// ---------------------------------------------------------
// 为了形成32 ADD + 32 MUL的数据流，
// 先进行奇偶分解：
//
// s0 = x0 + x7
// s1 = x1 + x6
// s2 = x2 + x5
// s3 = x3 + x4
//
// d0 = x0 - x7
// d1 = x1 - x6
// d2 = x2 - x5
// d3 = x3 - x4
//
// 这里：8个ADD/SUB
//
// ---------------------------------------------------------
// 偶数频率使用s0~s3
// 奇数频率使用d0~d3
//
// 每个输出：
//
// 4 MUL
// +
// 3 ADD
//
// 8个输出：
//
// 32 MUL
// +
// 24 ADD
//
// 总计：
//
// 32 MUL
// +
// (8 + 24) ADD
//
// = 32 MUL + 32 ADD
// =========================================================

Dct8Graph::Vector
Dct8Graph::runExact(
    const Vector& input,
    Trace* trace
) const
{
    const double pi =
        std::acos(-1.0);


    // =====================================================
    // 第一层：
    // 4个和 + 4个差
    //
    // ADD_00 ~ ADD_07
    // =====================================================

    std::array<double, 4> sums;

    std::array<double, 4> differences;


    // -----------------------------------------------------
    // ADD_00
    // s0 = x0 + x7
    // -----------------------------------------------------

    sums[0] =
        addExact(
            0,
            input[0],
            input[7],
            trace
        );


    // -----------------------------------------------------
    // ADD_01
    // s1 = x1 + x6
    // -----------------------------------------------------

    sums[1] =
        addExact(
            1,
            input[1],
            input[6],
            trace
        );


    // -----------------------------------------------------
    // ADD_02
    // s2 = x2 + x5
    // -----------------------------------------------------

    sums[2] =
        addExact(
            2,
            input[2],
            input[5],
            trace
        );


    // -----------------------------------------------------
    // ADD_03
    // s3 = x3 + x4
    // -----------------------------------------------------

    sums[3] =
        addExact(
            3,
            input[3],
            input[4],
            trace
        );


    // -----------------------------------------------------
    // ADD_04
    // d0 = x0 - x7
    //
    // 统一表示为：
    //
    // x0 + (-x7)
    // -----------------------------------------------------

    differences[0] =
        addExact(
            4,
            input[0],
            -input[7],
            trace
        );


    // -----------------------------------------------------
    // ADD_05
    // d1 = x1 - x6
    // -----------------------------------------------------

    differences[1] =
        addExact(
            5,
            input[1],
            -input[6],
            trace
        );


    // -----------------------------------------------------
    // ADD_06
    // d2 = x2 - x5
    // -----------------------------------------------------

    differences[2] =
        addExact(
            6,
            input[2],
            -input[5],
            trace
        );


    // -----------------------------------------------------
    // ADD_07
    // d3 = x3 - x4
    // -----------------------------------------------------

    differences[3] =
        addExact(
            7,
            input[3],
            -input[4],
            trace
        );


    // =====================================================
    // 计算8个DCT系数
    //
    // 每个系数：
    //
    // 4个乘法
    // 3个加法
    //
    // ADD_08 ~ ADD_31
    // =====================================================

    Vector output{};


    for (int k = 0;
         k < 8;
         ++k)
    {
        // -------------------------------------------------
        // 偶数频率使用 sums
        // 奇数频率使用 differences
        // -------------------------------------------------

        const std::array<double, 4>&
            values =
                (
                    k % 2 == 0
                )
                ?
                sums
                :
                differences;


        // -------------------------------------------------
        // OpenCV DCT的归一化系数
        // -------------------------------------------------

        const double normalization =
            (
                k == 0
            )
            ?
            std::sqrt(
                1.0 / 8.0
            )
            :
            std::sqrt(
                2.0 / 8.0
            );


        // =================================================
        // 4个精确乘法
        // =================================================

        std::array<double, 4>
            products{};


        for (int n = 0;
             n < 4;
             ++n)
        {
            const double coefficient =
                normalization
                *
                std::cos(
                    (
                        (2.0 * n + 1.0)
                        *
                        k
                        *
                        pi
                    )
                    /
                    16.0
                );


            products[n] =
                values[n]
                *
                coefficient;
        }


        // =================================================
        // 当前输出对应3个ADD
        //
        // k = 0：
        // ADD_08 ADD_09 ADD_10
        //
        // k = 1：
        // ADD_11 ADD_12 ADD_13
        //
        // ...
        //
        // k = 7：
        // ADD_29 ADD_30 ADD_31
        // =================================================

        const int firstAddNode =
            8
            +
            3 * k;


        const double partial01 =
            addExact(
                firstAddNode,
                products[0],
                products[1],
                trace
            );


        const double partial012 =
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

std::string Dct8Graph::nodeName(
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


    // -----------------------------------------------------
    // ADD_08 ~ ADD_31
    //
    // 每3个对应一个DCT输出
    // -----------------------------------------------------

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
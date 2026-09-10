#pragma once

#include <array>
#include <string>


namespace applications
{

class Dct8Graph
{
public:

    // 8点DCT：
    // 输入8个数，输出8个DCT系数
    using Vector =
        std::array<double, 8>;


    // DCT中一个ADD/SUB位置的运行记录
    struct AddTrace
    {
        int nodeId = -1;

        double input1 = 0.0;
        double input2 = 0.0;
        double output = 0.0;
    };


    // 一个8点DCT一共有32个ADD/SUB位置
    static constexpr int kAddNodeCount =
        32;


    using Trace =
        std::array<
            AddTrace,
            kAddNodeCount
        >;


    // =====================================================
    // 精确运行8点DCT
    //
    // trace == nullptr：
    //     只计算，不保存中间信息
    //
    // trace != nullptr：
    //     同时记录32个ADD/SUB的输入输出
    // =====================================================

    Vector runExact(
        const Vector& input,
        Trace* trace = nullptr
    ) const;


    // 返回节点名称
    static std::string nodeName(
        int nodeId
    );


private:

    // 所有加/减法都统一经过这个函数
    double addExact(
        int nodeId,
        double input1,
        double input2,
        Trace* trace
    ) const;
};

}
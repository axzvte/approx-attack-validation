#pragma once

#include <array>
#include <cstdint>
#include <string>


namespace applications
{

class Dct8FixedGraph
{
public:

    using Value =
        std::int32_t;


    using Vector =
        std::array<Value, 8>;


    struct AddTrace
    {
        int nodeId = -1;

        Value input1 = 0;
        Value input2 = 0;
        Value output = 0;
    };


    static constexpr int kAddNodeCount =
        32;


    // 余弦系数使用 Q15：
    //
    // realCoefficient
    //      × 2^15
    //      ↓
    // integerCoefficient
    static constexpr int kCoefficientFractionBits =
        15;


    using Trace =
        std::array<
            AddTrace,
            kAddNodeCount
        >;


    Vector runExact(
        const Vector& input,
        Trace* trace = nullptr
    ) const;


    static std::string nodeName(
        int nodeId
    );


private:

    Value addExact(
        int nodeId,
        Value input1,
        Value input2,
        Trace* trace
    ) const;


    Value multiplyCoefficientExact(
        Value input,
        int outputIndex,
        int coefficientIndex
    ) const;


    static std::int64_t roundShift(
        std::int64_t value,
        int shiftBits
    );
};

}
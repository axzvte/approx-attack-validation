#pragma once

#include "core/attack_config.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>


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


    static constexpr int kCoefficientFractionBits =
        15;


    // 基准近似加法器
    static constexpr approximate::ApproxUnitId
        kBaselineUnit =
            approximate::ApproxUnitId::Add12se5RP;


    using Trace =
        std::array<
            AddTrace,
            kAddNodeCount
        >;


    // 全精确运行
    Vector runExact(
        const Vector& input,
        Trace* trace = nullptr
    ) const;


    // 近似运行
    Vector runApprox(
        const Vector& input,
        const std::vector<core::AttackConfig>& configs,
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


    Value addApprox(
        int nodeId,
        Value input1,
        Value input2,
        const std::vector<core::AttackConfig>& configs,
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
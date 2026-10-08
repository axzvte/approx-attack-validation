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

        // 原 Baseline 单元在当前输入下的输出。
        Value baselineOutput = 0;

        // 当前配置真正送往后续电路的输出。
        Value output = 0;
    };


    static constexpr int kAddNodeCount =
        32;


    static constexpr int kCoefficientFractionBits =
        15;


    using Trace =
        std::array<
            AddTrace,
            kAddNodeCount
        >;


    struct BaselineNodeConfig
    {
        // true  -> 使用精确加法；
        // false -> 使用 unit 指定的近似加法器。
        bool exact = true;

        approximate::ApproxUnitId unit =
            approximate::ApproxUnitId::Add12se5RP;
    };


    using BaselineConfig =
        std::array<
            BaselineNodeConfig,
            kAddNodeCount
        >;


    Dct8FixedGraph();


    explicit Dct8FixedGraph(
        const BaselineConfig& baselineConfig
    );


    void setBaselineConfig(
        const BaselineConfig& baselineConfig
    );


    const BaselineConfig&
    baselineConfig() const;


    // 当前工作 Baseline：Balanced-10。
    // Node 8、10、11、13、16、19、22、25、28、31
    // 使用 5RP，其余节点 Exact。
    //
    // 该配置由 baseline sweep 在当前候选中选作后续搜索的
    // 工作 Baseline。
    static BaselineConfig
    createDefaultBaselineConfig();


    static BaselineConfig
    createAllExactBaselineConfig();


    static BaselineConfig
    createAllApproximateBaselineConfig(
        approximate::ApproxUnitId unit
    );


    static BaselineConfig
    createSparseApproximateBaselineConfig(
        const std::vector<int>& approximateNodeIds,
        approximate::ApproxUnitId unit =
            approximate::ApproxUnitId::Add12se5RP
    );


    Vector runExact(
        const Vector& input,
        Trace* trace = nullptr
    ) const;


    Vector runApprox(
        const Vector& input,
        const std::vector<core::AttackConfig>& configs,
        Trace* trace = nullptr
    ) const;


    static std::string nodeName(
        int nodeId
    );


private:

    BaselineConfig
        baselineConfig_;


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

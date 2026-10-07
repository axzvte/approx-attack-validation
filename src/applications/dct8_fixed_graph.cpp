#include "applications/dct8_fixed_graph.hpp"

#include "approximate/evoapprox_adapter.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>


namespace applications
{

namespace
{

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
// Baseline 配置
// =========================================================

Dct8FixedGraph::Dct8FixedGraph()
    :
    baselineConfig_(
        createDefaultBaselineConfig()
    )
{
}


Dct8FixedGraph::Dct8FixedGraph(
    const BaselineConfig& baselineConfig
)
    :
    baselineConfig_(
        baselineConfig
    )
{
}


void Dct8FixedGraph::setBaselineConfig(
    const BaselineConfig& baselineConfig
)
{
    baselineConfig_ =
        baselineConfig;
}


const Dct8FixedGraph::BaselineConfig&
Dct8FixedGraph::baselineConfig() const
{
    return baselineConfig_;
}


Dct8FixedGraph::BaselineConfig
Dct8FixedGraph::createDefaultBaselineConfig()
{
    return
        createSparseApproximateBaselineConfig(
            {
                25,
                19,
                13
            },
            approximate::ApproxUnitId::Add12se5RP
        );
}


Dct8FixedGraph::BaselineConfig
Dct8FixedGraph::createAllExactBaselineConfig()
{
    BaselineConfig config{};


    for (auto& node : config)
    {
        node.exact =
            true;


        node.unit =
            approximate::ApproxUnitId::Add12se5RP;
    }


    return config;
}


Dct8FixedGraph::BaselineConfig
Dct8FixedGraph::createAllApproximateBaselineConfig(
    approximate::ApproxUnitId unit
)
{
    BaselineConfig config{};


    for (auto& node : config)
    {
        node.exact =
            false;


        node.unit =
            unit;
    }


    return config;
}


Dct8FixedGraph::BaselineConfig
Dct8FixedGraph::createSparseApproximateBaselineConfig(
    const std::vector<int>& approximateNodeIds,
    approximate::ApproxUnitId unit
)
{
    BaselineConfig config =
        createAllExactBaselineConfig();


    std::array<bool, kAddNodeCount>
        seen{};


    for (const int nodeId : approximateNodeIds)
    {
        if (
            nodeId < 0
            ||
            nodeId >= kAddNodeCount
        )
        {
            throw std::runtime_error(
                "Sparse DCT baseline contains an invalid node ID."
            );
        }


        if (seen[nodeId])
        {
            throw std::runtime_error(
                "Sparse DCT baseline contains duplicate node IDs."
            );
        }


        seen[nodeId] =
            true;


        config[nodeId].exact =
            false;


        config[nodeId].unit =
            unit;
    }


    return config;
}


// =========================================================
// 精确加法
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

        (*trace)[nodeId].baselineOutput =
            output;

        (*trace)[nodeId].output =
            output;
    }


    return output;
}


// =========================================================
// 近似加法
//
// 正常状态：
// 使用 baselineConfig_[nodeId]
//
// 攻击触发：
// 每个节点只选择一个 MonitorSignal：
// input1 / input2 / BaselineOutput。
// 当该信号落入 [lower, upper] 时，
// 最终 MUX 选择新增近似加法器输出。
// =========================================================

Dct8FixedGraph::Value
Dct8FixedGraph::addApprox(
    int nodeId,
    Value input1,
    Value input2,
    const std::vector<core::AttackConfig>& configs,
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


    // 原正常路径始终存在。
    //
    // Baseline 由每个节点自己的 BaselineNodeConfig 决定。
    // 当前默认工作配置为 Sparse-3：
    // Node 25、19、13 = 5RP，其余节点 Exact。
    // 对 BaselineOutput monitor 来说，比较器监测的就是该正常路径输出。
    const auto& baselineNode =
        baselineConfig_[nodeId];


    const Value baselineOutput =
        baselineNode.exact
        ?
        input1 + input2
        :
        approximate::addSigned12(
            input1,
            input2,
            baselineNode.unit
        );


    Value output =
        baselineOutput;


    for (const auto& config : configs)
    {
        if (config.nodeId != nodeId)
        {
            continue;
        }


        if (config.lower > config.upper)
        {
            throw std::runtime_error(
                "AttackConfig lower is greater than upper."
            );
        }


        Value monitorValue =
            0;


        switch (config.monitorInput)
        {
            case core::MonitorSignal::Input1:
                monitorValue =
                    input1;
                break;

            case core::MonitorSignal::Input2:
                monitorValue =
                    input2;
                break;

            case core::MonitorSignal::BaselineOutput:
                monitorValue =
                    baselineOutput;
                break;
        }


        if (
            monitorValue >= config.lower
            &&
            monitorValue <= config.upper
        )
        {
            // 软件仿真只在触发时计算新增单元，
            // 但硬件语义对应于：
            //
            // Baseline unit  ----\
            //                    MUX -> output
            // Attack unit    ----/
            //
            // BaselineOutput -> comparator -> MUX select
            //
            // 即两个算术分支可并行存在，比较器只决定最终 MUX。
            output =
                approximate::addSigned12(
                    input1,
                    input2,
                    config.unit
                );
        }


        break;
    }


    if (trace != nullptr)
    {
        (*trace)[nodeId].nodeId =
            nodeId;

        (*trace)[nodeId].input1 =
            input1;

        (*trace)[nodeId].input2 =
            input2;

        (*trace)[nodeId].baselineOutput =
            baselineOutput;

        (*trace)[nodeId].output =
            output;
    }


    return output;
}


// =========================================================
// 有符号四舍五入右移
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
// 精确 Q15 系数乘法
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
// 精确 DCT
// =========================================================

Dct8FixedGraph::Vector
Dct8FixedGraph::runExact(
    const Vector& input,
    Trace* trace
) const
{
    std::array<Value, 4>
        sums{};


    std::array<Value, 4>
        differences{};


    sums[0] =
        addExact(
            0,
            input[0],
            input[7],
            trace
        );


    sums[1] =
        addExact(
            1,
            input[1],
            input[6],
            trace
        );


    sums[2] =
        addExact(
            2,
            input[2],
            input[5],
            trace
        );


    sums[3] =
        addExact(
            3,
            input[3],
            input[4],
            trace
        );


    differences[0] =
        addExact(
            4,
            input[0],
            -input[7],
            trace
        );


    differences[1] =
        addExact(
            5,
            input[1],
            -input[6],
            trace
        );


    differences[2] =
        addExact(
            6,
            input[2],
            -input[5],
            trace
        );


    differences[3] =
        addExact(
            7,
            input[3],
            -input[4],
            trace
        );


    Vector output{};


    for (int k = 0;
         k < 8;
         ++k)
    {
        const std::array<Value, 4>& values =
            (
                k % 2 == 0
            )
            ?
            sums
            :
            differences;


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
// 近似 DCT
// =========================================================

Dct8FixedGraph::Vector
Dct8FixedGraph::runApprox(
    const Vector& input,
    const std::vector<core::AttackConfig>& configs,
    Trace* trace
) const
{
    std::array<bool, kAddNodeCount>
        configuredNodes{};


    // 先检查配置是否合法
    for (const auto& config : configs)
    {
        if (
            config.nodeId < 0
            ||
            config.nodeId >= kAddNodeCount
        )
        {
            throw std::runtime_error(
                "AttackConfig contains invalid node ID."
            );
        }


        if (config.lower > config.upper)
        {
            throw std::runtime_error(
                "AttackConfig lower is greater than upper."
            );
        }


        if (configuredNodes[config.nodeId])
        {
            throw std::runtime_error(
                "Duplicate AttackConfig for the same node."
            );
        }


        configuredNodes[config.nodeId] =
            true;
    }


    std::array<Value, 4>
        sums{};


    std::array<Value, 4>
        differences{};


    sums[0] =
        addApprox(
            0,
            input[0],
            input[7],
            configs,
            trace
        );


    sums[1] =
        addApprox(
            1,
            input[1],
            input[6],
            configs,
            trace
        );


    sums[2] =
        addApprox(
            2,
            input[2],
            input[5],
            configs,
            trace
        );


    sums[3] =
        addApprox(
            3,
            input[3],
            input[4],
            configs,
            trace
        );


    differences[0] =
        addApprox(
            4,
            input[0],
            -input[7],
            configs,
            trace
        );


    differences[1] =
        addApprox(
            5,
            input[1],
            -input[6],
            configs,
            trace
        );


    differences[2] =
        addApprox(
            6,
            input[2],
            -input[5],
            configs,
            trace
        );


    differences[3] =
        addApprox(
            7,
            input[3],
            -input[4],
            configs,
            trace
        );


    Vector output{};


    for (int k = 0;
         k < 8;
         ++k)
    {
        const std::array<Value, 4>& values =
            (
                k % 2 == 0
            )
            ?
            sums
            :
            differences;


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


        const int firstAddNode =
            8
            +
            3 * k;


        const Value partial01 =
            addApprox(
                firstAddNode,
                products[0],
                products[1],
                configs,
                trace
            );


        const Value partial012 =
            addApprox(
                firstAddNode + 1,
                partial01,
                products[2],
                configs,
                trace
            );


        output[k] =
            addApprox(
                firstAddNode + 2,
                partial012,
                products[3],
                configs,
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
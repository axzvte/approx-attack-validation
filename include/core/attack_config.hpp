#pragma once

#include "approximate/evoapprox_adapter.hpp"

namespace core
{

// 最终硬件中，每个被选节点只实现一个 monitor signal。
//
// Input1 / Input2:
//   直接监测该加法节点的两个输入之一。
//
// BaselineOutput:
//   监测原 Baseline 加法器（当前默认 5RP）在 MUX 之前的输出。
//   该值先于最终 MUX 选择产生，因此不会形成组合反馈。
enum class MonitorSignal
{
    Input1,
    Input2,
    BaselineOutput
};


// 兼容当前分支中尚未清理完的旧接口名称。
// 新代码统一使用 MonitorSignal。
using MonitorInput =
    MonitorSignal;


struct AttackConfig
{
    int nodeId;

    approximate::ApproxUnitId unit;


    // 字段名暂时保留 monitorInput，以避免一次性破坏旧测试/工具。
    // 其实际类型已经是 MonitorSignal，可取：
    // Input1 / Input2 / BaselineOutput。
    MonitorSignal monitorInput;


    int lower;
    int upper;
};

}

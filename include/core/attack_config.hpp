#pragma once

#include "approximate/evoapprox_adapter.hpp"

namespace core
{

enum class MonitorInput
{
    Input1,
    Input2
};


struct AttackConfig
{
    int nodeId;

    approximate::ApproxUnitId unit;

    MonitorInput monitorInput;

    int lower;
    int upper;
};

}

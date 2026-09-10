#pragma once

#include "approximate/evoapprox_adapter.hpp"

namespace core
{

struct AttackConfig
{
    int nodeId;

    approximate::ApproxUnitId unit;

    int lower;
    int upper;
};

}
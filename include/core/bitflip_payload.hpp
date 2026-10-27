#pragma once
#include "core/attack_config.hpp"
#include "core/output_bitflip.hpp"

namespace core {
// Validate even an interval which never hits: invalid hardware is not a no-op.
inline void validateOutputPayload(const AttackConfig& config) {
    if(config.monitorInput!=MonitorSignal::Input1 && config.monitorInput!=MonitorSignal::Input2 &&
            config.monitorInput!=MonitorSignal::BaselineOutput)
        throw std::runtime_error("Unknown monitor signal.");
    if(config.payload==AttackPayload::BitFlip) {
        flipNonSignOutputBit(0,config.bitIndex);
    } else if(config.payload!=AttackPayload::ApproximateUnit) {
        throw std::runtime_error("Unknown attack payload.");
    }
}
}

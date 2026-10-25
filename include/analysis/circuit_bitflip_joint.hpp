#pragma once
#include "analysis/sharpen_bitflip_joint.hpp"
#include "core/application.hpp"

namespace analysis {
struct CircuitBitflipOptions {
    std::vector<int> bits{0,1,2,3,4,5,6,7,8,9,10,11};
    std::size_t boundaryBudget=24,beamWidth=4,representativeCount=2,candidatesPerCount=4;
    std::size_t refinementRounds=1,restarts=2;
    double globalPsnrThreshold=30;
    std::size_t maxActiveNodes=0; // Zero permits all native static nodes.
    std::size_t maxContexts=256,maxFullEvaluations=4096,dctFullCandidates=16;
    bool sensitivityOrder=false; // Same baseline-only heuristic as Sharpen.
};
struct CircuitBitflipStats : JointBitflipStats {
    std::uint64_t fullEvaluations=0,proposalsRetained=0;
    bool fullBudgetExhausted=false; // contextBudgetExhausted is inherited.
};
struct CircuitBitflipResult {
    BitflipQuality baseline;
    std::vector<JointBitflipState> candidates;
    CircuitBitflipStats stats;
};
// Exact per-pixel conditional graph replay. Current must execute, but its target
// removal may be range-invalid. DCT deliberately rejects this additive API.
JointBitflipProfile profileCircuitBitflip(const core::Application&,const cv::Mat&,
    const cv::Mat&,const JointBitflipConfiguration&,int node,int bit,core::MonitorSignal);
CircuitBitflipResult searchCircuitBitflips(const core::Application&,const cv::Mat&,
    const cv::Mat&,const CircuitBitflipOptions& options={},
    const std::function<void(const JointBitflipProgress&)>& progress={});
}

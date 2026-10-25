#pragma once
#include "analysis/sharpen_bitflip_joint.hpp"
#include <array>
#include <optional>

namespace analysis {
struct FixedBitflipNode {
    int nodeId=0;
    core::MonitorSignal monitorInput=core::MonitorSignal::Input1;
    int bitIndex=0;
};
using FixedBitflipHardware=std::vector<FixedBitflipNode>;
struct FixedBitflipOptions {
    std::size_t boundaryBudget=24,beamWidth=4,representativeCount=2;
    std::size_t refinementRounds=1,restarts=2,maxContexts=128;
    double globalPsnrThreshold=30;
    std::vector<JointBitflipConfiguration> seeds;
};
struct FixedBitflipStats {
    std::uint64_t contextsBuilt=0,cacheHits=0,evaluatedIntervals=0,rangeRejected=0;
    std::uint64_t retainedQualityFailures=0,acceptedSeeds=0,rejectedSeeds=0;
    std::array<std::uint64_t,4> contextsByNode{};
    bool contextBudgetExhausted=false;
};
struct FixedBitflipProgress {
    std::size_t restart=0,pass=0,node=0,completedStates=0,totalStates=0;
    FixedBitflipStats stats;
};
struct FixedBitflipResult {
    BitflipQuality baseline;
    // Final numeric eligibility requires the global floor and increased ROI MSE.
    // bestLegal also reports a full executable H when no numeric state exists.
    std::vector<JointBitflipState> candidates;
    std::optional<JointBitflipState> bestLegal;
    FixedBitflipStats stats;
};
bool matchesFixedBitflipHardware(const JointBitflipConfiguration&,const FixedBitflipHardware&);
std::optional<JointBitflipConfiguration> dormantFixedBitflipSeed(
    const applications::SharpenApplication&,const cv::Mat&,const cv::Mat&,const FixedBitflipHardware&);
FixedBitflipResult searchFixedBitflipIntervals(const applications::SharpenApplication&,
    const cv::Mat&,const cv::Mat&,const FixedBitflipHardware&,const FixedBitflipOptions& options={},
    const std::function<void(const FixedBitflipProgress&)>& progress={});
}

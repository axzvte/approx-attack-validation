#pragma once
#include "analysis/sharpen_bitflip_search.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>

namespace analysis {
using JointBitflipConfiguration=std::vector<core::AttackConfig>;

struct JointBitflipBin {
    std::int64_t roiDelta=0,nonRoiDelta=0,invalidDelta=0,roiHits=0,nonRoiHits=0;
};
struct JointBitflipProfile {
    JointBitflipConfiguration others;
    int node=0,bit=0;
    core::MonitorSignal monitor=core::MonitorSignal::Input1;
    std::uint64_t roiPixels=0,nonRoiPixels=0;
    std::int64_t offRoiSse=0,offNonRoiSse=0,offInvalid=0;
    std::map<int,JointBitflipBin> bins;
};
struct JointIntervalScore {
    bool valid=false;
    BitflipQuality metrics;
    std::uint64_t roiTriggers=0,nonRoiTriggers=0;
    std::int64_t invalidPixels=0;
};

// current must be executable. The target is removed only in the counterfactual;
// other downstream triggers are recalculated for both off/on suffixes. Invalid
// off paths are allowed: an interval can restore their validity.
JointBitflipProfile profileJointBitflip(const applications::SharpenApplication& app,
    const cv::Mat& image,const cv::Mat& mask,const JointBitflipConfiguration& current,
    int node,int bit,core::MonitorSignal monitor);
JointIntervalScore scoreJointInterval(const JointBitflipProfile& profile,int lower,int upper);

struct JointBitflipState {
    JointBitflipConfiguration configuration;
    BitflipQuality metrics;
};
struct JointBitflipOptions {
    std::vector<int> bits{0,1,2,3,4,5,6,7,8,9,10,11};
    std::size_t boundaryBudget=24; // 0 exhaustive; otherwise grid + short windows.
    std::size_t beamWidth=4; // Per configured-node-count bucket, not a node limit.
    std::size_t representativeCount=2; // Per trade-off, per conditional expansion.
    std::size_t candidatesPerCount=4;
    std::size_t refinementRounds=1;
    std::size_t restarts=2; // Alternating forward/reverse search visitation orders.
    double globalPsnrThreshold=30;
    std::size_t maxContexts=0; // 0 preserves legacy unlimited search; otherwise >=4.
};
struct JointBitflipStats {
    std::uint64_t contextsBuilt=0,cacheHits=0,evaluatedIntervals=0,rangeRejected=0;
    std::uint64_t retainedQualityFailures=0;
    std::array<std::uint64_t,4> contextsByNode{};
    bool contextBudgetExhausted=false;
};
struct JointBitflipProgress {
    std::size_t restart=0,pass=0,node=0,completedStates=0,totalStates=0;
    JointBitflipStats stats;
};
struct JointBitflipResult {
    BitflipQuality baseline;
    // Numeric candidates only; original application replay and visual inspection
    // are required by the CLI before a user can judge the research outcome.
    std::vector<JointBitflipState> candidates;
    JointBitflipStats stats;
};
JointBitflipResult searchJointBitflips(const applications::SharpenApplication& app,
    const cv::Mat& image,const cv::Mat& mask,const JointBitflipOptions& options={},
    const std::function<void(const JointBitflipProgress&)>& progress={});
}

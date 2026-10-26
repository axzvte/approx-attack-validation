#pragma once
#include "analysis/circuit_bitflip_fixed.hpp"
#include <string>

namespace analysis {
struct TenTrainImage {int imageId=0;cv::Mat image,mask;};
struct TenTrainOptions {
    std::size_t maxEvaluations=80,maxCandidates=24,initialCandidates=3,proposalsPerRound=3;
    std::size_t maxRounds=6,patience=2,workers=4;
    CircuitFixedBitflipOptions intervals;
    TenTrainOptions() {intervals.continuous=true;intervals.maxContexts=64;}
};
struct TenTrainEvaluation {
    int imageId=0;bool legal=false,numericEligible=false;
    JointBitflipState selected;BitflipQuality baseline;CircuitFixedBitflipStats stats;
    double elapsedSeconds=0;
};
struct TenTrainCandidate {
    std::string hardwareId;FixedBitflipHardware hardware;
    std::vector<TenTrainEvaluation> evaluations;std::string status,reason;
    std::size_t generation=0,numericSuccesses=0;double worstRoiDrop=0,meanRoiDrop=0;
};
struct TenTrainProgress {
    std::size_t candidates=0,evaluations=0;std::string hardwareId;int imageId=0;
};
struct TenTrainResult {
    std::vector<TenTrainCandidate> candidates;std::optional<std::size_t> bestIndex;
    std::vector<BitflipQuality> baselines;
    std::uint64_t actualEvaluations=0,duplicateStructuresSkipped=0,referenceBuilds=0;
    double preparationSeconds=0,searchSeconds=0;std::string stopReason;
};
// Complete candidates precede partial candidates; the remaining comparison is
// success count, worst/mean ROI drop, smaller H, then canonical native H tuple.
bool betterTenTrainCandidate(const TenTrainCandidate&,const TenTrainCandidate&);
TenTrainResult searchCircuitTenTrain(const core::Application&,const std::vector<TenTrainImage>&,
    const TenTrainOptions& options={},const std::function<void(const TenTrainProgress&)>& progress={});
}

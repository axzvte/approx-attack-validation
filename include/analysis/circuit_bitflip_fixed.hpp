#pragma once
#include "analysis/sharpen_bitflip_fixed.hpp"
#include "core/application.hpp"

namespace analysis {
struct CircuitFixedBitflipOptions : FixedBitflipOptions {
    // Full DCT reconstruction scores during search only. Baseline/dormant
    // initialization and final oracle replay are separate. Coverage reserves
    // about half the budget for subsequent joint refinement and restarts.
    std::size_t maxFullEvaluations=256,fullCandidatesPerContext=8;
    bool continuous=false;
    double convergenceTolerance=.001;
};
struct CircuitFixedBitflipStats {
    std::uint64_t contextsBuilt=0,cacheHits=0,evaluatedIntervals=0,rangeRejected=0;
    std::uint64_t retainedQualityFailures=0,acceptedSeeds=0,rejectedSeeds=0;
    // Search-only complete image scores; initialization is timed separately.
    // Pixel applications use exact conditional scores instead of full scores.
    std::uint64_t fullEvaluations=0,finalReplayEvaluations=0,traceBuilds=0,traceCacheHits=0;
    // Indexed by native node id (8/11/32), including untouched zero entries.
    std::vector<std::uint64_t> contextsByNode;
    bool contextBudgetExhausted=false,fullBudgetExhausted=false;
    bool continuous=false,stoppedForConvergence=false;
    bool referenceCacheHit=false,baselineCacheHit=false;
    std::size_t completedSweeps=0;
    double referenceSeconds=0,initializationSeconds=0,traceSeconds=0,profileSeconds=0;
    double intervalSeconds=0,finalReplaySeconds=0,fullEvaluationSeconds=0;
};
struct CircuitFixedBitflipProgress {
    std::size_t restart=0,pass=0,node=0,completedStates=0,totalStates=0;
    CircuitFixedBitflipStats stats;
};
struct CircuitFixedBitflipResult {
    BitflipQuality baseline;
    std::vector<JointBitflipState> candidates;
    std::optional<JointBitflipState> bestLegal;
    CircuitFixedBitflipStats stats;
};
class CircuitFixedPreparedImage {
public:
    ~CircuitFixedPreparedImage()=default;
    CircuitFixedPreparedImage(const CircuitFixedPreparedImage&)=delete;
    CircuitFixedPreparedImage& operator=(const CircuitFixedPreparedImage&)=delete;
    BitflipQuality baselineQuality() const {return quality_;}
private:
    CircuitFixedPreparedImage()=default;
    cv::Mat image_,mask_,binary_,reference_,baseline_;
    BitflipQuality quality_;
    std::string applicationFingerprint_;
    friend std::shared_ptr<const CircuitFixedPreparedImage> prepareCircuitFixedImage(
        const core::Application&,const cv::Mat&,const cv::Mat&);
    friend CircuitFixedBitflipResult searchCircuitFixedBitflipIntervals(const core::Application&,
        const cv::Mat&,const cv::Mat&,const FixedBitflipHardware&,const CircuitFixedBitflipOptions&,
        const std::function<void(const CircuitFixedBitflipProgress&)>&,const CircuitFixedPreparedImage*);
};
std::shared_ptr<const CircuitFixedPreparedImage> prepareCircuitFixedImage(
    const core::Application&,const cv::Mat&,const cv::Mat&);
bool matchesCircuitFixedBitflipHardware(const JointBitflipConfiguration&,
    const FixedBitflipHardware&,std::size_t nativeNodeCount);
std::optional<JointBitflipConfiguration> dormantCircuitFixedBitflipSeed(
    const core::Application&,const cv::Mat&,const cv::Mat&,const FixedBitflipHardware&);
CircuitFixedBitflipResult searchCircuitFixedBitflipIntervals(const core::Application&,
    const cv::Mat&,const cv::Mat&,const FixedBitflipHardware&,
    const CircuitFixedBitflipOptions& options={},
    const std::function<void(const CircuitFixedBitflipProgress&)>& progress={},
    const CircuitFixedPreparedImage* prepared=nullptr);
}

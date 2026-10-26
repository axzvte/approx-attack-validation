#include "analysis/circuit_bitflip_joint.hpp"
#include "analysis/circuit_bitflip_fixed.hpp"
#include "applications/conv.hpp"
#include "applications/sobel.hpp"
#include "applications/dct.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using Config=analysis::JointBitflipConfiguration;
void require(bool value,const char* text) {if(!value) throw std::runtime_error(text);}
core::AttackConfig node(int id,int bit=0,core::MonitorSignal monitor=core::MonitorSignal::BaselineOutput) {
    return {id,approximate::ApproxUnitId::Add12se5RP,monitor,-4096,-4096,core::AttackPayload::BitFlip,bit};
}
void equal(const analysis::BitflipQuality& a,const analysis::BitflipQuality& b) {
    for(auto p:{std::pair<double,double>{a.globalMse,b.globalMse},{a.roiMse,b.roiMse},{a.nonRoiMse,b.nonRoiMse}})
        require(std::abs(p.first-p.second)<1e-8,"Continuation score differs from original application");
}
analysis::CircuitBitflipOptions discovery() {
    analysis::CircuitBitflipOptions o;o.bits={0,1};o.boundaryBudget=2;o.beamWidth=1;
    o.representativeCount=1;o.candidatesPerCount=2;o.maxActiveNodes=3;
    o.restarts=2;o.refinementRounds=1;o.maxContexts=12;o.maxFullEvaluations=64;
    o.dctFullCandidates=2;o.globalPsnrThreshold=0;return o;
}
void distance() {
    const Config a{node(0),node(3)};auto b=a;
    b[0].lower=10;b[0].upper=20;std::reverse(b.begin(),b.end());
    require(analysis::bitflipStructureDistance(a,b)==0,"Intervals/order must not change structure distance");
    b[0].bitIndex=1;
    require(analysis::bitflipStructureDistance(a,b)==1,"Changed bit is one node edit");
    b[0].monitorInput=core::MonitorSignal::Input1;
    require(analysis::bitflipStructureDistance(a,b)==1,"Bit and monitor at one node remain one edit");
    b.pop_back();b.push_back(node(4));
    require(analysis::bitflipStructureDistance(a,b)==3,"Replace node identity counts removal and addition");
    require(analysis::bitflipStructureDistance({},a)==2,"Added nodes must be counted");
}
void boundedLocalAndGlobal() {
    applications::SobelApplication app(applications::SobelApplication::createAllExactBaselineConfig());
    cv::Mat image(5,5,CV_8UC1,cv::Scalar(40)),mask(5,5,CV_8UC1,cv::Scalar(0));
    mask.at<unsigned char>(2,2)=255;
    const Config seed{node(0),node(10)};
    auto o=discovery();o.structureSeeds={seed};o.structureRadius=1;o.globalExplorationContexts=3;
    const auto result=analysis::searchCircuitBitflips(app,image,mask,o);
    require(result.stats.acceptedStructureSeeds==1 && result.stats.rejectedStructureSeeds==0,"Usable seed accounting wrong");
    require(result.stats.contextsBuilt<=o.maxContexts,"Local and global discovery exceeded common context cap");
    require(result.stats.localContexts>0 && result.stats.localContexts<=9,"Local phase ignored its share");
    require(result.stats.globalContexts>0 && result.stats.globalContexts<=3,"Reserved global phase was starved or exceeded");
    require(result.stats.localContexts+result.stats.globalContexts==result.stats.contextsBuilt,"Context phases were not fully accounted");
    require(result.stats.localCandidatesFiltered>0,"Radius should exclude structural drift");
    o.globalExplorationContexts=0;
    const auto local=analysis::searchCircuitBitflips(app,image,mask,o);
    for(const auto& s:local.candidates) {
        require(analysis::bitflipStructureDistance(seed,s.configuration)<=o.structureRadius,"Local search drifted from original anchor");
        equal(s.metrics,analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,s.configuration),mask));
    }
    o.structureRadius=0;
    const auto intervalsOnly=analysis::searchCircuitBitflips(app,image,mask,o);
    for(const auto& s:intervalsOnly.candidates)
        require(analysis::bitflipStructureDistance(seed,s.configuration)==0,"Radius zero changed installed structure");
    o.bits={1}; // Existing seed bit 0 still needs an interval-only search.
    const auto warmBit=analysis::searchCircuitBitflips(app,image,mask,o);
    require(warmBit.stats.localContexts>0 && warmBit.stats.evaluatedIntervals>0,"Existing out-of-list bit lost interval refinement");
    o.structureSeeds.clear();o.globalExplorationContexts=0;
    const auto legacy=analysis::searchCircuitBitflips(app,image,mask,o);
    require(legacy.stats.localContexts==0 && legacy.stats.globalContexts==legacy.stats.contextsBuilt,"No-seed discovery must report global work");
}
void validationAndFallback() {
    cv::Mat image(5,5,CV_8UC1,cv::Scalar(40)),mask(5,5,CV_8UC1,cv::Scalar(0));mask.at<unsigned char>(2,2)=255;
    applications::ConvApplication app;
    auto o=discovery();o.structureSeeds={{node(0),node(0)}};
    const auto malformed=[&] {
        bool failed=false;try {analysis::searchCircuitBitflips(app,image,mask,o);}
        catch(const std::exception&) {failed=true;}require(failed,"Malformed structural seed silently disappeared");
    };
    malformed();o.structureSeeds={{node(8)}};malformed();
    o.structureSeeds={{node(0)}};o.structureSeeds[0][0].lower=-4097;malformed();
    o.structureSeeds={{node(0,12)}};malformed();
    o.structureSeeds={{node(0)}};o.structureSeeds[0][0].unit=approximate::ApproxUnitId::Add12se5PD;malformed();
    o.structureSeeds={{node(0)}};o.structureSeeds[0][0].monitorInput=static_cast<core::MonitorSignal>(99);malformed();
    class RangeSeed final : public applications::ConvApplication {
        cv::Mat runApprox(const cv::Mat& x,const std::vector<core::AttackConfig>& c) const override {
            if(!c.empty()) throw std::runtime_error("Value exceeds signed 12-bit range.");
            return applications::ConvApplication::runApprox(x,c);
        }
    } range;
    o.structureSeeds={{node(0),node(7)}};o.globalExplorationContexts=1;
    const auto fallback=analysis::searchCircuitBitflips(range,image,mask,o);
    require(fallback.stats.acceptedStructureSeeds==0 && fallback.stats.rejectedStructureSeeds==1,"Range-invalid seed rejection missing");
    require(fallback.stats.localContexts==0 && fallback.stats.globalContexts==fallback.stats.contextsBuilt &&
        fallback.stats.contextsBuilt>1 && fallback.stats.contextsBuilt<=o.maxContexts,"No-valid-seed fallback must use the total global cap");
    class Broken final : public applications::ConvApplication {
        cv::Mat runApprox(const cv::Mat& x,const std::vector<core::AttackConfig>& c) const override {
            if(!c.empty()) throw std::runtime_error("Unexpected continuation failure");
            return applications::ConvApplication::runApprox(x,c);
        }
    } broken;
    bool propagated=false;try {analysis::searchCircuitBitflips(broken,image,mask,o);}
    catch(const std::runtime_error& e) {propagated=std::string(e.what())=="Unexpected continuation failure";}
    require(propagated,"Unexpected seed oracle failures must propagate");
}
void dctFiltersBeforeFullScore() {
    class GuardedDct final : public applications::DctApplication {
    public:
        Config anchor;mutable std::size_t scored=0;
        GuardedDct():applications::DctApplication(applications::Dct8FixedGraph::createAllExactBaselineConfig()) {}
        cv::Mat runApprox(const cv::Mat& x,const std::vector<core::AttackConfig>& c) const override {
            if(!anchor.empty() && !c.empty()) {
                require(analysis::bitflipStructureDistance(c,anchor)==0,"Disallowed DCT structure reached expensive full scoring");++scored;
            }
            return applications::DctApplication::runApprox(x,c);
        }
    } app;
    cv::Mat image(8,8,CV_8UC1,cv::Scalar(120)),mask(8,8,CV_8UC1,cv::Scalar(0));mask(cv::Rect(2,2,2,2)).setTo(255);
    app.anchor={node(0),node(31)};
    auto o=discovery();o.structureSeeds={app.anchor};o.structureRadius=0;o.maxContexts=4;
    const auto result=analysis::searchCircuitBitflips(app,image,mask,o);
    require(result.stats.localCandidatesFiltered>0 && app.scored>1,"DCT local filtering or complete image scoring missing");
    require(result.stats.globalContexts==0 && result.stats.contextsBuilt<=4,"DCT local context cap violated");
    for(const auto& s:result.candidates)
        equal(s.metrics,analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,s.configuration),mask));
}
void continuousFixedHardware() {
    class CountingSobel final : public applications::SobelApplication {
    public:
        mutable std::size_t references=0;
        CountingSobel():applications::SobelApplication(createAllExactBaselineConfig()) {}
        cv::Mat runExact(const cv::Mat& x) const override {++references;return applications::SobelApplication::runExact(x);}
    } app;
    cv::Mat image(5,5,CV_8UC1,cv::Scalar(40)),mask(5,5,CV_8UC1,cv::Scalar(0));mask.at<unsigned char>(2,2)=255;
    const analysis::FixedBitflipHardware h{{0,core::MonitorSignal::BaselineOutput,4},{10,core::MonitorSignal::BaselineOutput,0}};
    Config seed{node(0,4),node(10)};seed[0].lower=120;seed[0].upper=120;
    const auto seedQuality=analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,seed),mask);app.references=0;
    analysis::CircuitFixedBitflipOptions o;o.continuous=true;o.convergenceTolerance=1e9;
    o.seeds={seed};o.boundaryBudget=2;o.beamWidth=1;o.representativeCount=1;
    o.restarts=3;o.refinementRounds=1;o.maxContexts=200;o.globalPsnrThreshold=0;
    const auto result=analysis::searchCircuitFixedBitflipIntervals(app,image,mask,h,o);
    require(app.references==1,"Continuous sweeps rebuilt exact image reference");
    require(result.stats.continuous && result.stats.stoppedForConvergence && result.stats.completedSweeps==2,
        "Numeric stagnation requires at least two complete sweeps");
    require(result.bestLegal && result.bestLegal->metrics.roiMse>=seedQuality.roiMse,"Early legal best was discarded across sweeps");
    require(result.stats.finalReplayEvaluations>0 && result.stats.cacheHits>0,"Continuous cache or final oracle was bypassed");
    const auto verify=[&](const auto& s) {
        require(analysis::matchesCircuitFixedBitflipHardware(s.configuration,h,11),"Continuous refinement changed fixed H");
        equal(s.metrics,analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,s.configuration),mask));
    };
    verify(*result.bestLegal);for(const auto& s:result.candidates) verify(s);
    o.maxContexts=1;
    const auto partial=analysis::searchCircuitFixedBitflipIntervals(app,image,mask,h,o);
    require(partial.stats.contextBudgetExhausted && partial.stats.completedSweeps==0 && !partial.stats.stoppedForConvergence,
        "Partial sweep was reported as complete or converged");
    require(partial.bestLegal && partial.stats.contextsBuilt==1,"Budget stop lost seed best or exceeded context cap");
    o.maxContexts=200;o.globalPsnrThreshold=1000;o.restarts=1;o.refinementRounds=0;
    const auto noNumeric=analysis::searchCircuitFixedBitflipIntervals(app,image,mask,h,o);
    require(noNumeric.stats.completedSweeps==2 && !noNumeric.stats.stoppedForConvergence,"Quality-ineligible states must not trigger numeric convergence");
}
void continuousDctHardBudget() {
    applications::DctApplication app(applications::Dct8FixedGraph::createAllExactBaselineConfig());
    cv::Mat image(8,8,CV_8UC1,cv::Scalar(120)),mask(8,8,CV_8UC1,cv::Scalar(0));mask(cv::Rect(1,1,3,3)).setTo(255);
    const analysis::FixedBitflipHardware h{{0,core::MonitorSignal::BaselineOutput,2},{31,core::MonitorSignal::BaselineOutput,2}};
    analysis::CircuitFixedBitflipOptions o;o.continuous=true;o.maxContexts=8;o.maxFullEvaluations=1;
    o.fullCandidatesPerContext=1;o.beamWidth=1;o.representativeCount=1;o.boundaryBudget=2;o.globalPsnrThreshold=0;
    const auto result=analysis::searchCircuitFixedBitflipIntervals(app,image,mask,h,o);
    require(result.stats.fullEvaluations==1 && result.stats.fullBudgetExhausted,"Continuous DCT did not honor full-image score cap");
    require(result.stats.completedSweeps==0 && !result.stats.stoppedForConvergence,"DCT full-budget interruption was reported as convergence");
    require(result.bestLegal && result.stats.finalReplayEvaluations>0,"Continuous DCT discarded legal initialization before final replay");
    require(analysis::matchesCircuitFixedBitflipHardware(result.bestLegal->configuration,h,32),"Continuous DCT changed installed H");
    equal(result.bestLegal->metrics,analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,result.bestLegal->configuration),mask));
}
}
int main() {
    try {distance();boundedLocalAndGlobal();validationAndFallback();dctFiltersBeforeFullScore();continuousFixedHardware();continuousDctHardBudget();
        std::cout<<"Circuit continuation contracts passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

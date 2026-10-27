#include "analysis/sharpen_bitflip_fixed.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

// Written before the implementation. Deliberately UNRUN: local execution is
// disabled by the project/user workflow; these fixtures use the real oracle.
namespace {
using namespace analysis;
using Signal=core::MonitorSignal;
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void close(double a,double b) {require(std::abs(a-b)<1e-8,"Oracle quality differs");}
core::AttackConfig flip(int n,int bit,Signal m,int lo,int hi) {
    return {n,approximate::ApproxUnitId::Add12se5RP,m,lo,hi,core::AttackPayload::BitFlip,bit};
}
template<class F> void rejects(F f) {
    bool threw=false;try {f();} catch(const std::runtime_error&) {threw=true;}
    require(threw,"Malformed hardware/seed must be an error, not silently skipped");
}
cv::Mat image() {return cv::Mat(3,3,CV_8UC1,cv::Scalar(40));}
cv::Mat mask() {cv::Mat m(3,3,CV_8UC1,cv::Scalar(0));m.at<unsigned char>(1,1)=255;return m;}
void assertHardware(const JointBitflipConfiguration& c,const FixedBitflipHardware& h) {
    require(c.size()==h.size() && matchesFixedBitflipHardware(c,h),"Installed hardware changed");
    for(const auto& x:c) {
        const int lo=x.monitorInput==Signal::BaselineOutput?-4096:-2048;
        const int hi=x.monitorInput==Signal::BaselineOutput?4095:2047;
        require(x.payload==core::AttackPayload::BitFlip && x.lower<=x.upper && x.lower>=lo && x.upper<=hi,
            "Executable interval uses an enable, empty interval, or invalid signed domain");
    }
}
void oracle(const applications::SharpenApplication& app,const cv::Mat& im,const cv::Mat& m,const JointBitflipState& s) {
    const auto q=measureBitflipQuality(app.runExact(im),app.runApprox(im,s.configuration),m);
    close(q.globalMse,s.metrics.globalMse);close(q.roiMse,s.metrics.roiMse);close(q.nonRoiMse,s.metrics.nonRoiMse);
}

// Catches implicit node pruning and fake no-hit sentinels.
void dormantSeedKeepsSignedDomainsAndAllNodes() {
    applications::SharpenApplication app;
    const FixedBitflipHardware h{{0,Signal::Input1,11},{1,Signal::Input2,3},
        {2,Signal::BaselineOutput,0},{3,Signal::BaselineOutput,4}};
    const auto im=image(),m=mask();const auto seed=dormantFixedBitflipSeed(app,im,m,h);
    require(seed.has_value(),"Sparse observed signals must admit legal dormant intervals");assertHardware(*seed,h);
    std::vector<core::AddSample> samples;app.collectConfiguredAddSamples(im,m,*seed,samples);
    for(const auto& c:*seed) {
        require(c.lower==c.upper,"Dormant intervals must be closed representable singletons");
        for(const auto& s:samples) if(s.nodeId==c.nodeId) {
            const int value=c.monitorInput==Signal::Input1?s.input1:c.monitorInput==Signal::Input2?s.input2:s.baselineOutput;
            require(value!=c.lower,"Dormant singleton unexpectedly triggers");
        }
    }
    const auto q=measureBitflipQuality(app.runApprox(im,{}),app.runApprox(im,*seed),m);close(q.globalMse,0);
    auto moved=*seed;moved[0].lower=0;moved[0].upper=1;
    require(matchesFixedBitflipHardware(moved,h),"Hardware equality must ignore interval endpoints");
    moved[0].payload=core::AttackPayload::ApproximateUnit;
    require(!matchesFixedBitflipHardware(moved,h),"Hardware equality must validate bit-flip payload");
    moved=*seed;moved.pop_back();require(!matchesFixedBitflipHardware(moved,h),"Missing installed node accepted");
    moved=*seed;moved[0].bitIndex=10;require(!matchesFixedBitflipHardware(moved,h),"Changed fixed bit accepted");
    moved=*seed;moved[0].monitorInput=Signal::Input2;require(!matchesFixedBitflipHardware(moved,h),"Changed fixed monitor accepted");
    moved=*seed;moved[1]=moved[0];require(!matchesFixedBitflipHardware(moved,h),"Duplicate installed node accepted");
    rejects([&]{dormantFixedBitflipSeed(app,im,m,{{0,Signal::Input1,0}});});
    rejects([&]{dormantFixedBitflipSeed(app,im,m,{{0,Signal::Input1,0},{0,Signal::Input2,1}});});
}

// Catches final-gate filtering of legal intermediate states and budget starvation.
void budgetsCoverTargetsBeforeManyBeamsAndRetainLegalFailures() {
    applications::SharpenApplication app;
    const FixedBitflipHardware h{{0,Signal::Input1,0},{1,Signal::Input2,2},{3,Signal::BaselineOutput,4}};
    FixedBitflipOptions opt;opt.maxContexts=3;opt.globalPsnrThreshold=1000;
    opt.beamWidth=4;opt.restarts=2;
    std::vector<std::size_t> firstNodes;
    auto result=searchFixedBitflipIntervals(app,image(),mask(),h,opt,[&](const FixedBitflipProgress& p) {
        if(firstNodes.size()<3) firstNodes.push_back(p.node);
        require(p.stats.contextsBuilt<=3,"Context budget exceeded in callback");
    });
    require(result.stats.contextsBuilt==3 && result.stats.contextBudgetExhausted,"Bounded search did not report exhaustion");
    require(firstNodes.size()==3 && firstNodes[0]!=firstNodes[1] && firstNodes[0]!=firstNodes[2] && firstNodes[1]!=firstNodes[2],
        "Coverage must visit each target before refining many beam states");
    require(result.stats.contextsByNode[0]==1 && result.stats.contextsByNode[1]==1 && result.stats.contextsByNode[3]==1,
        "Per-node context counts do not describe installed-node coverage");
    require(result.bestLegal.has_value(),"No numeric candidate must still expose a legal diagnostic state");
    require(result.candidates.empty(),"Impossible final quality floor must not fabricate numeric success");
    require(result.stats.retainedQualityFailures>0,"Quality-failing intermediate states must survive search");
    assertHardware(result.bestLegal->configuration,h);oracle(app,image(),mask(),*result.bestLegal);
    opt.maxContexts=1;const auto tiny=searchFixedBitflipIntervals(app,image(),mask(),h,opt);
    require(tiny.stats.contextsBuilt==1 && tiny.bestLegal.has_value(),"Small positive budget must yield partial search");
    assertHardware(tiny.bestLegal->configuration,h);
}

// Catches requiring two actual trigger nodes rather than two installed nodes.
void oneTriggerWithDormantInstalledNodeIsNumericCandidate() {
    applications::SharpenApplication app(applications::SharpenApplication::createAllExactBaselineConfig());
    const FixedBitflipHardware h{{0,Signal::Input1,0},{2,Signal::Input2,0}};
    FixedBitflipOptions opt;opt.maxContexts=1;opt.globalPsnrThreshold=30;
    const auto r=searchFixedBitflipIntervals(app,image(),mask(),h,opt);
    require(!r.candidates.empty(),"One active node with two installed nodes must remain eligible");
    for(const auto& s:r.candidates) {
        assertHardware(s.configuration,h);oracle(app,image(),mask(),s);
        require(s.metrics.roiMse>r.baseline.roiMse && s.metrics.globalPsnr()>=30,"Numeric gate incorrect");
        const auto dormant=std::find_if(s.configuration.begin(),s.configuration.end(),[](const auto& c){return c.nodeId==2;});
        require(dormant!=s.configuration.end() && !(dormant->lower<=-40 && dormant->upper>=-40),"Zero-hit installed node disappeared");
    }
}

// Catches silent acceptance of a malformed seed and broad exception swallowing.
void warmSeedsValidateIdentityDomainsAndArithmeticRejection() {
    auto baseline=applications::SharpenApplication::createAllExactBaselineConfig();baseline[1].exact=false;
    applications::SharpenApplication app(baseline);
    const FixedBitflipHardware h{{0,Signal::Input1,11},{3,Signal::Input2,0}};
    FixedBitflipOptions opt;opt.maxContexts=1;
    opt.seeds={{flip(0,11,Signal::Input1,200,200),flip(3,0,Signal::Input2,-2048,-2048)}};
    const auto r=searchFixedBitflipIntervals(app,image(),mask(),h,opt);
    require(r.stats.rejectedSeeds==1,"Only arithmetic-width-invalid warm seed must be counted and skipped");
    require(r.bestLegal.has_value(),"Dormant seed must survive invalid warm start");assertHardware(r.bestLegal->configuration,h);
    opt.seeds={{flip(0,10,Signal::Input1,200,200),flip(3,0,Signal::Input2,-2048,-2048)}};
    rejects([&]{searchFixedBitflipIntervals(app,image(),mask(),h,opt);});
    opt.seeds={{flip(0,11,Signal::Input1,-2049,0),flip(3,0,Signal::Input2,-2048,-2048)}};
    rejects([&]{searchFixedBitflipIntervals(app,image(),mask(),h,opt);});
    opt.seeds={{flip(0,11,Signal::Input1,1,0),flip(3,0,Signal::Input2,-2048,-2048)}};
    rejects([&]{searchFixedBitflipIntervals(app,image(),mask(),h,opt);});
    opt.seeds={{flip(0,11,Signal::Input1,-2048,-2048),flip(3,0,Signal::Input2,-2048,-2048)}};
    require(searchFixedBitflipIntervals(app,image(),mask(),h,opt).stats.acceptedSeeds>=1,"Valid train seed not accepted");
    baseline[0].exact=false;baseline[0].unit=static_cast<approximate::ApproxUnitId>(999);
    applications::SharpenApplication badUnit(baseline);
    rejects([&]{searchFixedBitflipIntervals(badUnit,image(),mask(),h,opt);});
}

// Catches adding independent deltas and failing to replay downstream triggers.
void fullHardwareProfileRecomputesDownstreamAndRepair() {
    applications::SharpenApplication exact(applications::SharpenApplication::createAllExactBaselineConfig());
    const auto im=image(),m=mask();
    const JointBitflipConfiguration current{flip(0,4,Signal::Input1,200,200),flip(3,4,Signal::Input1,96,96)};
    const auto p=profileJointBitflip(exact,im,m,current,0,4,Signal::Input1);
    const auto score=scoreJointInterval(p,200,200);require(score.valid,"Compensating full hardware must execute");
    close(score.metrics.roiMse,0);require(exact.runApprox(im,current).at<unsigned char>(1,1)==40,"Downstream trigger fixture invalid");
    FixedBitflipOptions opt;opt.maxContexts=2;opt.seeds={current};opt.globalPsnrThreshold=0;
    const FixedBitflipHardware h{{0,Signal::Input1,4},{3,Signal::Input1,4}};
    const auto result=searchFixedBitflipIntervals(exact,im,m,h,opt);
    require(result.stats.acceptedSeeds>=1,"Conditionally compensating warm seed lost");
    for(const auto& s:result.candidates) {assertHardware(s.configuration,h);oracle(exact,im,m,s);}

    auto bc=applications::SharpenApplication::createAllExactBaselineConfig();bc[3].exact=false;
    applications::SharpenApplication repairApp(bc);cv::Mat dark(3,3,CV_8UC1,cv::Scalar(0));
    dark.at<unsigned char>(0,1)=255;dark.at<unsigned char>(2,1)=255;dark.at<unsigned char>(1,0)=255;
    const JointBitflipConfiguration repair{flip(0,10,Signal::Input1,-2048,2047),
        flip(1,9,Signal::BaselineOutput,-1534,-1534),flip(2,11,Signal::Input1,-2048,2047)};
    const auto profile=profileJointBitflip(repairApp,dark,m,repair,1,9,Signal::BaselineOutput);
    require(profile.offInvalid==1,"Removal fixture must be invalid off path");
    require(scoreJointInterval(profile,-1534,-1534).valid,"Exact interval scorer must repair invalid removal");
    SharpenBitflipProfileWorkspace repairWorkspace(repairApp,dark,m);
    const auto cachedRepair=repairWorkspace.profile(repair,1,9,Signal::BaselineOutput);
    require(cachedRepair.offInvalid==1,"Cached path hid invalid target removal");
    const auto cachedScore=scoreJointInterval(cachedRepair,-1534,-1534);
    require(cachedScore.valid,"Cached path failed off-path validity repair");
    close(cachedScore.metrics.globalMse,scoreJointInterval(profile,-1534,-1534).metrics.globalMse);
    const FixedBitflipHardware repairH{{0,Signal::Input1,10},{1,Signal::BaselineOutput,9},{2,Signal::Input1,11}};
    opt.seeds={repair};opt.maxContexts=4;
    const auto repaired=searchFixedBitflipIntervals(repairApp,dark,m,repairH,opt);
    require(repaired.bestLegal.has_value(),"Legal warm seed with invalid removal must not disappear");
    require(repaired.stats.contextsByNode[1]>=1 && repaired.stats.rangeRejected>0,
        "Repair context must reject untriggered invalid off paths while retaining repaired intervals");
    assertHardware(repaired.bestLegal->configuration,repairH);oracle(repairApp,dark,m,*repaired.bestLegal);
    for(const auto& s:repaired.candidates) {assertHardware(s.configuration,repairH);oracle(repairApp,dark,m,s);}
}

// Catches prefix subtraction/end-point errors that a one-pixel profile misses.
void multipleBinsMatchOriginalApplication() {
    applications::SharpenApplication app;
    cv::Mat im(5,5,CV_8UC1),m(5,5,CV_8UC1,cv::Scalar(0));
    for(int r=0;r<5;++r) for(int c=0;c<5;++c) im.at<unsigned char>(r,c)=40+(r*19+c*31)%140;
    m(cv::Rect(1,1,2,2)).setTo(255);
    const FixedBitflipHardware h{{0,Signal::Input1,0},{1,Signal::Input2,2},{3,Signal::BaselineOutput,3}};
    FixedBitflipOptions opt;opt.maxContexts=8;opt.boundaryBudget=4;opt.globalPsnrThreshold=0;
    const auto result=searchFixedBitflipIntervals(app,im,m,h,opt);
    require(result.bestLegal.has_value() && result.stats.evaluatedIntervals>8,"Multiple-bin fixture did not explore intervals");
    assertHardware(result.bestLegal->configuration,h);oracle(app,im,m,*result.bestLegal);
    for(const auto& state:result.candidates) {assertHardware(state.configuration,h);oracle(app,im,m,state);}
}
// Equivalence against the unchanged legacy profiler, including every bin.
void cachedProfilesMatchLegacyAndReuseTrace() {
    struct CountReference:applications::SharpenApplication {
        mutable unsigned calls=0;
        cv::Mat runExact(const cv::Mat& im) const override {++calls;return applications::SharpenApplication::runExact(im);}
    } app;
    cv::Mat im(5,5,CV_8UC1),m(5,5,CV_8UC1,cv::Scalar(0));
    for(int r=0;r<5;++r) for(int c=0;c<5;++c) im.at<unsigned char>(r,c)=40+(r*19+c*31)%140;
    m(cv::Rect(1,1,2,2)).setTo(255);
    JointBitflipConfiguration c{flip(0,0,Signal::Input1,0,1000),flip(2,2,Signal::BaselineOutput,0,1000)};
    SharpenBitflipProfileWorkspace workspace(app,im,m);
    require(app.calls==1,"Workspace did not prepare exactly one reference");unsigned legacyCalls=0;
    for(int n=0;n<4;++n) for(auto signal:{Signal::Input1,Signal::Input2,Signal::BaselineOutput}) {
        const auto old=profileJointBitflip(app,im,m,c,n,3,signal),fast=workspace.profile(c,n,3,signal);
        require(app.calls==1+(++legacyCalls),"Cached profiler recomputed the exact reference");
        require(old.offInvalid==fast.offInvalid && old.offRoiSse==fast.offRoiSse && old.offNonRoiSse==fast.offNonRoiSse,"Cached off-path differs");
        require(old.roiPixels==fast.roiPixels && old.nonRoiPixels==fast.nonRoiPixels && old.bins.size()==fast.bins.size(),"Cached denominators/bins differ");
        for(const auto& p:old.bins) {
            const auto& b=fast.bins.at(p.first);const auto& a=p.second;
            require(a.roiDelta==b.roiDelta && a.nonRoiDelta==b.nonRoiDelta && a.invalidDelta==b.invalidDelta && a.roiHits==b.roiHits && a.nonRoiHits==b.nonRoiHits,"Cached monitor bin differs");
        }
    }
    require(workspace.stats().traceBuilds==1 && workspace.stats().traceCacheHits==11,"Identical configured trace was not reused");
    c[0].lower=1;workspace.profile(c,0,3,Signal::Input1);
    require(workspace.stats().traceBuilds==2,"Changed configuration reused stale trace");
    for(double t:{workspace.stats().referenceSeconds,workspace.stats().traceSeconds,workspace.stats().profileSeconds})
        require(std::isfinite(t) && t>=0,"Invalid profiler timing");
}
}
int main() {
    try {
        dormantSeedKeepsSignedDomainsAndAllNodes();budgetsCoverTargetsBeforeManyBeamsAndRetainLegalFailures();
        oneTriggerWithDormantInstalledNodeIsNumericCandidate();warmSeedsValidateIdentityDomainsAndArithmeticRejection();
        fullHardwareProfileRecomputesDownstreamAndRepair();multipleBinsMatchOriginalApplication();
        cachedProfilesMatchLegacyAndReuseTrace();
        std::cout<<"Fixed hardware interval contracts passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

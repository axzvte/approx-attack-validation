#include "analysis/circuit_bitflip_ten_train.hpp"
#include "applications/conv.hpp"
#include "applications/sobel.hpp"
#include "applications/dct.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

// Authored before implementation. Static review only; not run by the author.
namespace {
void require(bool x,const char* why) {if(!x) throw std::runtime_error(why);}
void equal(const analysis::BitflipQuality& a,const analysis::BitflipQuality& b) {
    require(std::abs(a.globalMse-b.globalMse)<1e-8 && std::abs(a.roiMse-b.roiMse)<1e-8 &&
        std::abs(a.nonRoiMse-b.nonRoiMse)<1e-8,"Original full-image oracle differs");
}
std::vector<analysis::TenTrainImage> images() {
    std::vector<analysis::TenTrainImage> out;
    for(int id=1;id<=10;++id) {
        cv::Mat image(8,8,CV_8UC1),mask(8,8,CV_8UC1,cv::Scalar(0));
        for(int r=0;r<8;++r) for(int c=0;c<8;++c) image.at<unsigned char>(r,c)=120+(r*3+c*5+id)%17;
        mask(cv::Rect(2,2,3,3)).setTo(255);out.push_back({id,image,mask});
    }
    return out;
}
analysis::TenTrainOptions small() {
    analysis::TenTrainOptions o;o.maxEvaluations=20;o.maxCandidates=3;o.initialCandidates=2;
    o.maxRounds=1;o.workers=2;o.intervals.maxContexts=2;o.intervals.maxFullEvaluations=2;
    o.intervals.boundaryBudget=1;o.intervals.beamWidth=1;o.intervals.representativeCount=1;
    o.intervals.fullCandidatesPerContext=1;o.intervals.restarts=1;o.intervals.refinementRounds=0;return o;
}
void preparedIdentity() {
    applications::ConvApplication app;auto input=images().front();
    auto prepared=analysis::prepareCircuitFixedImage(app,input.image,input.mask);
    const auto original=input.image.clone();input.image.at<unsigned char>(0,0)^=1;
    analysis::FixedBitflipHardware h{{0,core::MonitorSignal::Input1,0},{7,core::MonitorSignal::BaselineOutput,1}};
    auto o=small().intervals;bool rejected=false;
    try {analysis::searchCircuitFixedBitflipIntervals(app,input.image,input.mask,h,o,{},prepared.get());}
    catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Prepared pixels must be cloned and identity checked");
    input.image=original;auto alteredMask=input.mask.clone();alteredMask.at<unsigned char>(0,0)=128;rejected=false;
    try {analysis::searchCircuitFixedBitflipIntervals(app,input.image,alteredMask,h,o,{},prepared.get());}
    catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Prepared mask mismatch accepted");
    applications::ConvApplication changed(applications::ConvApplication::createAllExactBaselineConfig());rejected=false;
    try {analysis::searchCircuitFixedBitflipIntervals(changed,input.image,input.mask,h,o,{},prepared.get());}
    catch(const std::invalid_argument&) {rejected=true;}require(rejected,"Prepared baseline configuration mismatch accepted");
    const auto cached=analysis::searchCircuitFixedBitflipIntervals(app,input.image,input.mask,h,o,{},prepared.get());
    const auto plain=analysis::searchCircuitFixedBitflipIntervals(app,input.image,input.mask,h,o);
    require(cached.stats.referenceCacheHit && cached.stats.baselineCacheHit,"Prepared hits must be reported");
    require(!plain.stats.referenceCacheHit && !plain.stats.baselineCacheHit,"Plain path falsely reports cache hit");
    equal(cached.baseline,plain.baseline);require(bool(cached.bestLegal)==bool(plain.bestLegal),"Cache altered legality");
    if(cached.bestLegal) equal(cached.bestLegal->metrics,plain.bestLegal->metrics);
}
void budgetAndOracle(const core::Application& app) {
    auto data=images();auto o=small();const auto main=std::this_thread::get_id();std::size_t calls=0;
    const auto result=analysis::searchCircuitTenTrain(app,data,o,[&](const auto& p) {
        require(std::this_thread::get_id()==main,"Progress ran on worker thread");
        require(p.evaluations<=o.maxEvaluations,"Reported reservation exceeded budget");++calls;
    });
    require(result.actualEvaluations<=o.maxEvaluations && result.referenceBuilds==10,"Global preparation/budget contract failed");
    require(result.baselines.size()==10 && calls==result.actualEvaluations,"Missing baseline or callback");
    std::size_t pairs=0;std::set<std::string> hardware;
    for(const auto& c:result.candidates) {
        std::ostringstream key;for(const auto& n:c.hardware) key<<n.nodeId<<':'<<int(n.monitorInput)<<':'<<n.bitIndex<<';';
        require(hardware.insert(key.str()).second,"Duplicate H was reevaluated");pairs+=c.evaluations.size();
        require((c.status=="complete")== (c.evaluations.size()==10),"Partial denominator disguised as complete");
        std::set<int> ids;for(const auto& e:c.evaluations) {
            require(ids.insert(e.imageId).second,"A candidate/image was calibrated twice");
            require(e.stats.referenceCacheHit && e.stats.baselineCacheHit,"Ten-image search rebuilt references/baselines");
            if(e.legal) {
                require(analysis::matchesCircuitFixedBitflipHardware(e.selected.configuration,c.hardware,app.addNodes().size()),"Fixed H drifted");
                const auto& x=data.at(std::size_t(e.imageId-1));
                equal(e.selected.metrics,analysis::measureBitflipQuality(app.runExact(x.image),app.runApprox(x.image,e.selected.configuration),x.mask));
            }
        }
    }
    require(pairs==result.actualEvaluations,"Actual calls and retained outcomes disagree");
    require(result.bestIndex && result.candidates[*result.bestIndex].evaluations.size()==10,"Incumbent must be complete");
    o.maxEvaluations=3;const auto partial=analysis::searchCircuitTenTrain(app,data,o);
    require(!partial.bestIndex && partial.actualEvaluations==3,"Partial search froze an incumbent");
    require(partial.candidates.front().status=="budget_incomplete" && partial.candidates.front().evaluations.size()==3,"Partial outcomes were fabricated");
    std::reverse(data.begin(),data.end());o.maxEvaluations=20;
    const auto reversed=analysis::searchCircuitTenTrain(app,data,o);
    require(result.candidates[*result.bestIndex].hardwareId==reversed.candidates[*reversed.bestIndex].hardwareId,"Input ordering changed frozen H");
    require(result.candidates[*result.bestIndex].meanRoiDrop==reversed.candidates[*reversed.bestIndex].meanRoiDrop,"Input order changed score accumulation");
    for(std::size_t i=0;i<result.candidates.size();++i) {
        const auto& a=result.candidates[i];const auto& b=reversed.candidates.at(i);
        require(!analysis::betterTenTrainCandidate(a,b) && !analysis::betterTenTrainCandidate(b,a),"Image order changed H, scores or rank");
        for(std::size_t j=0;j<a.evaluations.size();++j) {
            const auto& ac=a.evaluations[j].selected.configuration;const auto& bc=b.evaluations.at(j).selected.configuration;
            require(ac.size()==bc.size(),"Immutable seed replay changed configuration size");
            for(std::size_t n=0;n<ac.size();++n) require(ac[n].nodeId==bc[n].nodeId &&
                ac[n].monitorInput==bc[n].monitorInput && ac[n].bitIndex==bc[n].bitIndex &&
                ac[n].lower==bc[n].lower && ac[n].upper==bc[n].upper,"Immutable seed snapshot drifted");
        }
    }
}
void preparedBuildCounts() {
    class Counting final : public applications::ConvApplication {
    public:
        mutable std::size_t exactCalls=0,baselineCalls=0;
        cv::Mat runExact(const cv::Mat& x) const override {++exactCalls;return applications::ConvApplication::runExact(x);}
        cv::Mat runApprox(const cv::Mat& x,const std::vector<core::AttackConfig>& c) const override {
            if(c.empty()) ++baselineCalls;return applications::ConvApplication::runApprox(x,c);
        }
    } app;
    const auto r=analysis::searchCircuitTenTrain(app,images(),small());
    require(app.exactCalls==10 && app.baselineCalls==10 && r.referenceBuilds==10,"References or approximate baselines were rebuilt per H");
}
// Regression authored before the priority-mask fix. DCT's baseline collector
// assigns block ROI weights from nonzero mask pixels, whereas the fixed oracle
// uses mask >=128. Equivalent raw masks must feed identical binary priorities.
void priorityMaskThreshold() {
    class BinaryMaskDct final : public applications::DctApplication {
    public:
        void collectBaselineAddSamples(const cv::Mat& image,const cv::Mat& mask,
                std::vector<core::AddSample>& samples) const override {
            cv::Mat intermediate;cv::inRange(mask,cv::Scalar(1),cv::Scalar(254),intermediate);
            require(cv::countNonZero(intermediate)==0,"Priority trace received an unnormalized ROI mask");
            applications::DctApplication::collectBaselineAddSamples(image,mask,samples);
        }
    } app;
    auto zero=images();auto gray=images();
    for(std::size_t i=0;i<zero.size();++i) {
        cv::Mat image(16,16,CV_8UC1),mask(16,16,CV_8UC1,cv::Scalar(0));
        for(int r=0;r<16;++r) for(int c=0;c<16;++c)
            image.at<unsigned char>(r,c)=80+(r*7+c*11+int(i))%90;
        mask(cv::Rect(1,1,5,5)).setTo(255);zero[i].image=image;zero[i].mask=mask;
        gray[i].image=image.clone();gray[i].mask=mask.clone();
        for(int r=0;r<16;++r) for(int c=0;c<16;++c)
            if(gray[i].mask.at<unsigned char>(r,c)==0) gray[i].mask.at<unsigned char>(r,c)=127;
    }
    const auto a=analysis::searchCircuitTenTrain(app,zero,small());
    const auto b=analysis::searchCircuitTenTrain(app,gray,small());
    require(a.bestIndex==b.bestIndex && a.actualEvaluations==b.actualEvaluations &&
        a.candidates.size()==b.candidates.size() && a.stopReason==b.stopReason,"Equivalent masks changed search outcomes");
    for(std::size_t i=0;i<a.baselines.size();++i) equal(a.baselines[i],b.baselines.at(i));
    for(std::size_t i=0;i<a.candidates.size();++i) {
        const auto& ac=a.candidates[i];const auto& bc=b.candidates.at(i);
        require(!analysis::betterTenTrainCandidate(ac,bc) && !analysis::betterTenTrainCandidate(bc,ac) &&
            ac.hardwareId==bc.hardwareId && ac.status==bc.status && ac.reason==bc.reason &&
            ac.evaluations.size()==bc.evaluations.size(),"Equivalent masks changed candidate H or scores");
        for(std::size_t j=0;j<ac.evaluations.size();++j) {
            const auto& ae=ac.evaluations[j];const auto& be=bc.evaluations.at(j);
            require(ae.imageId==be.imageId && ae.legal==be.legal && ae.numericEligible==be.numericEligible,
                "Equivalent masks changed pair eligibility");
            equal(ae.baseline,be.baseline);if(!ae.legal) continue;
            equal(ae.selected.metrics,be.selected.metrics);
            const auto& ax=ae.selected.configuration;const auto& bx=be.selected.configuration;
            require(ax.size()==bx.size(),"Equivalent masks changed configuration size");
            for(std::size_t n=0;n<ax.size();++n) require(ax[n].nodeId==bx[n].nodeId &&
                ax[n].monitorInput==bx[n].monitorInput && ax[n].bitIndex==bx[n].bitIndex &&
                ax[n].unit==bx[n].unit && ax[n].lower==bx[n].lower && ax[n].upper==bx[n].upper,
                "Equivalent masks changed selected intervals");
            const auto image=std::size_t(ae.imageId-1);
            require(cv::norm(app.runApprox(zero[image].image,ax),app.runApprox(gray[image].image,bx),cv::NORM_INF)==0,
                "Equivalent masks changed original full-image output");
        }
    }
}
void validationAndDefaults() {
    const analysis::TenTrainOptions defaults;require(defaults.intervals.continuous && defaults.intervals.maxContexts==64,"Training must default to bounded continuous intervals");
    applications::SobelApplication app;auto data=images();data.back().imageId=9;bool rejected=false;
    try {analysis::searchCircuitTenTrain(app,data,small());}catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Training IDs must be exactly 1..10");
    data=images();data.back().imageId=31;rejected=false;
    try {analysis::searchCircuitTenTrain(app,data,small());}catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Held-out image entered hardware selection");
}
void rankingContract() {
    analysis::TenTrainCandidate a,b;a.status=b.status="complete";
    a.hardware={{2,core::MonitorSignal::BaselineOutput,2},{10,core::MonitorSignal::Input1,2}};
    b.hardware={{2,core::MonitorSignal::Input1,2},{10,core::MonitorSignal::Input1,2}};
    require(analysis::betterTenTrainCandidate(a,b),"Canonical monitor order must use monitor text");
    b.hardware=a.hardware;b.hardware[1].nodeId=9;
    require(analysis::betterTenTrainCandidate(b,a),"Canonical node ordering must compare integers");
    b=a;b.hardware[0].bitIndex=10;
    require(analysis::betterTenTrainCandidate(a,b),"Canonical bit ordering must compare integers");
    b=a;b.numericSuccesses=1;require(analysis::betterTenTrainCandidate(b,a),"Numeric success rank lost");
    a.status="budget_incomplete";a.numericSuccesses=10;
    require(analysis::betterTenTrainCandidate(b,a),"Partial candidate must not outrank complete candidate");
    a=b;a.worstRoiDrop=1;require(analysis::betterTenTrainCandidate(a,b),"Worst ROI score rank lost");
    b=a;b.meanRoiDrop=2;require(analysis::betterTenTrainCandidate(b,a),"Mean ROI score rank lost");
}
}
int main() {
    try {preparedIdentity();validationAndDefaults();rankingContract();preparedBuildCounts();priorityMaskThreshold();budgetAndOracle(applications::ConvApplication{});
        budgetAndOracle(applications::SobelApplication{});budgetAndOracle(applications::DctApplication{});
        std::cout<<"Ten-image native contracts passed\n";return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

#include "analysis/circuit_bitflip_ten_train.hpp"
#include "analysis/detail/ten_train_schedule.hpp"
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
    const analysis::TenTrainOptions scheduling;
    require(scheduling.scheduling=="interleaved" && scheduling.initialBatch==4 && scheduling.completionReserve==20,"Completion scheduling defaults drifted");
    const analysis::TenTrainOptions defaults;require(defaults.intervals.continuous && defaults.intervals.maxContexts==64,"Training must default to bounded continuous intervals");
    applications::SobelApplication app;auto data=images();data.back().imageId=9;bool rejected=false;
    try {analysis::searchCircuitTenTrain(app,data,small());}catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Training IDs must be exactly 1..10");
    data=images();data.back().imageId=31;rejected=false;
    try {analysis::searchCircuitTenTrain(app,data,small());}catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Held-out image entered hardware selection");
}
// Authored before scheduler implementation; intentionally not executed locally.
void schedulingBudgets() {
    applications::ConvApplication app;
    for(const std::size_t cap:{3,10,23}) {
        auto o=small();o.maxEvaluations=cap;
        const auto r=analysis::searchCircuitTenTrain(app,images(),o);
        require(r.actualEvaluations<=cap,"Completion exceeded hard cap");
        if(r.actualEvaluations==cap) require(r.stopReason=="evaluation_budget_exhausted","Creation stop reason masked terminal pair cap");
        require(r.referenceEvaluations==std::min<std::size_t>(10,cap),"Reference was not attempted in full");
        require(r.referenceEvaluations+r.explorationEvaluations+r.completionEvaluations==r.actualEvaluations,"Phase accounting drifted");
        const auto remaining=cap-std::min<std::size_t>(10,cap);
        require(r.effectiveCompletionReserve==std::min<std::size_t>(20,remaining>o.initialBatch?remaining-o.initialBatch:0),"Reserve clamp drifted");
        require(bool(r.bestIndex)==(cap>=10),"Partial reference froze a winner");
        std::vector<int> difficult{1,2,3,4,5,6,7,8,9,10};
        std::stable_sort(difficult.begin(),difficult.end(),[&](int a,int b) {
            return r.baselines[std::size_t(a-1)].roiMse>r.baselines[std::size_t(b-1)].roiMse;
        });
        const std::vector<std::size_t> positions{0,9,4,2,7,1,8,3,6,5};
        for(std::size_t i=0;i<10;++i) require(r.evaluationImageOrder[i]==difficult[positions[i]],"Difficulty order changed");
        for(const auto& c:r.candidates) {
            if(c.status=="budget_incomplete") require(c.reason==
                (r.actualEvaluations==cap?"pair_evaluation_budget_exhausted":"search_terminated_before_completion"),
                "Returned partial candidate retained a temporary suspension reason");
            int previous=0;for(const auto& e:c.evaluations) {
                require(e.imageId>previous,"Reported pair order is not canonical");previous=e.imageId;
                require(e.phase=="reference" || e.phase=="exploration" || e.phase=="completion","Missing phase");
            }
        }
    }
    auto o=small();o.scheduling="sequential";
    std::vector<std::pair<std::string,int>> trajectory;
    const auto r=analysis::searchCircuitTenTrain(app,images(),o,[&](const auto& p) {
        trajectory.emplace_back(p.hardwareId,p.imageId);
    });
    require(r.effectiveCompletionReserve==0 && r.completionEvaluations==0 && r.completionCandidates==0,"Legacy mode reserved completion work");
    require(r.evaluationImageOrder==std::vector<int>({1,2,3,4,5,6,7,8,9,10}),"Legacy image order changed");
    for(const auto& c:r.candidates) for(const auto& e:c.evaluations) require(e.phase=="sequential","Legacy phase changed");
    std::set<std::string> finished;std::string current;int previous=0;
    for(const auto& step:trajectory) {
        if(step.first!=current) {if(!current.empty()) finished.insert(current);current=step.first;previous=0;}
        require(!finished.count(current) && step.second==previous+1,"Sequential control resumed/interleaved a candidate");previous=step.second;
    }
    require(trajectory.size()>=10 && trajectory[9]==std::make_pair(std::string("ten_h1"),10),"Legacy reference trajectory changed");
}
void adversarialCompletion() {
    // A promising 8/10 challenger must finish before a weaker 4/10 row.
    analysis::TenTrainCandidate incumbent,challenger,other;
    incumbent.status="complete";incumbent.numericSuccesses=10;incumbent.worstRoiDrop=5;
    challenger.status=other.status="budget_incomplete";
    challenger.evaluations.resize(8);challenger.numericSuccesses=8;challenger.worstRoiDrop=7;
    other.evaluations.resize(4);other.numericSuccesses=4;other.worstRoiDrop=6;
    std::vector<analysis::TenTrainCandidate> rows{incumbent,challenger,other};
    std::size_t calls=78;const std::size_t cap=80;
    while(calls<cap) {
        const auto next=analysis::detail::completionChoice(rows,0,10);
        require(next && *next==1,"Promising near-complete row was stranded");
        auto& c=rows[*next];c.evaluations.emplace_back();++c.numericSuccesses;++calls;
        if(c.evaluations.size()==10) c.status="complete";
    }
    require(rows[1].status=="complete" && calls==cap,"Adversarial completion missed cap");
    challenger.numericSuccesses=7;
    require(analysis::detail::strictlyEliminated(challenger,incumbent,10),"Impossible success bound survived");
    challenger.numericSuccesses=8;challenger.worstRoiDrop=4;
    require(analysis::detail::strictlyEliminated(challenger,incumbent,10),"Strict worst bound survived");
    challenger.worstRoiDrop=5;challenger.meanRoiDrop=0;
    require(!analysis::detail::strictlyEliminated(challenger,incumbent,10),"Equality or partial mean incorrectly pruned");
    incumbent.numericSuccesses=9;challenger.worstRoiDrop=0;
    require(!analysis::detail::strictlyEliminated(challenger,incumbent,10),"Worst bound overrode larger success upper bound");
    // Timing is never priority evidence; deterministic contexts break ties.
    challenger.worstRoiDrop=other.worstRoiDrop=6;
    other.evaluations.resize(8);other.numericSuccesses=8;
    challenger.evaluations.front().stats.contextsBuilt=10;
    other.evaluations.front().stats.contextsBuilt=2;
    other.evaluations.front().elapsedSeconds=100000;
    rows={incumbent,challenger,other};
    require(analysis::detail::completionChoice(rows,0,10)==std::optional<std::size_t>(2),"Timing displaced deterministic context estimate");
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
    try {preparedIdentity();validationAndDefaults();schedulingBudgets();adversarialCompletion();rankingContract();preparedBuildCounts();priorityMaskThreshold();budgetAndOracle(applications::ConvApplication{});
        budgetAndOracle(applications::SobelApplication{});budgetAndOracle(applications::DctApplication{});
        std::cout<<"Ten-image native contracts passed\n";return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

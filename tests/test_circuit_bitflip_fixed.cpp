#include "analysis/circuit_bitflip_fixed.hpp"
#include "applications/conv.hpp"
#include "applications/sobel.hpp"
#include "applications/dct.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* text) {if(!value) throw std::runtime_error(text);}
void equal(const analysis::BitflipQuality& a,const analysis::BitflipQuality& b) {
    for(auto p:{std::pair<double,double>{a.globalMse,b.globalMse},{a.roiMse,b.roiMse},{a.nonRoiMse,b.nonRoiMse}})
        require(std::abs(p.first-p.second)<1e-8,"Fixed score disagrees with full original application");
}
analysis::CircuitFixedBitflipOptions small() {
    analysis::CircuitFixedBitflipOptions o;o.boundaryBudget=2;o.beamWidth=2;o.representativeCount=1;
    o.restarts=2;o.refinementRounds=0;o.maxContexts=16;o.maxFullEvaluations=32;
    o.fullCandidatesPerContext=2;o.globalPsnrThreshold=0;return o;
}
void oracleAndCoverage(const core::Application& app,std::size_t count) {
    cv::Mat image(9,10,CV_8UC1),mask(9,10,CV_8UC1,cv::Scalar(127));
    for(int r=0;r<image.rows;++r) for(int c=0;c<image.cols;++c)
        image.at<unsigned char>(r,c)=120+(r*3+c*5)%17;
    mask(cv::Rect(6,5,3,4)).setTo(128);
    analysis::FixedBitflipHardware h;
    for(std::size_t n=0;n<count;++n) h.push_back({int(n),core::MonitorSignal::BaselineOutput,0});
    const auto dormant=analysis::dormantCircuitFixedBitflipSeed(app,image,mask,h);
    require(bool(dormant),"Small fixture requires an in-domain dormant singleton");
    require(analysis::matchesCircuitFixedBitflipHardware(*dormant,h,app.addNodes().size()),"Dormant hardware changed");
    std::vector<core::AddSample> trace;app.collectConfiguredAddSamples(image,mask,*dormant,trace);
    if(app.addNodes().size()==32) require(trace.size()==4*16*32 &&
        std::count_if(trace.begin(),trace.end(),[](const auto& s){return s.nodeId==31;})==64,
        "DCT fixed hardware must include repeated calls in all padded blocks");
    for(const auto& s:trace) for(const auto& x:*dormant) if(s.nodeId==x.nodeId)
        require(s.baselineOutput<x.lower || s.baselineOutput>x.upper,"Dormant installed node must have zero hits");
    equal(analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,{}),mask),
        analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,*dormant),mask));
    auto o=small();o.maxContexts=count;o.maxFullEvaluations=count;o.seeds={*dormant};
    const auto frozen=o.seeds;
    const auto result=analysis::searchCircuitFixedBitflipIntervals(app,image,mask,h,o);
    require(result.stats.contextsByNode.size()==app.addNodes().size(),"Native coverage was truncated");
    for(const auto& n:h) require(result.stats.contextsByNode[n.nodeId]>0,"Node-first coverage starved an installed target");
    require(result.stats.traceCacheHits>=count-1,"Immutable seed trace was rebuilt for each installed target");
    require(result.stats.contextsBuilt<=o.maxContexts && result.stats.fullEvaluations<=o.maxFullEvaluations,"Search budget exceeded");
    require(result.bestLegal.has_value(),"Executable installed dormant hardware disappeared");
    require(result.stats.finalReplayEvaluations>0,"Final oracle replay must be counted separately");
    auto verify=[&](const auto& s) {
        require(analysis::matchesCircuitFixedBitflipHardware(s.configuration,h,app.addNodes().size()),"Search altered installed hardware");
        equal(s.metrics,analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,s.configuration),mask));
    };
    verify(*result.bestLegal);for(const auto& s:result.candidates) verify(s);
    require(o.seeds[0].size()==frozen[0].size(),"Seed bank mutated");
    for(std::size_t i=0;i<frozen[0].size();++i) require(o.seeds[0][i].lower==frozen[0][i].lower &&
        o.seeds[0][i].upper==frozen[0][i].upper,"Seed intervals mutated");
}
void branchesAbsAndSingleReference() {
    cv::Mat image(3,3,CV_8UC1,cv::Scalar(128)),mask(3,3,CV_8UC1,cv::Scalar(0));
    mask.at<unsigned char>(1,1)=255;image.at<unsigned char>(1,1)=129;
    const auto config=[](int n,int bit,core::MonitorSignal m,int lo,int hi) {
        return core::AttackConfig{n,approximate::ApproxUnitId::Add12se5RP,m,lo,hi,core::AttackPayload::BitFlip,bit};
    };
    const auto verify=[&](const core::Application& app,const analysis::FixedBitflipHardware& h,
            const analysis::JointBitflipConfiguration& seed) {
        auto o=small();o.maxContexts=2;o.seeds={seed};
        const auto result=analysis::searchCircuitFixedBitflipIntervals(app,image,mask,h,o);
        require(result.stats.contextsBuilt==2,"Both branch contexts must be built");
        require(result.bestLegal.has_value(),"Compensating multi-node seed lost");
        equal(result.bestLegal->metrics,analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,result.bestLegal->configuration),mask));
        for(const auto& s:result.candidates)
            equal(s.metrics,analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,s.configuration),mask));
    };
    applications::ConvApplication conv(applications::ConvApplication::createAllExactBaselineConfig());
    verify(conv,{{0,core::MonitorSignal::BaselineOutput,0},{4,core::MonitorSignal::Input1,0}},
        {config(0,0,core::MonitorSignal::BaselineOutput,0,0),config(4,0,core::MonitorSignal::Input1,1,1)});
    image.setTo(40);
    applications::SobelApplication sobel(applications::SobelApplication::createAllExactBaselineConfig());
    verify(sobel,{{0,core::MonitorSignal::BaselineOutput,4},{10,core::MonitorSignal::Input1,4}},
        {config(0,4,core::MonitorSignal::BaselineOutput,120,120),config(10,4,core::MonitorSignal::Input1,16,16)});
    class CountingConv final : public applications::ConvApplication {
    public:
        mutable std::size_t references=0;
        CountingConv():applications::ConvApplication(createAllExactBaselineConfig()) {}
        cv::Mat runExact(const cv::Mat& x) const override {++references;return applications::ConvApplication::runExact(x);}
    } counting;
    image.setTo(128);auto o=small();o.globalPsnrThreshold=1000;
    o.seeds={{config(0,4,core::MonitorSignal::BaselineOutput,-4096,4095),
        config(7,0,core::MonitorSignal::BaselineOutput,-4096,-4096)}};
    const auto failing=analysis::searchCircuitFixedBitflipIntervals(counting,image,mask,
        {{0,core::MonitorSignal::BaselineOutput,4},{7,core::MonitorSignal::BaselineOutput,0}},o);
    require(counting.references==1,"Exact reference must be computed once per image search");
    require(failing.stats.retainedQualityFailures>0,"Executable quality-failing states must remain searchable");
    require(failing.candidates.empty(),"Numeric eligibility ignored global floor");
}
void invalidRemovalRepair() {
    auto units=applications::ConvApplication::createAllExactBaselineConfig();units[6].exact=false;
    applications::ConvApplication app(units);
    cv::Mat image(3,3,CV_8UC1,cv::Scalar(128)),mask(3,3,CV_8UC1,cv::Scalar(0));
    image.at<unsigned char>(1,1)=255;mask.at<unsigned char>(1,1)=255;
    analysis::FixedBitflipHardware h{{2,core::MonitorSignal::BaselineOutput,11},{5,core::MonitorSignal::BaselineOutput,11}};
    analysis::JointBitflipConfiguration seed{
        {2,approximate::ApproxUnitId::Add12se5RP,core::MonitorSignal::BaselineOutput,-4096,4095,core::AttackPayload::BitFlip,11},
        {5,approximate::ApproxUnitId::Add12se5RP,core::MonitorSignal::BaselineOutput,3064,3064,core::AttackPayload::BitFlip,11}};
    auto bad=seed;bad[1].lower=3065;bad[1].upper=3065;
    auto o=small();o.seeds={bad,seed};o.maxContexts=2;
    const auto result=analysis::searchCircuitFixedBitflipIntervals(app,image,mask,h,o);
    require(result.stats.rejectedSeeds==1,"Precisely range-invalid warm seed must remain visible");
    require(result.bestLegal.has_value() && result.stats.rangeRejected>0,"Invalid off-path repair must be searched and range rejections visible");
    equal(result.bestLegal->metrics,analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,result.bestLegal->configuration),mask));
}
void malformedAndUnexpectedErrors() {
    applications::ConvApplication app;
    cv::Mat image(5,5,CV_8UC1,cv::Scalar(128)),mask(5,5,CV_8UC1,cv::Scalar(0));mask.at<unsigned char>(2,2)=255;
    analysis::FixedBitflipHardware h{{0,core::MonitorSignal::Input1,0},{7,core::MonitorSignal::BaselineOutput,1}};
    auto o=small();auto seed=*analysis::dormantCircuitFixedBitflipSeed(app,image,mask,h);
    seed[0].lower=-2049;o.seeds={seed};bool rejected=false;
    try {analysis::searchCircuitFixedBitflipIntervals(app,image,mask,h,o);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Malformed warm seeds must error instead of disappearing");
    class Broken final : public applications::ConvApplication {
        cv::Mat runApprox(const cv::Mat& x,const std::vector<core::AttackConfig>& c) const override {
            if(!c.empty()) throw std::runtime_error("Unexpected fixture oracle failure");
            return applications::ConvApplication::runApprox(x,c);
        }
    } broken;
    o.seeds.clear();bool propagated=false;
    try {analysis::searchCircuitFixedBitflipIntervals(broken,image,mask,h,o);}
    catch(const std::runtime_error& e) {if(std::string(e.what())!="Unexpected fixture oracle failure") throw;propagated=true;}
    require(propagated,"Unexpected runtime errors must propagate");
}
void dctRefinementAndHonestIncompleteCoverage() {
    applications::DctApplication app(applications::Dct8FixedGraph::createAllExactBaselineConfig());
    cv::Mat image(8,8,CV_8UC1),mask(8,8,CV_8UC1,cv::Scalar(0));
    for(int r=0;r<8;++r) for(int c=0;c<8;++c) image.at<unsigned char>(r,c)=120+(r*3+c*5)%17;
    mask(cv::Rect(1,1,3,4)).setTo(255);
    analysis::FixedBitflipHardware h{{0,core::MonitorSignal::BaselineOutput,2},{31,core::MonitorSignal::BaselineOutput,2}};
    auto o=small();o.maxContexts=24;o.maxFullEvaluations=24;
    bool refined=false,restarted=false;
    const auto result=analysis::searchCircuitFixedBitflipIntervals(app,image,mask,h,o,[&](const auto& p) {
        refined|=p.pass>0;restarted|=p.restart>0;
    });
    require(refined && restarted,"Initial coverage must leave work for refinement and alternate restart");
    require(result.stats.fullEvaluations<=o.maxFullEvaluations,"DCT complete scoring exceeded its budget");
    o.maxFullEvaluations=1;o.maxContexts=4;
    const auto incomplete=analysis::searchCircuitFixedBitflipIntervals(app,image,mask,h,o);
    require(incomplete.stats.fullBudgetExhausted,"Insufficient full score budget must be explicit");
    require(incomplete.stats.contextsByNode[0]>0 && incomplete.stats.contextsByNode[31]==0,
        "Insufficient budget must report actual incomplete native coverage");
    require(incomplete.stats.finalReplayEvaluations>0 && incomplete.stats.fullEvaluations==1,
        "Final verification must be separate from bounded search scores");
    class BrokenDct final : public applications::DctApplication {
    public:
        BrokenDct():applications::DctApplication(applications::Dct8FixedGraph::createAllExactBaselineConfig()) {}
        cv::Mat runApprox(const cv::Mat& x,const std::vector<core::AttackConfig>& c) const override {
            if(std::any_of(c.begin(),c.end(),[](const auto& n){return n.lower!=-4096 || n.upper!=-4096;}))
                throw std::runtime_error("Unexpected DCT full-score failure");
            return applications::DctApplication::runApprox(x,c);
        }
    } broken;
    bool propagated=false;
    try {analysis::searchCircuitFixedBitflipIntervals(broken,image,mask,h,o);}
    catch(const std::runtime_error& e) {
        if(std::string(e.what())!="Unexpected DCT full-score failure") throw;propagated=true;
    }
    require(propagated,"Unexpected full DCT score failure must not be classified as arithmetic range rejection");
}
}
int main() {
    try {
        oracleAndCoverage(applications::ConvApplication{},8);
        oracleAndCoverage(applications::SobelApplication{},11);
        oracleAndCoverage(applications::DctApplication{},32);
        branchesAbsAndSingleReference();invalidRemovalRepair();malformedAndUnexpectedErrors();
        dctRefinementAndHonestIncompleteCoverage();
        std::cout<<"Fixed circuit interval search contracts passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

#include "analysis/circuit_bitflip_joint.hpp"
#include "applications/conv.hpp"
#include "applications/sobel.hpp"
#include "applications/dct.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool ok,const char* text) {if(!ok) throw std::runtime_error(text);}
void close(double a,double b) {require(std::abs(a-b)<1e-8,"Conditional metrics differ from original execution");}
core::AttackConfig flip(int node,int bit,core::MonitorSignal m,int lo=-4096,int hi=4095) {
    return {node,approximate::ApproxUnitId::Add12se5RP,m,lo,hi,core::AttackPayload::BitFlip,bit};
}
void equal(const analysis::BitflipQuality& a,const analysis::BitflipQuality& b) {
    close(a.globalMse,b.globalMse);close(a.roiMse,b.roiMse);close(a.nonRoiMse,b.nonRoiMse);
}
bool rangeError(const std::runtime_error& e) {
    const std::string s=e.what();return s=="Value exceeds signed 12-bit range." ||
        s=="Bit-flip input exceeds signed 13-bit output range.";
}
void profilesMatchOracle(const core::Application& app) {
    cv::Mat image(5,6,CV_8UC1),mask(5,6,CV_8UC1,cv::Scalar(127));
    for(int r=0;r<image.rows;++r) for(int c=0;c<image.cols;++c)
        image.at<unsigned char>(r,c)=70+(r*11+c*17)%90;
    mask(cv::Rect(1,1,2,3)).setTo(128);
    const analysis::JointBitflipConfiguration current{
        flip(0,0,core::MonitorSignal::Input2),
        flip(int(app.addNodes().size())-1,1,core::MonitorSignal::BaselineOutput,-128,128)};
    for(int node=0;node<int(app.addNodes().size());++node) for(int bit:{0,4,11})
        for(auto m:{core::MonitorSignal::Input1,core::MonitorSignal::Input2,core::MonitorSignal::BaselineOutput}) {
            const auto p=analysis::profileCircuitBitflip(app,image,mask,current,node,bit,m);
            require(p.roiPixels==6 && p.nonRoiPixels==24,"Mask threshold must be >=128");
            for(const auto& entry:p.bins) {
                auto cfg=p.others;cfg.push_back(flip(node,bit,m,entry.first,entry.first));
                const auto score=analysis::scoreJointInterval(p,entry.first,entry.first);
                cv::Mat actual;
                try {actual=app.runApprox(image,cfg);} catch(const std::runtime_error& e) {
                    if(!rangeError(e)) throw;
                    require(!score.valid,"Profile accepted an illegal range");continue;
                }
                require(score.valid,"Profile rejected an executable circuit");
                equal(score.metrics,analysis::measureBitflipQuality(app.runExact(image),actual,mask));
            }
        }
}
void branchingAbsAndTriggers() {
    cv::Mat image(3,3,CV_8UC1,cv::Scalar(128)),mask(3,3,CV_8UC1,cv::Scalar(0));
    mask.at<unsigned char>(1,1)=255;image.at<unsigned char>(1,1)=129;
    applications::ConvApplication conv(applications::ConvApplication::createAllExactBaselineConfig());
    const auto compensate=flip(4,0,core::MonitorSignal::Input1,1,1);
    const auto cp=analysis::profileCircuitBitflip(conv,image,mask,{compensate},0,0,core::MonitorSignal::BaselineOutput);
    const auto cs=analysis::scoreJointInterval(cp,0,0);
    require(cs.valid && cs.roiTriggers==1,"Conv target must trigger");close(cs.metrics.roiMse,0);
    applications::SobelApplication sobel(applications::SobelApplication::createAllExactBaselineConfig());
    image.setTo(40);
    const auto absCompensate=flip(10,4,core::MonitorSignal::Input1,16,16);
    const auto sp=analysis::profileCircuitBitflip(sobel,image,mask,{absCompensate},0,4,core::MonitorSignal::BaselineOutput);
    const auto ss=analysis::scoreJointInterval(sp,120,120);
    require(ss.valid && ss.roiTriggers==1,"Sobel target must trigger");close(ss.metrics.roiMse,0);
}
void removalMayBeInvalid() {
    auto units=applications::ConvApplication::createAllExactBaselineConfig();units[6].exact=false;
    applications::ConvApplication app(units);
    cv::Mat image(3,3,CV_8UC1,cv::Scalar(128)),mask(3,3,CV_8UC1,cv::Scalar(0));
    image.at<unsigned char>(1,1)=255;mask.at<unsigned char>(1,1)=255;
    const auto upstream=flip(2,11,core::MonitorSignal::BaselineOutput);
    const auto repair=flip(5,11,core::MonitorSignal::BaselineOutput);
    const auto current=app.runApprox(image,{upstream,repair});
    bool invalid=false;try {app.runApprox(image,{upstream});} catch(const std::runtime_error& e) {
        if(!rangeError(e)) throw;invalid=true;
    }
    require(invalid,"Fixture must have an invalid target-removal counterfactual");
    const auto p=analysis::profileCircuitBitflip(app,image,mask,{upstream,repair},5,11,core::MonitorSignal::BaselineOutput);
    require(p.offInvalid==1,"Invalid removal must stay in the profile");
    require(!analysis::scoreJointInterval(p,0,0).valid,"Missing repair must remain invalid");
    const auto s=analysis::scoreJointInterval(p,3064,3064);require(s.valid,"Repair interval must remain searchable");
    equal(s.metrics,analysis::measureBitflipQuality(app.runExact(image),current,mask));
}
void nativeNodeCountsAndQualityFailures() {
    applications::ConvApplication app(applications::ConvApplication::createAllExactBaselineConfig());
    cv::Mat image(3,3,CV_8UC1,cv::Scalar(128)),mask(3,3,CV_8UC1,cv::Scalar(0));
    image.at<unsigned char>(1,1)=129;mask.at<unsigned char>(1,1)=255;
    analysis::CircuitBitflipOptions o;o.bits={0};o.boundaryBudget=2;o.beamWidth=1;
    o.representativeCount=1;o.refinementRounds=0;o.restarts=1;o.globalPsnrThreshold=0;
    auto result=analysis::searchCircuitBitflips(app,image,mask,o);
    bool many=false;for(const auto& s:result.candidates) {
        many|=s.configuration.size()>4;
        equal(s.metrics,analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,s.configuration),mask));
    }
    require(many,"Native-count search must reach configurations larger than four nodes");
    // No background or delta-selectivity gate: both regions may deteriorate.
    cv::Mat larger(5,5,CV_8UC1,cv::Scalar(128)),roi(5,5,CV_8UC1,cv::Scalar(0));
    roi.at<unsigned char>(2,2)=255;o.globalPsnrThreshold=1000;
    result=analysis::searchCircuitBitflips(app,larger,roi,o);
    require(result.stats.retainedQualityFailures>0,"Quality-failing valid intermediate states must survive");
    require(result.candidates.empty(),"Final PSNR threshold must hold");
    o.globalPsnrThreshold=0;o.maxActiveNodes=2;
    result=analysis::searchCircuitBitflips(app,larger,roi,o);
    bool backgroundDamage=false;for(const auto& s:result.candidates) {
        require(s.configuration.size()<=2,"Explicit max-active cap must hold");
        backgroundDamage|=s.metrics.nonRoiMse>result.baseline.nonRoiMse;
    }
    require(backgroundDamage,"No unrequested background-improvement gate may suppress candidates");
    applications::ConvApplication approximateBaseline;
    for(int r=0;r<larger.rows;++r) for(int c=0;c<larger.cols;++c)
        larger.at<unsigned char>(r,c)=40+(r*19+c*31)%140;
    o.globalPsnrThreshold=1000;
    result=analysis::searchCircuitBitflips(approximateBaseline,larger,roi,o);
    require(result.baseline.globalPsnr()<o.globalPsnrThreshold,"Fixture needs a subthreshold original baseline");
    require(result.stats.contextsBuilt>0 && result.stats.retainedQualityFailures>0,
        "An originally subthreshold baseline must still seed compensation search");
}
void dctUsesFullReconstructionAndBoundedBudgets() {
    applications::DctApplication app(applications::Dct8FixedGraph::createAllExactBaselineConfig());
    cv::Mat image(9,10,CV_8UC1),mask(9,10,CV_8UC1,cv::Scalar(127));
    for(int r=0;r<9;++r) for(int c=0;c<10;++c) image.at<unsigned char>(r,c)=120+(r*3+c*5)%17;
    mask(cv::Rect(6,5,3,4)).setTo(128); // ROI crosses padded row/column block boundaries.
    cv::Mat binary;cv::compare(mask,127,binary,cv::CMP_GT);
    std::vector<core::AddSample> trace;app.collectConfiguredAddSamples(image,binary,{},trace);
    require(trace.size()==4*16*32,"DCT must include all repeated row and column calls in padded blocks");
    require(std::count_if(trace.begin(),trace.end(),[](const auto& s){return s.nodeId==0;})==4*16,
        "Every DCT static node must repeat sixteen times per padded block");
    bool rejected=false;try {
        analysis::profileCircuitBitflip(app,image,mask,{},0,0,core::MonitorSignal::Input1);
    } catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"DCT must reject additive per-call image profiles");
    analysis::CircuitBitflipOptions o;o.bits={2,3};o.boundaryBudget=3;o.beamWidth=1;
    o.representativeCount=1;o.refinementRounds=0;o.restarts=2;o.globalPsnrThreshold=0;
    o.maxContexts=12;o.maxFullEvaluations=97;o.dctFullCandidates=4;
    std::set<std::size_t> restarts;
    auto result=analysis::searchCircuitBitflips(app,image,mask,o,[&](const auto& p){restarts.insert(p.restart);});
    require(result.stats.fullEvaluations>1 && result.stats.fullEvaluations<=o.maxFullEvaluations,"DCT full replay cap must hold");
    require(result.stats.contextsBuilt<=o.maxContexts,"Context cap must hold");
    require(result.stats.contextBudgetExhausted || result.stats.fullBudgetExhausted,"Bounded incomplete search must report exhaustion");
    require(restarts.count(0) && restarts.count(1),"Forward order must not starve reverse restart");
    require(!result.candidates.empty(),"DCT fixture must exercise retained joint candidates");
    for(const auto& state:result.candidates) {
        require(state.configuration.size()>=2,"Candidate must be multi-node");
        equal(state.metrics,analysis::measureBitflipQuality(app.runExact(image),app.runApprox(image,state.configuration),mask));
    }
    o.maxContexts=1;o.maxFullEvaluations=1;
    result=analysis::searchCircuitBitflips(app,image,mask,o);
    require(result.stats.fullEvaluations==1 && result.stats.fullBudgetExhausted,"Baseline consumes the one full evaluation budget");
}
void unexpectedOracleErrorsPropagate() {
    class BrokenDct final : public applications::DctApplication {
    public:
        cv::Mat runApprox(const cv::Mat& image,const std::vector<core::AttackConfig>& config) const override {
            if(!config.empty()) throw std::runtime_error("Unexpected oracle failure");
            return applications::DctApplication::runApprox(image,config);
        }
    } app;
    cv::Mat image(8,8,CV_8UC1,cv::Scalar(128)),mask(8,8,CV_8UC1,cv::Scalar(0));
    mask.at<unsigned char>(0,0)=255;
    analysis::CircuitBitflipOptions o;o.bits={0};o.restarts=1;o.maxContexts=1;o.maxFullEvaluations=2;
    bool propagated=false;try {analysis::searchCircuitBitflips(app,image,mask,o);}
    catch(const std::runtime_error& e) {if(std::string(e.what())!="Unexpected oracle failure") throw;propagated=true;}
    require(propagated,"Unexpected oracle errors must not become range rejections");
    o.maxContexts=0;bool rejected=false;try {analysis::searchCircuitBitflips(app,image,mask,o);}
    catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Zero context budget must be rejected");
    o.maxContexts=1;o.maxFullEvaluations=0;rejected=false;
    try {analysis::searchCircuitBitflips(app,image,mask,o);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Zero full-evaluation budget must be rejected");
}
}
int main() {
    try {
        profilesMatchOracle(applications::ConvApplication{});profilesMatchOracle(applications::SobelApplication{});
        branchingAbsAndTriggers();removalMayBeInvalid();nativeNodeCountsAndQualityFailures();
        dctUsesFullReconstructionAndBoundedBudgets();unexpectedOracleErrorsPropagate();
        std::cout<<"Circuit conditional profiles and bounded oracle search contracts passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

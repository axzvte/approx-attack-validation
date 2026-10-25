#include "analysis/bitflip_sensitivity.hpp"
#include "analysis/sharpen_bitflip_joint.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void normalizedSeparationAndStableCoverage() {
    std::vector<core::AddSample> samples;
    for(int pixel=0;pixel<1000;++pixel) for(int node=0;node<4;++node) {
        const bool roi=pixel%5==0;
        samples.push_back({node,node==2?(roi?100:-100):0,0,64,roi?1.0:0.0,64});
    }
    const auto first=analysis::prioritizeBitflipNodes(samples,4,{0,4,8},16);
    const auto second=analysis::prioritizeBitflipNodes(samples,4,{0,4,8},16);
    require(first.order==second.order,"Sensitivity ordering must be deterministic");
    require(first.order.front()==2,"ROI/background signal separation must prioritize the distinguishable node");
    require(first.separability.at(2)>0.99,"Signal distributions must be normalized separately by region");
    require(first.separability.at(0)==0,"Identical ROI/background distributions must have zero separation");
    require(std::set<int>(first.order.begin(),first.order.end())==std::set<int>({0,1,2,3}),
        "Low priority nodes must remain available for joint compensation/exploration");
    for(const auto count:first.sampledPerNode) require(count<=32,"Sampling must respect both stratified caps");
    require(first.order.at(1)==0 && first.order.at(2)==1 && first.order.at(3)==3,
        "Priority ties must use deterministic native node IDs");
}
void fractionalAndMissingRegionsAreDiagnostics() {
    const std::vector<core::AddSample> samples{{0,10,20,30,0.25,30},{1,0,0,10,1.0,10}};
    const auto result=analysis::prioritizeBitflipNodes(samples,3,{3},8);
    require(result.order.size()==3,"Unobserved native nodes must not disappear");
    require(result.separability[0]==0 && result.separability[1]==0 && result.separability[2]==0,
        "Single identical fractional call or missing regions cannot imply separation");
    bool rejected=false;
    try {analysis::prioritizeBitflipNodes({{3,0,0,0,1.0,0}},3,{0},8);}
    catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Out-of-native trace nodes must be rejected");
}
void sharpenConditionalCoverageAndOracle() {
    applications::SharpenApplication app;
    cv::Mat image(5,5,CV_8UC1),mask(5,5,CV_8UC1,cv::Scalar(0));
    for(int r=0;r<5;++r) for(int c=0;c<5;++c) image.at<unsigned char>(r,c)=40+(r*19+c*31)%140;
    mask(cv::Rect(1,1,2,2)).setTo(255);
    analysis::JointBitflipOptions options;options.bits={0,2,3};options.beamWidth=2;options.boundaryBudget=4;
    options.maxContexts=24;options.sensitivityOrder=true;options.globalPsnrThreshold=0;
    const auto result=analysis::searchJointBitflips(app,image,mask,options);
    require(result.stats.sensitivityOrder.size()==4,"Sensitivity diagnostics must expose all native nodes");
    require(result.stats.traceCacheHits>=4,"Preanalysis and Sharpen bootstrap must reuse the same baseline trace");
    require(result.stats.traceBuilds<=result.stats.contextsBuilt+1,"Single-trace preparation must not rebuild per bit or signal");
    for(const auto contexts:result.stats.contextsByNode) require(contexts>=1,"Priority must retain Sharpen bootstrap coverage");
    const auto exact=app.runExact(image);
    for(const auto& state:result.candidates) {
        const auto actual=analysis::measureBitflipQuality(exact,app.runApprox(image,state.configuration),mask);
        require(std::abs(actual.globalMse-state.metrics.globalMse)<1e-8 && std::abs(actual.roiMse-state.metrics.roiMse)<1e-8,
            "Sensitivity ordering cannot replace full conditional scoring");
    }
}
}
int main() {
    try {normalizedSeparationAndStableCoverage();fractionalAndMissingRegionsAreDiagnostics();sharpenConditionalCoverageAndOracle();
        std::cout<<"Bitflip sensitivity ordering, sampling and conditional coverage verified\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

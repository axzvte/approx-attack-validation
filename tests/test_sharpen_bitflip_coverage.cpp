#include "analysis/sharpen_bitflip_joint.hpp"
#include <opencv2/core.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
struct Fixture {
    applications::SharpenApplication app;
    cv::Mat image{5,5,CV_8UC1},mask{5,5,CV_8UC1,cv::Scalar(0)};
    Fixture() {
        for(int r=0;r<5;++r) for(int c=0;c<5;++c)
            image.at<unsigned char>(r,c)=40+((r*19+c*31)%140);
        mask(cv::Rect(1,1,2,2)).setTo(255);
    }
};
analysis::JointBitflipOptions smallOptions() {
    analysis::JointBitflipOptions o;o.bits={0,2,3};o.beamWidth=2;o.boundaryBudget=4;
    o.refinementRounds=1;o.restarts=2;o.globalPsnrThreshold=0;return o;
}
void boundedBaselineCoverageBeforeGrowth() {
    Fixture f;auto o=smallOptions();o.maxContexts=4;
    std::vector<analysis::JointBitflipProgress> calls;
    const auto result=analysis::searchJointBitflips(f.app,f.image,f.mask,o,[&](const auto& p){calls.push_back(p);});
    require(result.stats.contextsBuilt==4,"A four-context budget must profile every native node once");
    require(result.stats.contextBudgetExhausted,"Pending growth must report exhausted context budget");
    require(calls.size()>=4,"Bootstrap must publish progress for all four nodes");
    for(std::size_t n=0;n<4;++n) {
        require(result.stats.contextsByNode[n]==1,"No node may consume a second context before all-node bootstrap");
        require(calls[n].node==n && calls[n].stats.contextsBuilt==n+1,"Bootstrap progress must report actual all-node coverage");
    }
    require(result.candidates.empty(),"Independent baseline profiles cannot masquerade as joint candidates");
}
void boundedGrowthUsesConditionalScores() {
    Fixture f;auto o=smallOptions();o.maxContexts=24;
    const auto result=analysis::searchJointBitflips(f.app,f.image,f.mask,o);
    require(result.stats.contextsBuilt<=o.maxContexts,"Context count exceeds hard budget");
    std::uint64_t sum=0;for(auto n:result.stats.contextsByNode) {require(n>=1,"Bounded growth lost native-node coverage");sum+=n;}
    require(sum==result.stats.contextsBuilt,"Per-node contexts must sum to total");
    require(result.stats.cacheHits>0,"Restart/bootstrap reuse must use the context cache");
    require(!result.candidates.empty(),"Fixture must retain a conditionally evaluated multi-node candidate");
    const auto exact=f.app.runExact(f.image);
    for(const auto& state:result.candidates) {
        const auto q=analysis::measureBitflipQuality(exact,f.app.runApprox(f.image,state.configuration),f.mask);
        require(std::abs(q.globalMse-state.metrics.globalMse)<1e-8 &&
            std::abs(q.roiMse-state.metrics.roiMse)<1e-8 && std::abs(q.nonRoiMse-state.metrics.nonRoiMse)<1e-8,
            "Bounded search must score the complete conditional circuit");
    }
}
void restartContextSharesCannotBeSpentByOneOrder() {
    Fixture f;auto o=smallOptions();o.maxContexts=6;
    std::array<std::uint64_t,2> used{};std::uint64_t previous=4;
    const auto result=analysis::searchJointBitflips(f.app,f.image,f.mask,o,[&](const auto& p) {
        if(p.stats.contextsBuilt>previous) {used.at(p.restart)+=p.stats.contextsBuilt-previous;previous=p.stats.contextsBuilt;}
    });
    require(result.stats.contextsBuilt==6 && used[0]==1 && used[1]==1,
        "Forward and reverse restart must each receive their remaining-context share");
}
void budgetValidationAndLegacyDefault() {
    Fixture f;auto o=smallOptions();require(o.maxContexts==0,"Default search must remain unlimited");
    for(std::size_t cap:{1,2,3}) {
        o.maxContexts=cap;bool rejected=false;
        try {analysis::searchJointBitflips(f.app,f.image,f.mask,o);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Budgets below native-node count must be rejected");
    }
    o.maxContexts=0;std::vector<std::size_t> initialNodes;
    const auto result=analysis::searchJointBitflips(f.app,f.image,f.mask,o,[&](const auto& p) {
        if(initialNodes.size()<2) initialNodes.push_back(p.node);
    });
    require(result.stats.contextsBuilt>4 && !result.stats.contextBudgetExhausted,"Unlimited legacy search must not stop at bootstrap budget");
    require(initialNodes.size()==2 && initialNodes[0]==0 && initialNodes[1]==1,"Unlimited mode must preserve legacy sequential visitation");
    o.maxContexts=100000;
    const auto generous=analysis::searchJointBitflips(f.app,f.image,f.mask,o);
    require(!generous.stats.contextBudgetExhausted,"A generous bound must not report exhaustion");
}
}
int main() {
    try {
        boundedBaselineCoverageBeforeGrowth();boundedGrowthUsesConditionalScores();
        restartContextSharesCannotBeSpentByOneOrder();budgetValidationAndLegacyDefault();
        std::cout<<"Joint bounded coverage, conditional scoring and legacy defaults verified\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

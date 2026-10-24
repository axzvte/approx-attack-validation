#include "analysis/sharpen_bitflip_joint.hpp"
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void close(double a,double b) {require(std::abs(a-b)<1e-8,"Conditional score differs from full application");}
core::AttackConfig flip(int node,int bit,core::MonitorSignal monitor,int lo,int hi) {
    return {node,approximate::ApproxUnitId::Add12se5RP,monitor,lo,hi,core::AttackPayload::BitFlip,bit};
}

void downstreamTriggerMustBeRecomputed() {
    applications::SharpenApplication app(applications::SharpenApplication::createAllExactBaselineConfig());
    cv::Mat image(3,3,CV_8UC1,cv::Scalar(40)),mask(3,3,CV_8UC1,cv::Scalar(0));
    mask.at<unsigned char>(1,1)=255;
    // A1 +16 changes A4's monitored input from 80 to 96. A4 then flips -16.
    const auto downstream=flip(3,4,core::MonitorSignal::Input1,96,96);
    auto profile=analysis::profileJointBitflip(app,image,mask,{downstream},0,4,core::MonitorSignal::Input1);
    const auto q=analysis::scoreJointInterval(profile,200,200);
    require(q.valid && q.roiTriggers==1,"Joint interval must trigger on the target's current input");
    close(q.metrics.roiMse,0);
    const auto full=app.runApprox(image,{flip(0,4,core::MonitorSignal::Input1,200,200),downstream});
    require(full.at<unsigned char>(1,1)==40,"Fixture must exercise downstream compensation");
}

void intervalCanRepairInvalidRemovalCounterfactual() {
    auto cfg=applications::SharpenApplication::createAllExactBaselineConfig();cfg[3].exact=false;
    applications::SharpenApplication app(cfg);
    cv::Mat image(3,3,CV_8UC1,cv::Scalar(0)),mask(3,3,CV_8UC1,cv::Scalar(0));
    image.at<unsigned char>(0,1)=255;image.at<unsigned char>(2,1)=255;
    image.at<unsigned char>(1,0)=255;mask.at<unsigned char>(1,1)=255;
    const auto a=flip(0,10,core::MonitorSignal::Input1,-4096,4095);
    const auto b=flip(1,9,core::MonitorSignal::Input1,-4096,4095);
    const auto c=flip(2,11,core::MonitorSignal::Input1,-4096,4095);
    // -255 -> A1 -1279 -> A2 raw -1534, flipped -2046 -> A3 raw
    // -2301, flipped -253: legal at approximate A4. Removing A2 makes
    // A3 raw -1789, flipped -3837: illegal at A4 until A2 is restored.
    app.runApprox(image,{a,b,c});
    bool rejected=false;try {app.runApprox(image,{a,c});} catch(const std::runtime_error&) {rejected=true;}
    require(rejected,"Counterfactual fixture must exceed downstream input width");
    const auto profile=analysis::profileJointBitflip(app,image,mask,{a,b,c},1,9,core::MonitorSignal::BaselineOutput);
    require(profile.offInvalid==1,"Invalid off path must not cause premature candidate loss");
    require(!analysis::scoreJointInterval(profile,0,0).valid,"Untriggered invalid pixels remain invalid");
    const auto repaired=analysis::scoreJointInterval(profile,-1534,-1534);
    require(repaired.valid,"A valid flip must repair the invalid node-removal counterfactual");
    const auto exact=app.runExact(image),actual=app.runApprox(image,{a,b,c});
    const auto expected=analysis::measureBitflipQuality(exact,actual,mask);
    close(repaired.metrics.globalMse,expected.globalMse);
}

void conditionedProfilesMatchApplication() {
    applications::SharpenApplication app;
    for(int center:{0,43,153,255}) {
        cv::Mat image(3,3,CV_8UC1,cv::Scalar(71)),mask(3,3,CV_8UC1,cv::Scalar(0));
        image.at<unsigned char>(1,1)=center;mask.at<unsigned char>(1,1)=255;
        const std::vector<core::AttackConfig> current{
            flip(1,0,core::MonitorSignal::Input2,-4096,4095),
            flip(3,2,core::MonitorSignal::BaselineOutput,-200,200)};
        for(int node=0;node<4;++node) for(int bit:{0,2,9,11})
            for(auto monitor:{core::MonitorSignal::Input1,core::MonitorSignal::Input2,core::MonitorSignal::BaselineOutput}) {
                const auto profile=analysis::profileJointBitflip(app,image,mask,current,node,bit,monitor);
                for(const auto& entry:profile.bins) {
                    auto config=current;
                    config.erase(std::remove_if(config.begin(),config.end(),[&](const auto& x){return x.nodeId==node;}),config.end());
                    config.push_back(flip(node,bit,monitor,entry.first,entry.first));
                    const auto scored=analysis::scoreJointInterval(profile,entry.first,entry.first);
                    cv::Mat output;
                    try {output=app.runApprox(image,config);} catch(const std::runtime_error&) {
                        require(!scored.valid,"Profile accepted an illegal complete circuit");continue;
                    }
                    require(scored.valid,"Profile rejected a legal complete circuit");
                    const auto q=analysis::measureBitflipQuality(app.runExact(image),output,mask);
                    close(q.globalMse,scored.metrics.globalMse);close(q.roiMse,scored.metrics.roiMse);
                    close(q.nonRoiMse,scored.metrics.nonRoiMse);
                }
            }
    }
}

void searchRetainsMultiNodeAndIntermediateQualityFailures() {
    applications::SharpenApplication app;
    cv::Mat image(5,5,CV_8UC1),mask(5,5,CV_8UC1,cv::Scalar(0));
    for(int r=0;r<5;++r) for(int c=0;c<5;++c) image.at<unsigned char>(r,c)=40+((r*19+c*31)%140);
    mask(cv::Rect(1,1,2,2)).setTo(255);
    analysis::JointBitflipOptions options;options.bits={0,2,3};options.beamWidth=2;
    options.boundaryBudget=4;options.refinementRounds=1;options.restarts=2;
    options.globalPsnrThreshold=0;
    const auto result=analysis::searchJointBitflips(app,image,mask,options);
    bool multi=false;
    for(const auto& state:result.candidates) {
        multi|=state.configuration.size()>=2;
        const auto full=app.runApprox(image,state.configuration);
        const auto q=analysis::measureBitflipQuality(app.runExact(image),full,mask);
        close(q.globalMse,state.metrics.globalMse);close(q.roiMse,state.metrics.roiMse);
    }
    require(multi,"Joint search must expose configurations with multiple nodes");
    options.globalPsnrThreshold=100;
    const auto infeasible=analysis::searchJointBitflips(app,image,mask,options);
    require(infeasible.stats.retainedQualityFailures>0,"Intermediate states must not be gated by final PSNR");
    require(infeasible.candidates.empty(),"No numeric candidate may be replaced with a baseline success");
}
}
int main() {
    try {
        downstreamTriggerMustBeRecomputed();intervalCanRepairInvalidRemovalCounterfactual();
        conditionedProfilesMatchApplication();searchRetainsMultiNodeAndIntermediateQualityFailures();
        std::cout<<"Joint bit-flip profiles, counterfactual repair and search contracts passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

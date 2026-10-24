#include "applications/conv.hpp"
#include "applications/sobel.hpp"
#include "applications/dct.hpp"
#include "core/output_bitflip.hpp"
#include <opencv2/core.hpp>
#include <iostream>
#include <stdexcept>
#include <limits>

// User-run only. Catch using an alternate FU, missing downstream propagation,
// wrong signed decoding and accepting invalid configuration on a non-hit path.
namespace {
void require(bool ok,const char* msg) {if(!ok) throw std::runtime_error(msg);}
template<class F> void rejects(F f) {bool bad=false;try {f();}catch(const std::runtime_error&){bad=true;}require(bad,"Expected rejection");}
core::AttackConfig flip(int node,int bit,core::MonitorSignal m,int lo,int hi) {
    return {node,approximate::ApproxUnitId::Add12se5Z0,m,lo,hi,core::AttackPayload::BitFlip,bit};
}
void spatialApplications() {
    applications::ConvApplication conv(applications::ConvApplication::createAllExactBaselineConfig());
    applications::SobelApplication sobel(applications::SobelApplication::createAllExactBaselineConfig());
    cv::Mat image(5,5,CV_8UC1,cv::Scalar(0)),mask(5,5,CV_8UC1,cv::Scalar(255));
    for(const core::Application* app:{static_cast<const core::Application*>(&conv),static_cast<const core::Application*>(&sobel)}) {
        auto input=image.clone();if(app==&conv) input.setTo(128);
        const auto base=app->runApprox(input,{});
        for(auto signal:{core::MonitorSignal::Input1,core::MonitorSignal::Input2,core::MonitorSignal::BaselineOutput}) {
            const auto hit=flip(0,4,signal,0,0);
            auto out=app->runApprox(input,{hit});
            require(out.at<unsigned char>(2,2)==16,"Exact zero node flip must propagate to magnitude output");
            auto miss=hit;miss.lower=1;miss.upper=1;
            require(cv::countNonZero(app->runApprox(input,{miss})!=base)==0,"Miss changed original baseline");
            std::vector<core::AddSample> trace;app->collectConfiguredAddSamples(input,mask,{hit},trace);
            require(trace[0].baselineOutput==0 && trace[0].output==16,"Flip must follow original exact unit");
            const int downstream=app==&conv?4:1;
            require(trace[downstream].input1==16,"Downstream input did not change");
            auto second=flip(downstream,4,core::MonitorSignal::Input1,16,16);
            require(app->runApprox(input,{hit,second}).at<unsigned char>(2,2)==0,"Changed downstream trigger not reevaluated");
        }
        auto legacy=flip(0,4,core::MonitorSignal::Input1,0,0);legacy.payload=core::AttackPayload::ApproximateUnit;
        std::vector<core::AddSample> old;app->collectConfiguredAddSamples(input,mask,{legacy},old);
        require(old[0].baselineOutput==0 && old[0].output==approximate::addSigned12(0,0,legacy.unit),"Legacy spatial payload changed");
        for(int bit:{-1,12,31}) rejects([&]{app->runApprox(input,{flip(0,bit,core::MonitorSignal::Input1,9000,9001)});});
        rejects([&]{auto f=flip(0,1,core::MonitorSignal::Input1,0,0);app->runApprox(input,{f,f});});
        rejects([&]{app->runApprox(input,{flip(-1,1,core::MonitorSignal::Input1,0,0)});});
        rejects([&]{app->runApprox(input,{flip(0,1,core::MonitorSignal::Input1,2,1)});});
        rejects([&]{app->runApprox(input,{flip(0,1,static_cast<core::MonitorSignal>(99),0,0)});});
    }
    // C1 exact output -2; bit0 toggles signed -2 to -1, not to a large unsigned value.
    image.setTo(129);std::vector<core::AddSample> negative;
    conv.collectConfiguredAddSamples(image,mask,{flip(0,0,core::MonitorSignal::BaselineOutput,-2,-2)},negative);
    require(negative[0].baselineOutput==-2 && negative[0].output==-1,"Negative signed13 decoding broken");
    // A 13-bit result cannot be silently wrapped into a following signed12 FU.
    applications::ConvApplication approx;
    image.setTo(128);
    rejects([&]{approx.runApprox(image,{flip(0,11,core::MonitorSignal::Input1,0,0)});});
    applications::SobelApplication approximateSobel;
    image.setTo(255);
    rejects([&]{approximateSobel.runApprox(image,{flip(0,11,core::MonitorSignal::Input1,255,255)});});
    image.setTo(128);
    // Legacy replacement is still computed from the selected FU, not the bit payload.
    auto legacy=flip(0,4,core::MonitorSignal::Input1,0,0);legacy.payload=core::AttackPayload::ApproximateUnit;
    std::vector<core::AddSample> old;conv.collectConfiguredAddSamples(image,mask,{legacy},old);
    require(old[0].output==approximate::addSigned12(0,0,legacy.unit),"Legacy payload changed");
    for(const core::Application* app:{static_cast<const core::Application*>(&approx),static_cast<const core::Application*>(&approximateSobel)}) {
        cv::Mat small(5,5,CV_8UC1,cv::Scalar(20));std::vector<core::AddSample> normal,changed;
        app->collectBaselineAddSamples(small,mask,normal);
        const int node=app==&approx?1:0;
        const auto f=flip(node,0,core::MonitorSignal::BaselineOutput,normal[node].baselineOutput,normal[node].baselineOutput);
        app->collectConfiguredAddSamples(small,mask,{f},changed);
        require(changed[node].output==core::flipNonSignOutputBit(normal[node].baselineOutput,0),"Approximate normal unit not retained");
    }
}
void dctGraphAndReuse() {
    applications::Dct8FixedGraph graph(applications::Dct8FixedGraph::createAllExactBaselineConfig());
    applications::Dct8FixedGraph::Vector zero{};
    applications::Dct8FixedGraph::Trace trace{};
    graph.runApprox(zero,{flip(0,4,core::MonitorSignal::Input1,0,0)},&trace);
    require(trace[0].baselineOutput==0 && trace[0].output==16,"DCT must flip exact normal output");
    require(trace[8].input1==6,"DCT Q15 multiplication must propagate flipped sum");
    auto legacy=flip(0,4,core::MonitorSignal::Input1,0,0);legacy.payload=core::AttackPayload::ApproximateUnit;
    graph.runApprox(zero,{legacy},&trace);
    require(trace[0].baselineOutput==0 && trace[0].output==approximate::addSigned12(0,0,legacy.unit),"Legacy DCT replacement changed");
    applications::Dct8FixedGraph::Vector negative{};negative[0]=-1;negative[7]=-1;
    graph.runApprox(negative,{flip(0,0,core::MonitorSignal::BaselineOutput,-2,-2)},&trace);
    require(trace[0].output==-1,"DCT signed13 negative decode broken");
    for(int bit:{-1,12}) rejects([&]{graph.runApprox(zero,{flip(0,bit,core::MonitorSignal::Input1,9000,9001)});});
    applications::Dct8FixedGraph::Vector tooWide{};tooWide.fill(3000);
    rejects([&]{graph.runApprox(tooWide,{flip(0,1,core::MonitorSignal::Input1,3000,3000)});});
    rejects([&]{auto f=flip(0,0,core::MonitorSignal::Input1,0,0);graph.runApprox(zero,{f,f});});
    applications::DctApplication app;
    cv::Mat image(9,10,CV_8UC1,cv::Scalar(128)),mask(9,10,CV_8UC1,cv::Scalar(0));mask(cv::Rect(4,3,4,4)).setTo(255);
    const auto f=flip(31,2,core::MonitorSignal::BaselineOutput,-4096,4095);
    std::vector<core::AddSample> samples;app.collectConfiguredAddSamples(image,mask,{f},samples);
    require(samples.size()==4*16*32,"DCT must include all padded blocks and row/column calls");
    int hits=0;for(const auto& s:samples) if(s.nodeId==31){++hits;require(s.output==core::flipNonSignOutputBit(s.baselineOutput,2),"Repeated DCT invocation not flipped");}
    require(hits==64,"Static DCT node must apply to every dynamic invocation");
    require(app.runApprox(image,{f}).size()==image.size(),"DCT crop changed");
}
}
int main(){try{spatialApplications();dctGraphAndReuse();std::cout<<"Circuit bit-flip execution contracts passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

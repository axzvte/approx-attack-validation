#include "analysis/sharpen_bitflip_diagnostics.hpp"
#include <opencv2/core.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
long long square(int x) { return static_cast<long long>(x)*x; }

void checkTrace() {
    applications::SharpenApplication app;
    cv::Mat image(6,7,CV_8UC1), mask(6,7,CV_8UC1,cv::Scalar(0));
    for(int r=0;r<6;++r) for(int c=0;c<7;++c)
        image.at<unsigned char>(r,c)=(17*r+31*c+13*r*c)%256;
    mask(cv::Rect(1,1,3,3)).setTo(255);
    const auto data=analysis::collectSharpenBaseline(app,image,mask);
    require(data.samples.size()==80,"Only interior pixels execute four nodes");
    require(data.roiPixels==9 && data.nonRoiPixels==33,"Image metrics include borders");
    std::vector<core::AddSample> baseline,exact;
    app.collectBaselineAddSamples(image,mask,baseline);
    app.collectExactAddSamples(image,mask,exact);
    const auto ref=app.runExact(image), out=app.runApprox(image,{});
    bool distinctErrors=false;
    for(std::size_t i=0;i<data.samples.size();++i) {
        const auto& s=data.samples[i];
        const int r=1+int(i/4)/5,c=1+int(i/4)%5;
        require(s.row==r && s.col==c && s.nodeId==int(i%4),"Pixel/trace alignment lost");
        require(s.roi==(mask.at<unsigned char>(r,c)>=128),"ROI tag differs from original mask");
        require(s.input1==baseline[i].input1 && s.input2==baseline[i].input2 &&
                s.baselineOutput==baseline[i].output,"Baseline must use original circuit");
        require(s.exactSameInputOutput==s.input1+s.input2,"Local exact output uses same inputs");
        require(s.exactPathOutput==exact[i].output,"Exact path must have its own upstream values");
        require(s.localError==s.baselineOutput-s.exactSameInputOutput &&
                s.accumulatedError==s.baselineOutput-s.exactPathOutput,"Error definitions confused");
        distinctErrors|=s.localError!=s.accumulatedError;
        require(s.baselinePixel==out.at<unsigned char>(r,c) &&
                s.referencePixel==ref.at<unsigned char>(r,c),"Final reference mismatch");
    }
    require(distinctErrors,"Fixture must distinguish local from accumulated error");
    long long er=0,en=0;
    for(int r=0;r<6;++r) for(int c=0;c<7;++c) {
        const auto e=square(int(out.at<unsigned char>(r,c))-ref.at<unsigned char>(r,c));
        (mask.at<unsigned char>(r,c)>=128?er:en)+=e;
    }
    require(data.roiSquaredError==er && data.nonRoiSquaredError==en,"Full-image baseline errors wrong");
}

void checkAgainstFullApplication() {
    std::size_t valid=0,invalid=0,up=0,down=0,saturated=0,negative=0;
    const std::array<std::array<int,5>,5> patches{{
        {{0,255,255,255,255}},{{255,0,0,0,0}},{{100,100,100,100,100}},
        {{70,255,10,200,1}},{{153,17,181,8,230}}
    }};
    for(int mode=0;mode<3;++mode) for(const auto& p:patches) {
        auto cfg=applications::SharpenApplication::createDefaultBaselineConfig();
        if(mode==1) cfg=applications::SharpenApplication::createAllExactBaselineConfig();
        if(mode==2) cfg[2].exact=true;
        applications::SharpenApplication app(cfg);
        cv::Mat image(3,3,CV_8UC1,cv::Scalar(0)),mask(3,3,CV_8UC1,cv::Scalar(0));
        image.at<unsigned char>(1,1)=p[0];image.at<unsigned char>(0,1)=p[1];
        image.at<unsigned char>(2,1)=p[2];image.at<unsigned char>(1,0)=p[3];
        image.at<unsigned char>(1,2)=p[4];
        const auto data=analysis::collectSharpenBaseline(app,image,mask);
        std::size_t probes=0;
        analysis::visitSharpenBitflips(data,[&](const auto& s,const auto& f) {
            ++probes;
            negative+=s.baselineOutput<0;
            require(f.nodeDelta==f.flippedOutput-s.baselineOutput,"Signed flip delta wrong");
            require((f.nodeDelta==(1<<f.bit)) || (f.nodeDelta==-(1<<f.bit)),"Non-sign flip magnitude wrong");
            up+=f.nodeDelta>0;down+=f.nodeDelta<0;
            require(f.localErrorAfter==f.flippedOutput-s.exactSameInputOutput &&
                    f.accumulatedErrorAfter==f.flippedOutput-s.exactPathOutput,"Probe errors use wrong reference");
            require(f.localSquaredErrorDelta==square(f.localErrorAfter)-square(s.localError),"Local delta wrong");
            core::AttackConfig attack{s.nodeId,approximate::ApproxUnitId::Add12se5RP,
                core::MonitorSignal::Input1,-4096,4095,core::AttackPayload::BitFlip,f.bit};
            cv::Mat actual;
            try { actual=app.runApprox(image,{attack}); }
            catch(const std::runtime_error&) {
                require(!f.valid,"Illegal downstream input must be reported");++invalid;return;
            }
            require(f.valid,"Legal full application was incorrectly rejected");++valid;
            const int pixel=actual.at<unsigned char>(1,1);
            require(f.finalPixel==pixel,"Replay differs from actual application");
            require(f.finalSquaredErrorDelta==square(pixel-s.referencePixel)-
                    square(s.baselinePixel-s.referencePixel),"Final delta must include clipping and original error");
            saturated+=pixel==0 || pixel==255;
        });
        require(probes==48,"Must inspect all four nodes and twelve non-sign bits");
    }
    require(valid && invalid && up && down && saturated && negative,"Fixtures miss a critical branch");
}
}

int main() {
    try {
        checkTrace();checkAgainstFullApplication();
        std::cout<<"Baseline traces and bit probes match complete application execution\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

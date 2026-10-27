#include "analysis/sharpen_bitflip_diagnostics.hpp"
#include "core/output_bitflip.hpp"
#include "region/region_mask.hpp"
#include <algorithm>
#include <stdexcept>

namespace analysis {
namespace {
std::int64_t square(int x) { return static_cast<std::int64_t>(x)*x; }
}

SharpenBaselineData collectSharpenBaseline(const applications::SharpenApplication& app,
    const cv::Mat& image, const cv::Mat& mask) {
    if(image.rows<3 || image.cols<3)
        throw std::runtime_error("Diagnostics require at least one interior pixel (3x3 image).");
    std::vector<core::AddSample> baseline,exact;
    // These calls validate grayscale input and the same-size original mask.
    app.collectBaselineAddSamples(image,mask,baseline);
    app.collectExactAddSamples(image,mask,exact);
    const auto reference=app.runExact(image), output=app.runApprox(image,{});
    const auto expected=static_cast<std::size_t>(image.rows-2)*(image.cols-2)*4;
    if(baseline.size()!=expected || exact.size()!=expected)
        throw std::runtime_error("Sharpen trace length differs from interior pixel count.");
    SharpenBaselineData data;
    data.rows=image.rows;data.cols=image.cols;data.config=app.baselineConfig();
    data.samples.reserve(expected);
    std::size_t index=0;
    for(int r=0;r<image.rows;++r) for(int c=0;c<image.cols;++c) {
        const bool roi=region_mask::isImportantPixel(mask,r,c);
        const int pixel=output.at<unsigned char>(r,c), ref=reference.at<unsigned char>(r,c);
        if(roi) {++data.roiPixels;data.roiSquaredError+=square(pixel-ref);}
        else {++data.nonRoiPixels;data.nonRoiSquaredError+=square(pixel-ref);}
        if(r==0 || c==0 || r==image.rows-1 || c==image.cols-1) continue;
        for(int node=0;node<4;++node,++index) {
            const auto& b=baseline[index];const auto& e=exact[index];
            if(b.nodeId!=node || e.nodeId!=node)
                throw std::runtime_error("Sharpen trace order differs from pixel/node order.");
            SharpenBaselineSample s;
            s.row=r;s.col=c;s.nodeId=node;s.roi=roi;
            s.input1=b.input1;s.input2=b.input2;s.baselineOutput=b.output;
            s.exactSameInputOutput=b.input1+b.input2;s.exactPathOutput=e.output;
            s.localError=b.output-s.exactSameInputOutput;
            s.accumulatedError=b.output-e.output;
            s.baselinePixel=pixel;s.referencePixel=ref;
            data.samples.push_back(s);
        }
    }
    return data;
}

void visitSharpenBitflips(const SharpenBaselineData& data,
    const std::function<void(const SharpenBaselineSample&,const SharpenBitflipResponse&)>& visitor) {
    if(!visitor || data.rows<3 || data.cols<3 || data.samples.size()!=
            static_cast<std::size_t>(data.rows-2)*(data.cols-2)*4)
        throw std::runtime_error("Invalid baseline data or missing diagnostic visitor.");
    for(std::size_t base=0;base<data.samples.size();base+=4) {
        for(int node=0;node<4;++node) {
            const auto& s=data.samples[base+node];
            if(s.nodeId!=node) throw std::runtime_error("Baseline node ordering is invalid.");
            for(int bit=0;bit<12;++bit) {
                SharpenBitflipResponse f;
                f.bit=bit;f.flippedOutput=core::flipNonSignOutputBit(s.baselineOutput,bit);
                f.nodeDelta=f.flippedOutput-s.baselineOutput;
                f.localErrorAfter=f.flippedOutput-s.exactSameInputOutput;
                f.accumulatedErrorAfter=f.flippedOutput-s.exactPathOutput;
                f.localSquaredErrorDelta=square(f.localErrorAfter)-square(s.localError);
                f.accumulatedSquaredErrorDelta=square(f.accumulatedErrorAfter)-square(s.accumulatedError);
                int value=f.flippedOutput;
                f.valid=true;
                for(int next=node+1;next<4;++next) {
                    const auto& cfg=data.config[next];
                    // In Sharpening each downstream RHS is an unchanged source pixel.
                    const int rhs=data.samples[base+next].input2;
                    if(!cfg.exact && (value < -2048 || value > 2047 || rhs < -2048 || rhs > 2047)) {
                        f.valid=false;break;
                    }
                    value=cfg.exact ? value+rhs : approximate::addSigned12(value,rhs,cfg.unit);
                }
                if(f.valid) {
                    f.finalPixel=std::clamp(value,0,255);
                    f.finalSquaredErrorDelta=square(f.finalPixel-s.referencePixel)-
                        square(s.baselinePixel-s.referencePixel);
                }
                visitor(s,f);
            }
        }
    }
}

}

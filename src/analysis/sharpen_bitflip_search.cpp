#include "analysis/sharpen_bitflip_search.hpp"
#include "core/output_bitflip.hpp"
#include "region/region_mask.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace analysis {
namespace {
double psnr(double mse) {
    return mse <= 0 ? std::numeric_limits<double>::infinity() : 10*std::log10(65025.0/mse);
}
struct Errors {
    double roi = 0, non = 0;
    std::size_t roiCount = 0, nonCount = 0, invalid = 0;
    Errors& operator+=(const Errors& b) {
        roi += b.roi; non += b.non; roiCount += b.roiCount;
        nonCount += b.nonCount; invalid += b.invalid; return *this;
    }
};
Errors difference(const Errors& a, const Errors& b) {
    return {a.roi-b.roi, a.non-b.non, a.roiCount-b.roiCount,
            a.nonCount-b.nonCount, a.invalid-b.invalid};
}
int monitored(const core::AddSample& s, int signal) {
    return signal == 0 ? s.input1 : signal == 1 ? s.input2 : s.baselineOutput;
}
constexpr std::array<core::MonitorSignal,3> signals{
    core::MonitorSignal::Input1, core::MonitorSignal::Input2, core::MonitorSignal::BaselineOutput};
}

double BitflipQuality::globalPsnr() const { return psnr(globalMse); }
double BitflipQuality::roiPsnr() const { return psnr(roiMse); }
double BitflipQuality::nonRoiPsnr() const { return psnr(nonRoiMse); }

bool SharpenBitflipCandidate::successful(const BitflipQuality& baseline) const {
    return hasInterval && quality.roiMse > baseline.roiMse + 1e-10 &&
        quality.roiMse-baseline.roiMse > quality.nonRoiMse-baseline.nonRoiMse + 1e-10;
}

BitflipQuality measureBitflipQuality(const cv::Mat& ref, const cv::Mat& out, const cv::Mat& mask) {
    if (ref.empty() || ref.type()!=CV_8UC1 || out.type()!=CV_8UC1 || mask.type()!=CV_8UC1 ||
        ref.size()!=out.size() || ref.size()!=mask.size())
        throw std::runtime_error("Quality inputs must be same-size nonempty grayscale images.");
    Errors sums;
    for(int r=0;r<ref.rows;++r) for(int c=0;c<ref.cols;++c) {
        const double d = int(ref.at<unsigned char>(r,c))-int(out.at<unsigned char>(r,c));
        if(region_mask::isImportantPixel(mask,r,c)) { sums.roi+=d*d; ++sums.roiCount; }
        else { sums.non+=d*d; ++sums.nonCount; }
    }
    if (!sums.roiCount || !sums.nonCount)
        throw std::runtime_error("Single-node validation needs both ROI and non-ROI pixels.");
    return {(sums.roi+sums.non)/ref.total(), sums.roi/sums.roiCount, sums.non/sums.nonCount};
}

SharpenBitflipResult SharpenBitflipSearch::evaluate(const applications::SharpenApplication& app,
    const cv::Mat& image, const cv::Mat& mask, std::size_t boundaryBudget, double threshold,
    const core::AttackConfig* fixedHardware) {
    if (!std::isfinite(threshold) || (boundaryBudget != 0 && boundaryBudget < 2))
        throw std::runtime_error("Use a finite PSNR threshold and zero or at least two boundaries.");
    if(fixedHardware) {
        if(fixedHardware->payload!=core::AttackPayload::BitFlip || fixedHardware->nodeId<0 ||
            fixedHardware->nodeId>=4 || std::find(signals.begin(),signals.end(),fixedHardware->monitorInput)==signals.end())
            throw std::runtime_error("Invalid fixed bit-flip hardware.");
        core::flipNonSignOutputBit(0,fixedHardware->bitIndex);
    }
    const auto reference = app.runExact(image);
    const auto baselineImage = app.runApprox(image, {});
    SharpenBitflipResult result;
    result.baseline = measureBitflipQuality(reference, baselineImage, mask);
    if (image.rows < 3 || image.cols < 3)
        throw std::runtime_error("Sharpen bit-flip search needs at least one interior pixel.");
    std::vector<core::AddSample> samples;
    app.collectBaselineAddSamples(image, mask, samples);
    std::size_t roiCount=0, nonCount=0;
    for(int r=0;r<image.rows;++r) for(int c=0;c<image.cols;++c)
        region_mask::isImportantPixel(mask,r,c) ? ++roiCount : ++nonCount;
    const double globalLimit=65025.0*std::pow(10.0,-threshold/10.0);
    constexpr int offset=4096, bins=8192;
    for(int node=0;node<4;++node) for(int bit=0;bit<12;++bit) {
        if(fixedHardware && (node!=fixedHardware->nodeId || bit!=fixedHardware->bitIndex)) continue;
        std::array<std::vector<Errors>,3> hist;
        for(auto& h:hist) h.resize(bins);
        std::size_t pixel=0;
        for(int r=1;r<image.rows-1;++r) for(int c=1;c<image.cols-1;++c,++pixel) {
            const auto base=pixel*4;
            const auto& sample=samples.at(base+node);
            int value=core::flipNonSignOutputBit(sample.baselineOutput,bit);
            bool invalid=false;
            for(int downstream=node+1;downstream<4;++downstream) {
                const auto& cfg=app.baselineConfig()[downstream];
                const int rhs=samples[base+downstream].input2;
                if(!cfg.exact && (value < -2048 || value > 2047)) { invalid=true; break; }
                value=cfg.exact ? value+rhs : approximate::addSigned12(value,rhs,cfg.unit);
            }
            const int clipped=std::clamp(value,0,255);
            const double flippedError=int(reference.at<unsigned char>(r,c))-clipped;
            const double baseError=int(reference.at<unsigned char>(r,c))-int(baselineImage.at<unsigned char>(r,c));
            const double delta=flippedError*flippedError-baseError*baseError;
            const bool roi=region_mask::isImportantPixel(mask,r,c);
            for(int signal=0;signal<3;++signal) {
                const int index=monitored(sample,signal)+offset;
                if(index<0 || index>=bins) throw std::runtime_error("Monitor exceeds supported signed 13-bit range.");
                auto& h=hist[signal][index];
                if(roi) { ++h.roiCount; h.roi+=delta; } else { ++h.nonCount; h.non+=delta; }
                if(invalid) ++h.invalid;
            }
        }
        for(int signal=0;signal<3;++signal) {
            if(fixedHardware && signals[signal]!=fixedHardware->monitorInput) continue;
            SharpenBitflipCandidate best;
            best.config={node, app.baselineConfig()[node].unit, signals[signal], 0, 0,
                         core::AttackPayload::BitFlip, bit};
            std::vector<int> values;
            std::vector<Errors> prefix(1);
            for(int v=0;v<bins;++v) if(hist[signal][v].roiCount+hist[signal][v].nonCount) {
                values.push_back(v-offset); prefix.push_back(prefix.back()); prefix.back()+=hist[signal][v];
            }
            std::vector<std::size_t> boundaries;
            const auto count=boundaryBudget==0 ? values.size() : std::min(boundaryBudget,values.size());
            for(std::size_t i=0;i<count;++i)
                boundaries.push_back(count==1 ? 0 : i*(values.size()-1)/(count-1));
            for(auto lo=boundaries.begin();lo!=boundaries.end();++lo) for(auto hi=lo;hi!=boundaries.end();++hi) {
                ++best.evaluatedIntervals;
                const auto change=difference(prefix[*hi+1],prefix[*lo]);
                if(change.invalid) { ++best.rangeRejectedIntervals; continue; }
                BitflipQuality q;
                q.roiMse=std::max(0.0,result.baseline.roiMse+change.roi/roiCount);
                q.nonRoiMse=std::max(0.0,result.baseline.nonRoiMse+change.non/nonCount);
                q.globalMse=(q.roiMse*roiCount+q.nonRoiMse*nonCount)/image.total();
                if(q.globalMse > globalLimit+1e-10) continue;
                if(best.hasInterval && (q.roiMse < best.quality.roiMse ||
                    (q.roiMse==best.quality.roiMse && q.globalMse>=best.quality.globalMse))) continue;
                best.hasInterval=true; best.quality=q;
                best.config.lower=values[*lo]; best.config.upper=values[*hi];
                best.roiTriggerRate=double(change.roiCount)/roiCount;
                best.nonRoiTriggerRate=double(change.nonCount)/nonCount;
                best.triggerRate=double(change.roiCount+change.nonCount)/((image.rows-2)*(image.cols-2));
            }
            result.candidates.push_back(best);
        }
    }
    return result;
}

}

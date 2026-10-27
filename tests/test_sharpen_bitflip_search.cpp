#include "analysis/sharpen_bitflip_search.hpp"
#include "region/region_mask.hpp"
#include <opencv2/core.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
analysis::BitflipQuality quality(const cv::Mat& ref, const cv::Mat& out, const cv::Mat& mask) {
    double roi = 0, non = 0, nr = 0, nn = 0;
    for (int r = 0; r < ref.rows; ++r) for (int c = 0; c < ref.cols; ++c) {
        double d = int(ref.at<unsigned char>(r,c)) - int(out.at<unsigned char>(r,c));
        if (mask.at<unsigned char>(r,c) >= 128) { roi += d*d; ++nr; }
        else { non += d*d; ++nn; }
    }
    analysis::BitflipQuality q;
    q.globalMse = (roi + non)/(nr + nn); q.roiMse = roi/nr; q.nonRoiMse = non/nn;
    return q;
}
int monitor(const core::AddSample& s, core::MonitorSignal signal) {
    return signal == core::MonitorSignal::Input1 ? s.input1 :
           signal == core::MonitorSignal::Input2 ? s.input2 : s.baselineOutput;
}
void compare(double a, double b) { require(std::abs(a-b) < 1e-8, "Prefix metrics differ from full application"); }
}

int main() {
    try {
        cv::Mat image(6, 7, CV_8UC1);
        for (int r=0; r<image.rows; ++r) for(int c=0;c<image.cols;++c)
            image.at<unsigned char>(r,c) = (17*r + 31*c + 13*r*c) % 256;
        cv::Mat mask(image.size(), CV_8UC1, cv::Scalar(0));
        mask(cv::Rect(1, 1, 3, 3)).setTo(255);
        applications::SharpenApplication app;
        auto result = analysis::SharpenBitflipSearch::evaluate(app, image, mask, 0, 10.0);
        require(result.candidates.size() == 144, "Must evaluate all 4 nodes x 3 monitors x 12 bits");
        auto ref = app.runExact(image);
        auto baseline = quality(ref, app.runApprox(image, {}), mask);
        compare(result.baseline.globalMse, baseline.globalMse);
        std::vector<core::AddSample> samples;
        app.collectBaselineAddSamples(image, mask, samples);
        std::size_t invalid = 0;
        for (const auto& candidate : result.candidates) {
            std::set<int> values;
            for (const auto& s : samples) if(s.nodeId == candidate.config.nodeId)
                values.insert(monitor(s, candidate.config.monitorInput));
            bool found = false;
            double bestRoi = -1;
            std::size_t rangeRejected = 0;
            for(auto lo=values.begin(); lo!=values.end(); ++lo) for(auto hi=lo;hi!=values.end();++hi) {
                auto config = candidate.config; config.lower=*lo; config.upper=*hi;
                analysis::BitflipQuality q;
                try { q = quality(ref, app.runApprox(image, {config}), mask); }
                catch(const std::runtime_error&) { ++rangeRejected; continue; }
                if(q.globalPsnr() + 1e-10 >= 10.0) { found=true; bestRoi=std::max(bestRoi,q.roiMse); }
            }
            invalid += rangeRejected;
            require(candidate.rangeRejectedIntervals == rangeRejected, "Illegal downstream ranges must be excluded per interval");
            require(candidate.hasInterval == found, "Feasibility differs from exhaustive full runs");
            if (!found) continue;
            compare(candidate.quality.roiMse, bestRoi);
            auto q = quality(ref, app.runApprox(image, {candidate.config}), mask);
            compare(candidate.quality.globalMse, q.globalMse);
            compare(candidate.quality.roiMse, q.roiMse);
            compare(candidate.quality.nonRoiMse, q.nonRoiMse);
            require(candidate.triggerRate > 0, "Selected interval must actually trigger");
        }
        require(invalid > 0, "Fixture must exercise downstream width rejection");
        auto reduced = analysis::SharpenBitflipSearch::evaluate(app, image, mask, 4, 30.0);
        for (const auto& c : reduced.candidates) {
            require(c.evaluatedIntervals <= 10, "Boundary budget must be respected");
            if(c.hasInterval) {
                auto q=quality(ref,app.runApprox(image,{c.config}),mask);
                compare(c.quality.globalMse,q.globalMse);
                require(q.globalPsnr()+1e-10>=30.0,"Global quality constraint was violated");
            }
        }
        const auto fixedHardware = result.candidates[17].config;
        auto frozen = analysis::SharpenBitflipSearch::evaluate(app, image, mask, 4, 30.0, &fixedHardware);
        require(frozen.candidates.size()==1, "Frozen-hardware evaluation must search only one structure");
        require(frozen.candidates[0].config.nodeId==fixedHardware.nodeId &&
                frozen.candidates[0].config.monitorInput==fixedHardware.monitorInput &&
                frozen.candidates[0].config.bitIndex==fixedHardware.bitIndex,
                "Frozen hardware changed during interval adaptation");
        std::cout << "Sharpen exact interval search matches exhaustive full application runs\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

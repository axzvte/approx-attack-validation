#pragma once
#include "core/add_sample.hpp"
#include "core/output_bitflip.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace analysis {
struct BitflipSensitivityPriority {
    std::vector<int> order;
    std::vector<double> separability,disturbance;
    // ROI/non-ROI stratified contributions; a fractional call may count twice.
    std::vector<std::size_t> sampledPerNode;
};

// A bounded, baseline-only ordering heuristic. Separability is normalized total
// variation between region signal distributions. Disturbance is the normalized
// raw-node change caused by a bit flip, NOT final image error or a causal role.
// Every native node stays in the order, including unobserved/low priority nodes.
// DCT fractional weights are block diagnostics, not independent pixel scores.
inline BitflipSensitivityPriority prioritizeBitflipNodes(const std::vector<core::AddSample>& samples,
    std::size_t nodes,const std::vector<int>& bits,std::size_t perRegionCap=128) {
    if(!nodes || !perRegionCap || bits.empty()) throw std::invalid_argument("Invalid sensitivity sampling options.");
    for(int bit:bits) core::flipNonSignOutputBit(0,bit);
    using Counts=std::array<std::size_t,2>;
    std::vector<Counts> totals(nodes),seen(nodes);
    for(const auto& s:samples) {
        if(s.nodeId<0 || std::size_t(s.nodeId)>=nodes || !std::isfinite(s.roiWeight) || s.roiWeight<0 || s.roiWeight>1)
            throw std::invalid_argument("Invalid native node or ROI weight in sensitivity trace.");
        if(s.roiWeight>0) ++totals[s.nodeId][0];
        if(s.roiWeight<1) ++totals[s.nodeId][1];
    }
    struct Summary {
        std::array<std::array<std::array<double,32>,3>,2> bins{};
        std::array<double,2> weights{};
        double roiDisturbance=0;
    };
    std::vector<Summary> summaries(nodes);
    BitflipSensitivityPriority out;out.order.resize(nodes);std::iota(out.order.begin(),out.order.end(),0);
    out.separability.resize(nodes);out.disturbance.resize(nodes);out.sampledPerNode.resize(nodes);
    for(const auto& s:samples) for(std::size_t region=0;region<2;++region) {
        const double weight=region?1-s.roiWeight:s.roiWeight;if(weight<=0) continue;
        const auto n=std::size_t(s.nodeId),index=seen[n][region]++,total=totals[n][region];
        // Systematic sampling spans each region's entire trace, with a fixed
        // cap independent of image size and without a random generator.
        if(total>perRegionCap && (index*perRegionCap)/total==((index+1)*perRegionCap)/total) continue;
        ++out.sampledPerNode[n];auto& summary=summaries[n];summary.weights[region]+=weight;
        const std::array<int,3> values{s.input1,s.input2,s.baselineOutput};
        for(std::size_t monitor=0;monitor<3;++monitor) {
            const int half=monitor==2?4096:2048;
            const int bin=(std::clamp(values[monitor],-half,half-1)+half)*32/(half*2);
            summary.bins[region][monitor][bin]+=weight;
        }
        if(region==0 && s.baselineOutput>=-4096 && s.baselineOutput<=4095) {
            double disturbance=0;
            for(int bit:bits) disturbance+=std::min(1.0,std::abs(double(core::flipNonSignOutputBit(s.baselineOutput,bit))-s.baselineOutput)/(1+std::abs(double(s.baselineOutput))));
            summary.roiDisturbance+=weight*disturbance/bits.size();
        }
    }
    for(std::size_t n=0;n<nodes;++n) {
        const auto& s=summaries[n];
        if(s.weights[0]>0) out.disturbance[n]=s.roiDisturbance/s.weights[0];
        if(s.weights[0]<=0 || s.weights[1]<=0) continue;
        for(std::size_t monitor=0;monitor<3;++monitor) {
            double distance=0;
            for(std::size_t bin=0;bin<32;++bin) distance+=std::abs(s.bins[0][monitor][bin]/s.weights[0]-s.bins[1][monitor][bin]/s.weights[1]);
            out.separability[n]=std::max(out.separability[n],distance/2);
        }
    }
    std::stable_sort(out.order.begin(),out.order.end(),[&](int a,int b) {
        const auto score=[&](int n){return out.separability[n]+0.25*out.disturbance[n];};
        return score(a)>score(b);
    });
    return out;
}
}

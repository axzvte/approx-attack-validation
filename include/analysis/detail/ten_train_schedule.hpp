#pragma once
#include "analysis/circuit_bitflip_ten_train.hpp"
#include <algorithm>
#include <tuple>

namespace analysis::detail {
inline bool strictlyEliminated(const TenTrainCandidate& c,const TenTrainCandidate& incumbent,std::size_t total) {
    const auto upper=c.numericSuccesses+total-c.evaluations.size();
    return upper<incumbent.numericSuccesses || (upper==incumbent.numericSuccesses &&
        !c.evaluations.empty() && c.worstRoiDrop<incumbent.worstRoiDrop);
}
inline auto canonicalHardware(const FixedBitflipHardware& h) {
    std::vector<std::tuple<int,std::string,int>> key;
    for(const auto& n:h) key.emplace_back(n.nodeId,n.monitorInput==core::MonitorSignal::BaselineOutput?
        "baseline_output":n.monitorInput==core::MonitorSignal::Input1?"input1":"input2",n.bitIndex);
    std::sort(key.begin(),key.end());return key;
}
inline std::optional<std::size_t> completionChoice(const std::vector<TenTrainCandidate>& rows,
        std::optional<std::size_t> incumbent,std::size_t total) {
    std::optional<std::size_t> best;
    const auto preferred=[&](const auto& a,const auto& b) {
        const auto am=total-a.evaluations.size(),bm=total-b.evaluations.size();
        if(am!=bm) return am<bm;
        const auto au=a.numericSuccesses+am,bu=b.numericSuccesses+bm;
        if(au!=bu) return au>bu;
        if(a.worstRoiDrop!=b.worstRoiDrop) return a.worstRoiDrop>b.worstRoiDrop;
        // Native interval context count is a deterministic workload proxy;
        // elapsed wall time never participates in scheduling or pruning.
        const auto estimate=[&](const auto& c) {
            double contexts=0;for(const auto& e:c.evaluations) contexts+=e.stats.contextsBuilt;
            return c.evaluations.empty()?0.0:contexts*(total-c.evaluations.size())/c.evaluations.size();
        };
        const auto ae=estimate(a),be=estimate(b);if(ae!=be) return ae<be;
        return canonicalHardware(a.hardware)<canonicalHardware(b.hardware);
    };
    for(std::size_t i=0;i<rows.size();++i) {
        const auto& c=rows[i];
        if(c.status!="budget_incomplete" || c.evaluations.size()>=total ||
                (incumbent && strictlyEliminated(c,rows[*incumbent],total))) continue;
        if(!best || preferred(c,rows[*best])) best=i;
    }
    return best;
}
}

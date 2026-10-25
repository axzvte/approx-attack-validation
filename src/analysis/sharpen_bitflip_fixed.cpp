#include "analysis/sharpen_bitflip_fixed.hpp"
#include "core/output_bitflip.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace analysis {
namespace {
using Config=JointBitflipConfiguration;
using State=JointBitflipState;
using Bin=JointBitflipBin;
using Signal=core::MonitorSignal;
using Clock=std::chrono::steady_clock;
std::pair<int,int> domain(Signal s) {
    if(s==Signal::Input1 || s==Signal::Input2) return {-2048,2047};
    if(s==Signal::BaselineOutput) return {-4096,4095};
    throw std::runtime_error("Unknown fixed hardware monitor signal.");
}
bool validHardware(const FixedBitflipHardware& h) {
    if(h.size()<2 || h.size()>4) return false;
    std::set<int> nodes;
    for(const auto& n:h) {
        if(n.nodeId<0 || n.nodeId>=4 || !nodes.insert(n.nodeId).second || n.bitIndex<0 || n.bitIndex>=12) return false;
        if(n.monitorInput!=Signal::Input1 && n.monitorInput!=Signal::Input2 && n.monitorInput!=Signal::BaselineOutput) return false;
    }
    return true;
}
void validateHardware(const FixedBitflipHardware& h) {
    if(!validHardware(h)) throw std::runtime_error("Fixed hardware requires 2..4 unique native nodes, valid monitors and non-sign bits.");
}
void validateSeed(const Config& c,const FixedBitflipHardware& h) {
    if(!matchesFixedBitflipHardware(c,h)) throw std::runtime_error("Warm seed does not match fixed hardware.");
    for(const auto& n:c) {
        const auto d=domain(n.monitorInput);
        if(n.lower>n.upper || n.lower<d.first || n.upper>d.second)
            throw std::runtime_error("Fixed interval is outside its monitor's signed representable domain.");
    }
}
// These are the two precise arithmetic errors from the original application.
// Other runtime errors are configuration/program errors and must propagate.
bool widthFailure(const std::runtime_error& e) {
    const std::string message=e.what();
    return message=="Value exceeds signed 12-bit range." ||
        message=="Bit-flip input exceeds signed 13-bit output range.";
}
int monitored(const core::AddSample& s,Signal signal) {
    if(signal==Signal::Input1) return s.input1;
    if(signal==Signal::Input2) return s.input2;
    if(signal==Signal::BaselineOutput) return s.baselineOutput;
    throw std::runtime_error("Unknown fixed monitor signal.");
}
std::string key(Config c,int omit=-1) {
    std::sort(c.begin(),c.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});
    std::ostringstream out;
    for(const auto& n:c) if(n.nodeId!=omit)
        out<<n.nodeId<<':'<<int(n.monitorInput)<<':'<<n.bitIndex<<':'<<n.lower<<':'<<n.upper<<';';
    return out.str();
}
std::optional<int> unobserved(const std::set<int>& seen,Signal signal) {
    const auto d=domain(signal);int value=d.first;
    for(int x:seen) {
        if(x<value) continue;
        if(x>value) break;
        if(value==d.second) return std::nullopt;
        ++value;
    }
    return value;
}
bool feasible(const BitflipQuality& q,double threshold) {return q.globalPsnr()+1e-10>=threshold;}
using Rank=std::pair<double,double>;
Rank rank(const BitflipQuality& q,int role) {
    switch(role) {
        case 0:case 1:return {q.roiMse,-q.globalMse};
        case 2:return {-q.globalMse,q.roiMse};
        case 3:return {-q.nonRoiMse,q.roiMse};
        case 4:return {q.roiMse-q.globalMse,-q.globalMse};
        default:return {q.roiMse-4*q.globalMse,q.roiMse};
    }
}
class Representatives {
    std::array<std::vector<State>,6> lists;
    std::size_t capacity;double threshold;
public:
    Representatives(std::size_t n,double t):capacity(n),threshold(t) {}
    template<class Make> void offer(const BitflipQuality& q,Make make) {
        std::optional<State> state;
        for(int role=0;role<6;++role) {
            if(role==0 && !feasible(q,threshold)) continue;
            auto& list=lists[role];
            if(list.size()>=capacity && rank(q,role)<=rank(list.back().metrics,role)) continue;
            if(!state) state=State{make(),q};
            const auto id=key(state->configuration);
            if(std::any_of(list.begin(),list.end(),[&](const auto& x){return key(x.configuration)==id;})) continue;
            list.push_back(*state);
            std::stable_sort(list.begin(),list.end(),[&](const auto& a,const auto& b){
                if(rank(a.metrics,role)!=rank(b.metrics,role)) return rank(a.metrics,role)>rank(b.metrics,role);
                return key(a.configuration)<key(b.configuration);
            });
            if(list.size()>capacity) list.resize(capacity);
        }
    }
    std::vector<State> states() const {
        std::map<std::string,State> unique;
        for(const auto& list:lists) for(const auto& state:list) unique.emplace(key(state.configuration),state);
        std::vector<State> out;for(const auto& x:unique) out.push_back(x.second);return out;
    }
};
std::vector<State> retain(const std::vector<State>& states,std::size_t width,double threshold) {
    Representatives pool(width,threshold);
    for(const auto& s:states) pool.offer(s.metrics,[&]{return s.configuration;});
    auto all=pool.states();std::array<std::vector<std::size_t>,6> orders;
    for(int role=0;role<6;++role) {
        for(std::size_t i=0;i<all.size();++i) if(role!=0 || feasible(all[i].metrics,threshold)) orders[role].push_back(i);
        std::stable_sort(orders[role].begin(),orders[role].end(),[&](auto a,auto b){
            if(rank(all[a].metrics,role)!=rank(all[b].metrics,role)) return rank(all[a].metrics,role)>rank(all[b].metrics,role);
            return key(all[a].configuration)<key(all[b].configuration);
        });
    }
    std::set<std::size_t> selected;
    for(std::size_t depth=0;selected.size()<width && depth<all.size();++depth)
        for(int role:{0,2,1,3,4,5}) if(depth<orders[role].size() && selected.size()<width) selected.insert(orders[role][depth]);
    std::vector<State> out;for(auto i:selected) out.push_back(std::move(all[i]));return out;
}
Bin add(Bin a,const Bin& b,int sign=1) {
    a.roiDelta+=sign*b.roiDelta;a.nonRoiDelta+=sign*b.nonRoiDelta;a.invalidDelta+=sign*b.invalidDelta;
    a.roiHits+=sign*b.roiHits;a.nonRoiHits+=sign*b.nonRoiHits;return a;
}
std::vector<State> expand(SharpenBitflipProfileWorkspace& workspace,
    const Config& current,const FixedBitflipNode& target,const FixedBitflipOptions& options,FixedBitflipStats& stats) {
    const auto profile=workspace.profile(current,target.nodeId,target.bitIndex,target.monitorInput);
    const auto intervalBegin=Clock::now();
    ++stats.contextsBuilt;++stats.contextsByNode[target.nodeId];
    std::vector<int> values;std::vector<Bin> prefix(1);std::set<int> seen;
    const auto d=domain(target.monitorInput);
    for(const auto& entry:profile.bins) {
        seen.insert(entry.first);
        if(entry.first<d.first || entry.first>d.second) continue;
        values.push_back(entry.first);prefix.push_back(add(prefix.back(),entry.second));
    }
    Representatives pool(options.representativeCount,options.globalPsnrThreshold);
    // Feed an already prefix-summed delta through the public scorer. Its exact
    // invalid-off-path repair logic is reused; each query now examines one bin.
    auto summed=profile;summed.bins.clear();summed.bins.emplace(0,Bin{});
    const auto evaluate=[&](int lo,int hi,const Bin& delta) {
        ++stats.evaluatedIntervals;summed.bins.begin()->second=delta;
        const auto score=scoreJointInterval(summed,0,0);
        if(!score.valid) {++stats.rangeRejected;return;}
        pool.offer(score.metrics,[&] {
            auto config=current;
            auto node=std::find_if(config.begin(),config.end(),[&](const auto& c){return c.nodeId==target.nodeId;});
            if(node==config.end()) throw std::runtime_error("Fixed target disappeared from executable state.");
            node->lower=lo;node->upper=hi;return config;
        });
    };
    // A no-hit interval is a real representable singleton, never node removal.
    if(const auto dormant=unobserved(seen,target.monitorInput)) evaluate(*dormant,*dormant,Bin{});
    const auto interval=[&](std::size_t lo,std::size_t hi) {
        evaluate(values[lo],values[hi],add(prefix[hi+1],prefix[lo],-1));
    };
    if(options.boundaryBudget==0) {
        for(std::size_t lo=0;lo<values.size();++lo) for(std::size_t hi=lo;hi<values.size();++hi) interval(lo,hi);
    } else {
        std::set<std::pair<std::size_t,std::size_t>> intervals;
        for(std::size_t lo=0;lo<values.size();++lo) for(std::size_t width:{1,2,3,4,8,16})
            if(width<=values.size()-lo) intervals.emplace(lo,lo+width-1);
        const auto count=std::min(options.boundaryBudget,values.size());
        std::vector<std::size_t> ends;
        for(std::size_t i=0;i<count;++i) ends.push_back(count==1?0:i*(values.size()-1)/(count-1));
        for(auto lo:ends) for(auto hi:ends) if(hi>=lo) intervals.emplace(lo,hi);
        for(const auto& x:intervals) interval(x.first,x.second);
    }
    auto states=pool.states();
    stats.intervalSeconds+=std::chrono::duration<double>(Clock::now()-intervalBegin).count();
    return states;
}
bool better(const State& a,const State& b) {
    if(a.metrics.roiMse!=b.metrics.roiMse) return a.metrics.roiMse>b.metrics.roiMse;
    if(a.metrics.globalMse!=b.metrics.globalMse) return a.metrics.globalMse<b.metrics.globalMse;
    return key(a.configuration)<key(b.configuration);
}
void verifyMetrics(const BitflipQuality& scored,const BitflipQuality& oracle) {
    for(const auto pair:{std::pair<double,double>{scored.globalMse,oracle.globalMse},
            {scored.roiMse,oracle.roiMse},{scored.nonRoiMse,oracle.nonRoiMse}})
        if(std::abs(pair.first-pair.second)>1e-8*std::max(1.0,std::abs(pair.second)))
            throw std::runtime_error("Fixed interval profile disagrees with original Sharpen application.");
}
}

bool matchesFixedBitflipHardware(const Config& config,const FixedBitflipHardware& hardware) {
    if(!validHardware(hardware) || config.size()!=hardware.size()) return false;
    std::set<int> seen;
    for(const auto& c:config) {
        if(c.payload!=core::AttackPayload::BitFlip || !seen.insert(c.nodeId).second) return false;
        const auto found=std::find_if(hardware.begin(),hardware.end(),[&](const auto& n){return n.nodeId==c.nodeId;});
        if(found==hardware.end() || c.monitorInput!=found->monitorInput || c.bitIndex!=found->bitIndex) return false;
    }
    return true;
}

std::optional<Config> dormantFixedBitflipSeed(const applications::SharpenApplication& app,
    const cv::Mat& image,const cv::Mat& mask,const FixedBitflipHardware& hardware) {
    validateHardware(hardware);
    std::vector<core::AddSample> baseline;
    app.collectBaselineAddSamples(image,mask,baseline);
    Config config;
    for(const auto& n:hardware) {
        std::set<int> seen;for(const auto& s:baseline) if(s.nodeId==n.nodeId) seen.insert(monitored(s,n.monitorInput));
        const auto value=unobserved(seen,n.monitorInput);if(!value) return std::nullopt;
        config.push_back({n.nodeId,app.baselineConfig()[n.nodeId].unit,n.monitorInput,*value,*value,core::AttackPayload::BitFlip,n.bitIndex});
    }
    validateSeed(config,hardware);
    std::vector<core::AddSample> actual;app.collectConfiguredAddSamples(image,mask,config,actual);
    for(const auto& c:config) for(const auto& s:actual) if(s.nodeId==c.nodeId) {
        const auto v=monitored(s,c.monitorInput);
        if(v>=c.lower && v<=c.upper) throw std::runtime_error("Dormant seed unexpectedly triggers installed hardware.");
    }
    const auto unchanged=measureBitflipQuality(app.runApprox(image,{}),app.runApprox(image,config),mask);
    if(unchanged.globalMse!=0) throw std::runtime_error("Dormant fixed hardware seed changed original baseline output.");
    return config;
}

FixedBitflipResult searchFixedBitflipIntervals(const applications::SharpenApplication& app,
    const cv::Mat& image,const cv::Mat& mask,const FixedBitflipHardware& hardware,const FixedBitflipOptions& options,
    const std::function<void(const FixedBitflipProgress&)>& progress) {
    validateHardware(hardware);
    if(!options.beamWidth || !options.representativeCount || !options.restarts || !options.maxContexts ||
            !std::isfinite(options.globalPsnrThreshold)) throw std::runtime_error("Invalid fixed interval search options.");
    // Validate every supplied bank member before running any search; malformed
    // rows are errors even when a previous seed would fill the context budget.
    for(const auto& seed:options.seeds) validateSeed(seed,hardware);
    FixedBitflipResult result;SharpenBitflipProfileWorkspace workspace(app,image,mask);
    const auto& reference=workspace.reference();result.stats.referenceSeconds=workspace.stats().referenceSeconds;
    const auto initializationBegin=Clock::now();
    result.baseline=measureBitflipQuality(reference,app.runApprox(image,{}),mask);
    std::vector<State> seeds;std::set<std::string> seedKeys;
    const auto accept=[&](const Config& seed,bool warm) {
        BitflipQuality q;
        try {q=measureBitflipQuality(reference,app.runApprox(image,seed),mask);}
        catch(const std::runtime_error& e) {
            if(!warm || !widthFailure(e)) throw;
            ++result.stats.rejectedSeeds;return;
        }
        ++result.stats.acceptedSeeds;
        if(seedKeys.insert(key(seed)).second) seeds.push_back({seed,q});
    };
    for(const auto& seed:options.seeds) accept(seed,true);
    if(const auto dormant=dormantFixedBitflipSeed(app,image,mask,hardware)) accept(*dormant,false);
    result.stats.initializationSeconds=std::chrono::duration<double>(Clock::now()-initializationBegin).count();
    if(seeds.empty()) return result;
    std::map<std::string,std::vector<State>> cache;
    Representatives legalArchive(options.beamWidth,options.globalPsnrThreshold);
    Representatives numericArchive(options.representativeCount,options.globalPsnrThreshold);
    const auto remember=[&](const std::vector<State>& states) {
        for(const auto& state:states) {
            validateSeed(state.configuration,hardware);
            legalArchive.offer(state.metrics,[&]{return state.configuration;});
            if(feasible(state.metrics,options.globalPsnrThreshold) && state.metrics.roiMse>result.baseline.roiMse+1e-10)
                numericArchive.offer(state.metrics,[&]{return state.configuration;});
            if(!feasible(state.metrics,options.globalPsnrThreshold)) ++result.stats.retainedQualityFailures;
        }
    };
    remember(seeds);auto beam=retain(seeds,options.beamWidth,options.globalPsnrThreshold);
    auto order=hardware;
    std::sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});
    const auto visit=[&](const State& state,const FixedBitflipNode& target,std::size_t restart,std::size_t pass,
            std::size_t done,std::size_t total,std::vector<State>& expanded) {
        const auto id=std::to_string(target.nodeId)+"|"+key(state.configuration,target.nodeId);
        auto found=cache.find(id);
        if(found==cache.end()) {
            if(result.stats.contextsBuilt>=options.maxContexts) {result.stats.contextBudgetExhausted=true;return false;}
            auto states=expand(workspace,state.configuration,target,options,result.stats);
            const auto& ps=workspace.stats();result.stats.traceBuilds=ps.traceBuilds;result.stats.traceCacheHits=ps.traceCacheHits;
            result.stats.traceSeconds=ps.traceSeconds;result.stats.profileSeconds=ps.profileSeconds;
            found=cache.emplace(id,std::move(states)).first;
        } else ++result.stats.cacheHits;
        remember(found->second);expanded.insert(expanded.end(),found->second.begin(),found->second.end());
        if(progress) progress({restart,pass,static_cast<std::size_t>(target.nodeId),done,total,result.stats});
        return true;
    };
    // Seed-first round robin: one state visits every installed target before
    // another seed is expanded. Refinement of multiple beams happens afterward.
    bool exhausted=false;
    for(std::size_t i=0;i<seeds.size() && !exhausted;++i) {
        auto seedOrder=order;if(i%2) std::reverse(seedOrder.begin(),seedOrder.end());
        for(const auto& target:seedOrder) {
            auto expanded=beam;
            if(!visit(seeds[i],target,0,0,i+1,seeds.size(),expanded)) {exhausted=true;break;}
            beam=retain(expanded,options.beamWidth,options.globalPsnrThreshold);
        }
    }
    for(std::size_t restart=0;restart<options.restarts && !exhausted;++restart) {
        if(restart) beam=retain(seeds,options.beamWidth,options.globalPsnrThreshold);
        for(std::size_t pass=0;pass<=options.refinementRounds && !exhausted;++pass) {
            auto traversal=order;if((restart+pass)%2) std::reverse(traversal.begin(),traversal.end());
            for(const auto& target:traversal) {
                auto expanded=beam;const auto total=beam.size();std::size_t done=0;
                for(const auto& state:beam) if(!visit(state,target,restart,pass+1,++done,total,expanded)) {exhausted=true;break;}
                beam=retain(expanded,options.beamWidth,options.globalPsnrThreshold);
                if(exhausted) break;
            }
        }
    }
    // Every returned/eligible state is replayed through the original complete
    // circuit. No off-path configuration is ever offered as executable state.
    const auto replayBegin=Clock::now();std::map<std::string,State> finalists;
    for(const auto& state:legalArchive.states()) finalists.emplace(key(state.configuration),state);
    for(const auto& state:numericArchive.states()) finalists.emplace(key(state.configuration),state);
    for(auto& entry:finalists) {
        auto& state=entry.second;
        const auto actual=measureBitflipQuality(reference,app.runApprox(image,state.configuration),mask);
        verifyMetrics(state.metrics,actual);state.metrics=actual;
        if(!result.bestLegal || better(state,*result.bestLegal)) result.bestLegal=state;
        if(feasible(actual,options.globalPsnrThreshold) && actual.roiMse>result.baseline.roiMse+1e-10)
            result.candidates.push_back(state);
    }
    std::stable_sort(result.candidates.begin(),result.candidates.end(),better);
    result.stats.finalReplaySeconds=std::chrono::duration<double>(Clock::now()-replayBegin).count();
    return result;
}
}

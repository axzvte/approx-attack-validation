#include "analysis/circuit_bitflip_joint.hpp"
#include "applications/conv.hpp"
#include "applications/sobel.hpp"
#include "applications/dct.hpp"
#include "core/bitflip_payload.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace analysis {
namespace {
using Config=JointBitflipConfiguration;
using State=JointBitflipState;
using Bin=JointBitflipBin;
using Sum=std::int64_t;
constexpr std::array<core::MonitorSignal,3> signals{core::MonitorSignal::Input1,
    core::MonitorSignal::Input2,core::MonitorSignal::BaselineOutput};
enum class Kind {Conv,Sobel,Dct};
Kind kindOf(const core::Application& app) {
    if(dynamic_cast<const applications::ConvApplication*>(&app)) return Kind::Conv;
    if(dynamic_cast<const applications::SobelApplication*>(&app)) return Kind::Sobel;
    if(dynamic_cast<const applications::DctApplication*>(&app)) return Kind::Dct;
    throw std::invalid_argument("Circuit bit-flip search supports Conv, Sobel and DCT only.");
}
int monitored(core::MonitorSignal m,int a,int b,int raw) {
    switch(m) {
        case core::MonitorSignal::Input1:return a;
        case core::MonitorSignal::Input2:return b;
        case core::MonitorSignal::BaselineOutput:return raw;
    }
    throw std::invalid_argument("Unknown circuit monitor signal.");
}
Config without(const Config& c,int node) {
    Config result;for(const auto& x:c) if(x.nodeId!=node) result.push_back(x);return result;
}
std::string key(Config c) {
    std::sort(c.begin(),c.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});
    std::ostringstream out;
    for(const auto& x:c) out<<x.nodeId<<':'<<x.bitIndex<<':'<<int(x.monitorInput)<<':'<<x.lower<<':'<<x.upper<<';';
    return out.str();
}
cv::Mat binaryMask(const cv::Mat& image,const cv::Mat& mask) {
    if(image.empty() || image.type()!=CV_8UC1 || mask.type()!=CV_8UC1 || image.size()!=mask.size())
        throw std::invalid_argument("Circuit search needs matching nonempty grayscale image and mask.");
    cv::Mat binary;cv::compare(mask,127,binary,cv::CMP_GT);
    const auto count=cv::countNonZero(binary);
    if(!count || std::size_t(count)==binary.total())
        throw std::invalid_argument("Circuit search needs both ROI (mask >=128) and background pixels.");
    return binary;
}
void validateConfig(const Config& config,std::size_t nodes) {
    std::set<int> seen;
    for(const auto& c:config) {
        core::validateOutputPayload(c);
        if(c.payload!=core::AttackPayload::BitFlip || c.nodeId<0 || std::size_t(c.nodeId)>=nodes ||
                c.lower>c.upper || !seen.insert(c.nodeId).second)
            throw std::invalid_argument("Invalid circuit bit-flip configuration.");
    }
}
bool rangeError(const std::runtime_error& e) {
    // Only these two arithmetic-width failures are legal search rejections.
    // Unexpected failures (trace shape, unknown units, OpenCV, etc.) propagate.
    const std::string text=e.what();
    return text=="Value exceeds signed 12-bit range." ||
        text=="Bit-flip input exceeds signed 13-bit output range.";
}
Sum square(int n) {return Sum(n)*n;}
struct Unit {bool exact;approximate::ApproxUnitId id;};
struct Pixel {std::array<core::AddSample,11> trace{};int reference=0;bool roi=false;Sum offSse=0;bool offValid=false;};
struct Context {
    Kind kind=Kind::Conv;int target=0;Config others;
    std::vector<Unit> units;std::vector<std::optional<core::AttackConfig>> active;
    std::vector<Pixel> pixels;
    std::uint64_t roi=0,non=0;Sum roiSse=0,nonSse=0,invalid=0;
};
template<class Units> std::vector<Unit> unitsFor(const Units& input) {
    std::vector<Unit> out;for(const auto& x:input) out.push_back({x.exact,x.unit});return out;
}
// Replay the actual dependency graph from constant leaf inputs in the trace.
// Branches not downstream of the target remain unchanged; signed and absolute
// edges are explicit. Every downstream monitor uses freshly computed operands.
bool replay(const Context& c,const Pixel& p,int bit,bool targetOn,int& output) {
    std::array<int,11> values{};
    for(std::size_t n=0;n<c.units.size();++n) {
        int a=p.trace[n].input1,b=p.trace[n].input2;
        if(c.kind==Kind::Conv) {
            switch(n) {
                case 4:a=values[0];b=values[1];break;
                case 5:a=values[2];b=values[3];break;
                case 6:a=values[4];b=values[5];break;
                case 7:a=values[6];break;
            }
        } else {
            switch(n) {
                case 1:a=values[0];break;
                case 3:a=values[2];break;
                case 4:a=values[1];b=-values[3];break;
                case 6:a=values[5];break;
                case 8:a=values[7];break;
                case 9:a=values[6];b=-values[8];break;
                case 10:a=std::abs(values[4]);b=std::abs(values[9]);break;
            }
        }
        const auto& unit=c.units[n];
        if(!unit.exact && (a < -2048 || a>2047 || b < -2048 || b>2047)) return false;
        const int raw=unit.exact?a+b:approximate::addSigned12(a,b,unit.id);
        bool flip=false;int flipBit=bit;
        if(int(n)==c.target) flip=targetOn;
        else if(c.active[n]) {
            const auto& attack=*c.active[n];const auto v=monitored(attack.monitorInput,a,b,raw);
            flip=v>=attack.lower && v<=attack.upper;flipBit=attack.bitIndex;
        }
        if(flip && (raw < -4096 || raw>4095)) return false;
        values[n]=flip?core::flipNonSignOutputBit(raw,flipBit):raw;
    }
    const int value=values[c.units.size()-1];
    output=std::clamp(c.kind==Kind::Conv?std::abs(value):value,0,255);return true;
}
Context makeContext(const core::Application& app,const cv::Mat& image,const cv::Mat& mask,
    const cv::Mat& reference,const Config& current,int node) {
    Context c;c.kind=kindOf(app);c.target=node;c.others=without(current,node);
    if(c.kind==Kind::Dct) throw std::invalid_argument("DCT does not support additive conditional image profiles.");
    if(image.rows<3 || image.cols<3 || node<0 || std::size_t(node)>=app.addNodes().size())
        throw std::invalid_argument("Pixel profiling needs an interior pixel and a valid node.");
    validateConfig(current,app.addNodes().size());
    if(c.kind==Kind::Conv) c.units=unitsFor(dynamic_cast<const applications::ConvApplication&>(app).baselineConfig());
    else c.units=unitsFor(dynamic_cast<const applications::SobelApplication&>(app).baselineConfig());
    c.active.resize(c.units.size());for(const auto& attack:c.others) c.active[attack.nodeId]=attack;
    std::vector<core::AddSample> samples;app.collectConfiguredAddSamples(image,mask,current,samples);
    const auto expected=std::size_t(image.rows-2)*(image.cols-2)*c.units.size();
    if(samples.size()!=expected) throw std::runtime_error("Unexpected circuit pixel trace size.");
    c.roi=cv::countNonZero(mask);c.non=mask.total()-c.roi;
    std::size_t offset=0;c.pixels.reserve(expected/c.units.size());
    for(int r=1;r<image.rows-1;++r) for(int col=1;col<image.cols-1;++col) {
        Pixel p;p.reference=reference.at<unsigned char>(r,col);p.roi=mask.at<unsigned char>(r,col)>=128;
        for(std::size_t n=0;n<c.units.size();++n) {
            const auto& sample=samples[offset++];
            if(sample.nodeId!=int(n)) throw std::runtime_error("Unexpected circuit pixel trace order.");
            p.trace[n]=sample;
        }
        int value=0;p.offValid=replay(c,p,0,false,value);
        if(p.offValid) {p.offSse=square(value-p.reference);(p.roi?c.roiSse:c.nonSse)+=p.offSse;}
        else ++c.invalid;
        c.pixels.push_back(std::move(p));
    }
    // Both original application borders are identical to their exact borders.
    return c;
}
std::array<JointBitflipProfile,3> profilesFor(const Context& c,int bit) {
    core::flipNonSignOutputBit(0,bit);std::array<JointBitflipProfile,3> out;
    for(std::size_t m=0;m<signals.size();++m) {
        auto& p=out[m];p.others=c.others;p.node=c.target;p.bit=bit;p.monitor=signals[m];
        p.roiPixels=c.roi;p.nonRoiPixels=c.non;p.offRoiSse=c.roiSse;p.offNonRoiSse=c.nonSse;p.offInvalid=c.invalid;
    }
    for(const auto& pixel:c.pixels) {
        int final=0;const bool valid=replay(c,pixel,bit,true,final);
        const Sum delta=(valid?square(final-pixel.reference):0)-pixel.offSse;
        const auto& sample=pixel.trace[c.target];
        for(std::size_t m=0;m<signals.size();++m) {
            auto& bin=out[m].bins[monitored(signals[m],sample.input1,sample.input2,sample.baselineOutput)];
            if(pixel.roi) {bin.roiDelta+=delta;++bin.roiHits;} else {bin.nonRoiDelta+=delta;++bin.nonRoiHits;}
            bin.invalidDelta+=Sum(!valid)-Sum(!pixel.offValid);
        }
    }
    return out;
}
bool feasible(const BitflipQuality& q,double threshold) {return q.globalPsnr()+1e-10>=threshold;}
using Rank=std::pair<double,double>;
Rank rankFor(const BitflipQuality& q,int role) {
    switch(role) {
        case 0:case 1:return {q.roiMse,-q.globalMse};
        case 2:return {-q.globalMse,q.roiMse};
        case 3:return {-q.nonRoiMse,q.roiMse};
        case 4:return {q.roiMse-q.globalMse,-q.globalMse};
        default:return {q.roiMse-4*q.globalMse,q.roiMse};
    }
}
std::vector<State> retain(const std::vector<State>& input,std::size_t width,double threshold,std::size_t maxActive) {
    // Dynamically sized buckets: configured-count diversity is independent of
    // beam width and does not impose Sharpening's four-node architecture.
    std::vector<std::map<std::string,State>> buckets(maxActive+1);
    for(const auto& state:input) if(state.configuration.size()<=maxActive)
        buckets[state.configuration.size()].emplace(key(state.configuration),state);
    std::vector<State> out;
    for(auto& bucket:buckets) {
        std::vector<State> states;for(auto& x:bucket) states.push_back(std::move(x.second));
        std::array<std::vector<std::size_t>,6> orders;
        for(int role=0;role<6;++role) {
            for(std::size_t i=0;i<states.size();++i) if(role!=0 || feasible(states[i].metrics,threshold)) orders[role].push_back(i);
            std::stable_sort(orders[role].begin(),orders[role].end(),[&](auto a,auto b){return rankFor(states[a].metrics,role)>rankFor(states[b].metrics,role);});
        }
        std::set<std::size_t> chosen;
        for(std::size_t depth=0;chosen.size()<width && depth<states.size();++depth)
            for(int role:{0,2,1,3,4,5}) if(depth<orders[role].size() && chosen.size()<width) chosen.insert(orders[role][depth]);
        for(auto i:chosen) out.push_back(std::move(states[i]));
    }
    return out;
}
class Representatives {
    std::array<std::vector<State>,6> slots;std::size_t capacity;double threshold;
public:
    Representatives(std::size_t cap,double t):capacity(cap),threshold(t) {}
    template<class Maker> void offer(const BitflipQuality& q,const Maker& maker) {
        std::optional<State> candidate;
        for(int role=0;role<6;++role) {
            if(role==0 && !feasible(q,threshold)) continue;
            auto& list=slots[role];
            if(list.size()>=capacity && rankFor(q,role)<=rankFor(list.back().metrics,role)) continue;
            if(!candidate) candidate=State{maker(),q};const auto identity=key(candidate->configuration);
            if(std::any_of(list.begin(),list.end(),[&](const auto& s){return key(s.configuration)==identity;})) continue;
            list.push_back(*candidate);
            std::stable_sort(list.begin(),list.end(),[&](const auto& a,const auto& b){return rankFor(a.metrics,role)>rankFor(b.metrics,role);});
            if(list.size()>capacity) list.resize(capacity);
        }
    }
    std::vector<State> states() const {
        std::map<std::string,State> unique;for(const auto& list:slots) for(const auto& s:list) unique.emplace(key(s.configuration),s);
        std::vector<State> out;for(const auto& x:unique) out.push_back(x.second);return out;
    }
};
// Observed-value short windows plus an evenly spaced endpoint grid. Zero asks
// for exhaustive intervals, explicitly, and can be expensive on large traces.
template<class Evaluate> void intervals(std::size_t size,std::size_t budget,const Evaluate& evaluate) {
    if(!size) return;
    if(!budget) {for(std::size_t lo=0;lo<size;++lo) for(std::size_t hi=lo;hi<size;++hi) evaluate(lo,hi);return;}
    std::set<std::pair<std::size_t,std::size_t>> seen;
    for(std::size_t lo=0;lo<size;++lo) for(std::size_t width:{1,2,3,4,8,16})
        if(width<=size-lo) seen.emplace(lo,lo+width-1);
    const auto count=std::min(size,budget);
    for(std::size_t i=0;i<count;++i) for(std::size_t j=i;j<count;++j)
        seen.emplace(count==1?0:i*(size-1)/(count-1),count==1?0:j*(size-1)/(count-1));
    for(const auto& pair:seen) evaluate(pair.first,pair.second);
}
core::AttackConfig attack(int node,int bit,core::MonitorSignal signal,int lo,int hi) {
    return {node,approximate::ApproxUnitId::Add12se5RP,signal,lo,hi,core::AttackPayload::BitFlip,bit};
}
std::vector<State> expandPixels(const core::Application& app,const cv::Mat& image,const cv::Mat& mask,
    const cv::Mat& reference,const Config& current,int node,const CircuitBitflipOptions& options,CircuitBitflipStats& stats) {
    const auto context=makeContext(app,image,mask,reference,current,node);++stats.contextsBuilt;
    Representatives pool(options.representativeCount,options.globalPsnrThreshold);
    if(!context.invalid) {
        const BitflipQuality q{double(context.roiSse+context.nonSse)/(context.roi+context.non),
            double(context.roiSse)/context.roi,double(context.nonSse)/context.non};
        pool.offer(q,[&]{return context.others;});
    }
    for(int bit:options.bits) for(const auto& profile:profilesFor(context,bit)) {
        std::vector<int> values;std::vector<Bin> prefix(1);
        for(const auto& x:profile.bins) {
            values.push_back(x.first);auto sum=prefix.back();const auto& b=x.second;
            sum.roiDelta+=b.roiDelta;sum.nonRoiDelta+=b.nonRoiDelta;sum.invalidDelta+=b.invalidDelta;
            sum.roiHits+=b.roiHits;sum.nonRoiHits+=b.nonRoiHits;prefix.push_back(sum);
        }
        auto summary=profile;summary.bins.clear();summary.bins.emplace(0,Bin{});
        intervals(values.size(),options.boundaryBudget,[&](std::size_t lo,std::size_t hi) {
            ++stats.evaluatedIntervals;
            // Reuse the public exact scorer with one pre-summed bin, avoiding
            // rescanning all histogram bins for every enumerated interval.
            auto delta=prefix[hi+1];const auto& before=prefix[lo];
            delta.roiDelta-=before.roiDelta;delta.nonRoiDelta-=before.nonRoiDelta;delta.invalidDelta-=before.invalidDelta;
            delta.roiHits-=before.roiHits;delta.nonRoiHits-=before.nonRoiHits;summary.bins.begin()->second=delta;
            const auto score=scoreJointInterval(summary,0,0);
            if(!score.valid) {++stats.rangeRejected;return;}
            if(!score.roiTriggers && !score.nonRoiTriggers) return;
            pool.offer(score.metrics,[&] {auto config=profile.others;config.push_back(attack(node,bit,profile.monitor,values[lo],values[hi]));return config;});
        });
    }
    return pool.states();
}
struct Local {
    double roiDelta=0,nonDelta=0,roiSigned=0,nonSigned=0,roiHits=0,hits=0;
    Local& operator+=(const Local& b) {
        roiDelta+=b.roiDelta;nonDelta+=b.nonDelta;roiSigned+=b.roiSigned;nonSigned+=b.nonSigned;
        roiHits+=b.roiHits;hits+=b.hits;return *this;
    }
};
Local subtract(Local a,const Local& b) {
    a.roiDelta-=b.roiDelta;a.nonDelta-=b.nonDelta;a.roiSigned-=b.roiSigned;a.nonSigned-=b.nonSigned;
    a.roiHits-=b.roiHits;a.hits-=b.hits;return a;
}
struct Proposal {core::AttackConfig config;Local local;};
Rank proposalRank(const Proposal& p,int role) {
    const auto& x=p.local;
    switch(role) {
        case 0:return {x.roiDelta,-x.nonDelta};
        case 1:return {-x.roiDelta-x.nonDelta,x.roiHits}; // signed error compensation
        case 2:return {std::abs(x.roiSigned),-std::abs(x.nonSigned)};
        case 3:return {x.roiHits,-x.hits};
        case 4:return {-std::abs(x.roiSigned+x.nonSigned),x.roiDelta};
        default:return {-std::abs(x.roiDelta+x.nonDelta),x.roiHits};
    }
}
bool sameProposal(const Proposal& a,const Proposal& b) {
    const auto& x=a.config;const auto& y=b.config;
    return x.bitIndex==y.bitIndex && x.monitorInput==y.monitorInput && x.lower==y.lower && x.upper==y.upper;
}
std::vector<Proposal> proposeDct(const std::vector<const core::AddSample*>& targetCalls,int node,
    const CircuitBitflipOptions& options,CircuitBitflipStats& stats) {
    using Lists=std::array<std::vector<Proposal>,6>;
    std::vector<Lists> groups;
    for(int bit:options.bits) for(auto signal:signals) {
        std::map<int,Local> bins;
        for(const auto* call:targetCalls) {
            const auto& sample=*call;
            // A current trace is executable, including repeated row/column
            // invocations. Local differences only order a bounded shortlist;
            // they NEVER become image quality, validity, or beam state scores.
            if(sample.baselineOutput < -4096 || sample.baselineOutput>4095) continue;
            const int replacement=core::flipNonSignOutputBit(sample.baselineOutput,bit);
            const double oldError=double(sample.output)-sample.input1-sample.input2;
            const double newError=double(replacement)-sample.input1-sample.input2;
            const double delta=newError*newError-oldError*oldError;
            const double signedDelta=double(replacement)-sample.output;
            const double weight=sample.roiWeight;
            auto& bin=bins[monitored(signal,sample.input1,sample.input2,sample.baselineOutput)];
            bin+={delta*weight,delta*(1-weight),signedDelta*weight,signedDelta*(1-weight),weight,1};
        }
        std::vector<int> values;std::vector<Local> prefix(1);
        for(const auto& entry:bins) {values.push_back(entry.first);auto x=prefix.back();x+=entry.second;prefix.push_back(x);}
        Lists lists;
        intervals(values.size(),options.boundaryBudget,[&](std::size_t lo,std::size_t hi) {
            ++stats.evaluatedIntervals;
            Proposal p{attack(node,bit,signal,values[lo],values[hi]),subtract(prefix[hi+1],prefix[lo])};
            for(int role=0;role<6;++role) {
                auto& list=lists[role];
                if(list.size()>=options.representativeCount && proposalRank(p,role)<=proposalRank(list.back(),role)) continue;
                list.push_back(p);std::stable_sort(list.begin(),list.end(),[&](const auto& a,const auto& b){return proposalRank(a,role)>proposalRank(b,role);});
                if(list.size()>options.representativeCount) list.resize(options.representativeCount);
            }
        });
        if(!values.empty()) groups.push_back(std::move(lists));
    }
    std::vector<Proposal> out;
    auto offer=[&](const Proposal& p) {
        if(out.size()<options.dctFullCandidates && std::none_of(out.begin(),out.end(),[&](const auto& old){return sameProposal(old,p);})) out.push_back(p);
    };
    // Spread the initial slots across the entire bit/signal space, then fill
    // remaining slots from alternative roles. This avoids a shortlist consumed
    // by near-duplicate low-bit intervals of one monitor.
    const auto first=std::min(options.dctFullCandidates,groups.size());
    for(std::size_t i=0;i<first;++i) {
        const auto g=i*groups.size()/first;
        for(std::size_t alternative=0;alternative<6;++alternative) {
            const auto& list=groups[g][(i+alternative)%6];
            if(!list.empty()) {const auto before=out.size();offer(list.front());if(out.size()>before) break;}
        }
    }
    for(std::size_t depth=0;depth<options.representativeCount;++depth) for(int role=0;role<6;++role)
        for(const auto& group:groups) if(depth<group[role].size()) offer(group[role][depth]);
    stats.proposalsRetained+=out.size();return out;
}
struct Budget {std::uint64_t contexts,full;};
std::vector<State> expandDct(const core::Application& app,const cv::Mat& image,const cv::Mat& mask,
    const cv::Mat& reference,const Config& current,int node,const CircuitBitflipOptions& options,
    CircuitBitflipStats& stats,const Budget& budget) {
    std::vector<core::AddSample> samples;app.collectConfiguredAddSamples(image,mask,current,samples);++stats.contextsBuilt;
    const auto nodes=app.addNodes().size();
    const auto expected=std::size_t((image.rows+7)/8)*std::size_t((image.cols+7)/8)*16*nodes;
    if(samples.size()!=expected) throw std::runtime_error("Unexpected DCT repeated-call trace size.");
    std::vector<const core::AddSample*> targetCalls;targetCalls.reserve(expected/nodes);
    for(std::size_t i=0;i<samples.size();++i) {
        if(samples[i].nodeId!=int(i%nodes) || !std::isfinite(samples[i].roiWeight) ||
                samples[i].roiWeight<0 || samples[i].roiWeight>1)
            throw std::runtime_error("Unexpected DCT repeated-call trace order or ROI weight.");
        if(samples[i].nodeId==node) targetCalls.push_back(&samples[i]);
    }
    Representatives pool(options.representativeCount,options.globalPsnrThreshold);
    const auto others=without(current,node);
    auto evaluate=[&](const Config& config) {
        if(stats.fullEvaluations>=budget.full) {stats.fullBudgetExhausted=true;return;}
        ++stats.fullEvaluations;
        cv::Mat output;
        try {output=app.runApprox(image,config);} catch(const std::runtime_error& e) {
            if(!rangeError(e)) throw;++stats.rangeRejected;return;
        }
        const auto quality=measureBitflipQuality(reference,output,mask);
        pool.offer(quality,[&]{return config;});
    };
    // No target-removal trace collection: removal can be illegal even though
    // current is legal. Candidate intervals always originate in CURRENT traces.
    if(others.size()!=current.size()) evaluate(others);
    const auto proposals=proposeDct(targetCalls,node,options,stats);
    for(const auto& p:proposals) {
        if(stats.fullEvaluations>=budget.full) {stats.fullBudgetExhausted=true;break;}
        auto config=others;config.push_back(p.config);evaluate(config);
    }
    return pool.states();
}
}

JointBitflipProfile profileCircuitBitflip(const core::Application& app,const cv::Mat& image,
    const cv::Mat& mask,const Config& current,int node,int bit,core::MonitorSignal signal) {
    if(kindOf(app)==Kind::Dct) throw std::invalid_argument("DCT does not support additive conditional image profiles.");
    monitored(signal,0,0,0);core::flipNonSignOutputBit(0,bit);
    const auto binary=binaryMask(image,mask);
    auto profiles=profilesFor(makeContext(app,image,binary,app.runExact(image),current,node),bit);
    for(auto& p:profiles) if(p.monitor==signal) return std::move(p);
    throw std::invalid_argument("Unknown circuit monitor signal.");
}
CircuitBitflipResult searchCircuitBitflips(const core::Application& app,const cv::Mat& image,
    const cv::Mat& mask,const CircuitBitflipOptions& options,const std::function<void(const JointBitflipProgress&)>& progress) {
    const auto kind=kindOf(app);const auto nodes=app.addNodes().size();
    if(options.bits.empty() || !options.beamWidth || !options.representativeCount || !options.candidatesPerCount ||
            !options.restarts || options.boundaryBudget==1 || !std::isfinite(options.globalPsnrThreshold) ||
            !options.maxContexts || !options.maxFullEvaluations || !options.dctFullCandidates)
        throw std::invalid_argument("Invalid circuit bit-flip search options.");
    std::set<int> seen;for(int bit:options.bits) {
        core::flipNonSignOutputBit(0,bit);if(!seen.insert(bit).second) throw std::invalid_argument("Duplicate circuit candidate bit.");
    }
    const auto maxActive=options.maxActiveNodes?std::min(nodes,options.maxActiveNodes):nodes;
    const auto binary=binaryMask(image,mask);const auto reference=app.runExact(image);
    CircuitBitflipResult result;
    result.baseline=measureBitflipQuality(reference,app.runApprox(image,{}),binary);result.stats.fullEvaluations=1;
    const State initial{{},result.baseline};std::vector<State> archive;
    std::map<std::string,std::vector<State>> cache;
    for(std::size_t restart=0;restart<options.restarts;++restart) {
        // Cumulative budget shares reserve work for alternate visitation orders.
        // Unused earlier shares are automatically available to later restarts.
        const auto share=[&](std::uint64_t total) {
            const auto count=std::uint64_t(options.restarts),index=std::uint64_t(restart+1);
            return (total/count)*index+std::min(total%count,index);
        };
        const Budget budget{share(options.maxContexts),1+share(options.maxFullEvaluations-1)};
        std::vector<State> beam{initial};bool exhausted=false;
        for(std::size_t pass=0;pass<=options.refinementRounds && !exhausted;++pass) {
            std::vector<int> order;for(std::size_t n=0;n<nodes;++n) order.push_back(int(n));
            if((restart+pass)%2) std::reverse(order.begin(),order.end());
            for(int node:order) {
                std::vector<State> expanded=beam;std::size_t done=0;
                for(const auto& state:beam) {
                    if(without(state.configuration,node).size()>=maxActive) {++done;continue;}
                    // Static DCT nodes repeat within and across transforms:
                    // their own current interval changes later monitored calls.
                    const auto identity=std::to_string(node)+"|"+key(kind==Kind::Dct?state.configuration:without(state.configuration,node));
                    auto found=cache.find(identity);
                    if(found!=cache.end()) {++result.stats.cacheHits;expanded.insert(expanded.end(),found->second.begin(),found->second.end());}
                    else {
                        if(result.stats.contextsBuilt>=budget.contexts) {result.stats.contextBudgetExhausted=true;exhausted=true;break;}
                        if(kind==Kind::Dct && result.stats.fullEvaluations>=budget.full) {result.stats.fullBudgetExhausted=true;exhausted=true;break;}
                        auto states=kind==Kind::Dct?expandDct(app,image,binary,reference,state.configuration,node,options,result.stats,budget):
                            expandPixels(app,image,binary,reference,state.configuration,node,options,result.stats);
                        // A partial DCT expansion is not reusable as a completed
                        // context under the next restart's larger budget.
                        if(kind!=Kind::Dct || result.stats.fullEvaluations<budget.full) cache.emplace(identity,states);
                        expanded.insert(expanded.end(),states.begin(),states.end());
                    }
                    if(progress) progress({restart,pass,std::size_t(node),++done,beam.size(),result.stats});
                }
                beam=retain(expanded,options.beamWidth,options.globalPsnrThreshold,maxActive);
                for(const auto& state:beam) {
                    if(!feasible(state.metrics,options.globalPsnrThreshold)) ++result.stats.retainedQualityFailures;
                    if(state.configuration.size()>=2 && feasible(state.metrics,options.globalPsnrThreshold) &&
                            state.metrics.roiMse>result.baseline.roiMse+1e-10) archive.push_back(state);
                }
                archive=retain(archive,options.candidatesPerCount,options.globalPsnrThreshold,maxActive);
                if(exhausted) break;
            }
        }
    }
    result.candidates=std::move(archive);
    std::stable_sort(result.candidates.begin(),result.candidates.end(),[](const auto& a,const auto& b) {
        if(a.configuration.size()!=b.configuration.size()) return a.configuration.size()<b.configuration.size();
        if(a.metrics.roiMse!=b.metrics.roiMse) return a.metrics.roiMse>b.metrics.roiMse;
        return a.metrics.globalMse<b.metrics.globalMse;
    });
    return result;
}
}

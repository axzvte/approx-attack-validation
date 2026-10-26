#include "analysis/circuit_bitflip_joint.hpp"
#include "analysis/circuit_bitflip_fixed.hpp"
#include "analysis/bitflip_sensitivity.hpp"
#include "applications/conv.hpp"
#include "applications/sobel.hpp"
#include "applications/dct.hpp"
#include "core/bitflip_payload.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <map>
#include <limits>
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
approximate::ApproxUnitId applicationBaselineUnit(const core::Application& app,int node) {
    switch(kindOf(app)) {
        case Kind::Conv:return dynamic_cast<const applications::ConvApplication&>(app).baselineConfig()[node].unit;
        case Kind::Sobel:return dynamic_cast<const applications::SobelApplication&>(app).baselineConfig()[node].unit;
        case Kind::Dct:return dynamic_cast<const applications::DctApplication&>(app).baselineConfig()[node].unit;
    }
    throw std::runtime_error("Unknown circuit application.");
}
void validateStructureSeed(const core::Application& app,const Config& config,std::size_t maxActive) {
    if(config.empty() || config.size()>maxActive)
        throw std::invalid_argument("Structural seed requires 1..maxActiveNodes native nodes.");
    std::set<int> seen;
    for(const auto& c:config) {
        if(c.nodeId<0 || std::size_t(c.nodeId)>=app.addNodes().size() || !seen.insert(c.nodeId).second ||
                c.payload!=core::AttackPayload::BitFlip || c.bitIndex<0 || c.bitIndex>=12)
            throw std::invalid_argument("Invalid structural seed node, payload or bit.");
        const bool input=c.monitorInput==core::MonitorSignal::Input1 || c.monitorInput==core::MonitorSignal::Input2;
        if(!input && c.monitorInput!=core::MonitorSignal::BaselineOutput)
            throw std::invalid_argument("Invalid structural seed monitor.");
        if(c.lower>c.upper || c.lower<(input?-2048:-4096) || c.upper>(input?2047:4095) ||
                c.unit!=applicationBaselineUnit(app,c.nodeId))
            throw std::invalid_argument("Structural seed interval or baseline unit is invalid.");
    }
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
    const cv::Mat& reference,const Config& current,int node,
    const std::vector<core::AddSample>* cachedSamples=nullptr) {
    Context c;c.kind=kindOf(app);c.target=node;c.others=without(current,node);
    if(c.kind==Kind::Dct) throw std::invalid_argument("DCT does not support additive conditional image profiles.");
    if(image.rows<3 || image.cols<3 || node<0 || std::size_t(node)>=app.addNodes().size())
        throw std::invalid_argument("Pixel profiling needs an interior pixel and a valid node.");
    validateConfig(current,app.addNodes().size());
    if(c.kind==Kind::Conv) c.units=unitsFor(dynamic_cast<const applications::ConvApplication&>(app).baselineConfig());
    else c.units=unitsFor(dynamic_cast<const applications::SobelApplication&>(app).baselineConfig());
    c.active.resize(c.units.size());for(const auto& attack:c.others) c.active[attack.nodeId]=attack;
    std::vector<core::AddSample> collected;
    if(!cachedSamples) app.collectConfiguredAddSamples(image,mask,current,collected);
    const auto& samples=cachedSamples?*cachedSamples:collected;
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
std::array<JointBitflipProfile,3> profilesFor(const Context& c,int bit,
    const std::array<bool,3>* selected=nullptr) {
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
            if(selected && !(*selected)[m]) continue;
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
std::vector<int> candidateBits(const Config& current,int node,const CircuitBitflipOptions& options,bool local) {
    auto bits=options.bits;
    // A warm structure can contain a bit outside the global discovery list.
    // Its existing interval must still be refinable without a structure edit.
    if(local) for(const auto& c:current) if(c.nodeId==node &&
            std::find(bits.begin(),bits.end(),c.bitIndex)==bits.end()) bits.push_back(c.bitIndex);
    return bits;
}
std::vector<State> expandPixels(const core::Application& app,const cv::Mat& image,const cv::Mat& mask,
    const cv::Mat& reference,const Config& current,int node,const CircuitBitflipOptions& options,CircuitBitflipStats& stats,
    const std::vector<core::AddSample>* cachedSamples=nullptr,
    const std::function<bool(const Config&)>& allowed={}) {
    const auto context=makeContext(app,image,mask,reference,current,node,cachedSamples);++stats.contextsBuilt;
    Representatives pool(options.representativeCount,options.globalPsnrThreshold);
    if(!context.invalid) {
        const BitflipQuality q{double(context.roiSse+context.nonSse)/(context.roi+context.non),
            double(context.roiSse)/context.roi,double(context.nonSse)/context.non};
        if(!allowed || allowed(context.others)) pool.offer(q,[&]{return context.others;});
        else ++stats.localCandidatesFiltered;
    }
    for(int bit:candidateBits(current,node,options,bool(allowed))) {
      std::array<bool,3> selected{true,true,true};bool any=false;
      for(std::size_t m=0;m<signals.size();++m) {
        auto structure=context.others;structure.push_back(attack(node,bit,signals[m],0,0));
        selected[m]=!allowed || allowed(structure);any=any || selected[m];
        if(!selected[m]) ++stats.localCandidatesFiltered;
      }
      if(!any) continue; // Avoid the per-pixel graph replay for disallowed bits.
      const auto profiles=profilesFor(context,bit,&selected);
      for(std::size_t m=0;m<signals.size();++m) {
        if(!selected[m]) continue;
        const auto& profile=profiles[m];
        std::vector<int> values;std::vector<Bin> prefix(1);
        for(const auto& x:profile.bins) {
            const bool input=profile.monitor!=core::MonitorSignal::BaselineOutput;
            if(allowed && (x.first<(input?-2048:-4096) || x.first>(input?2047:4095))) continue;
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
            pool.offer(score.metrics,[&] {
                auto config=profile.others;auto candidate=attack(node,bit,profile.monitor,values[lo],values[hi]);
                if(allowed) candidate.unit=applicationBaselineUnit(app,node);
                config.push_back(candidate);return config;
            });
        });
      }
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
    const CircuitBitflipOptions& options,CircuitBitflipStats& stats,const Config& others,
    const std::function<bool(const Config&)>& allowed) {
    using Lists=std::array<std::vector<Proposal>,6>;
    std::vector<Lists> groups;
    for(int bit:options.bits) for(auto signal:signals) {
        auto structure=others;structure.push_back(attack(node,bit,signal,0,0));
        if(allowed && !allowed(structure)) {++stats.localCandidatesFiltered;continue;}
        std::map<int,Local> bins;
        for(const auto* call:targetCalls) {
            const auto& sample=*call;
            const int monitorValue=monitored(signal,sample.input1,sample.input2,sample.baselineOutput);
            const bool input=signal!=core::MonitorSignal::BaselineOutput;
            if(allowed && (monitorValue<(input?-2048:-4096) || monitorValue>(input?2047:4095))) continue;
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
            auto& bin=bins[monitorValue];
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
    CircuitBitflipStats& stats,const Budget& budget,const std::vector<core::AddSample>* cachedSamples=nullptr,
    const std::function<bool(const Config&)>& allowed={}) {
    std::vector<core::AddSample> owned;
    if(!cachedSamples) {app.collectConfiguredAddSamples(image,mask,current,owned);cachedSamples=&owned;}
    const auto& samples=*cachedSamples;++stats.contextsBuilt;
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
        if(allowed && !allowed(config)) {++stats.localCandidatesFiltered;return;}
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
    auto proposalOptions=options;proposalOptions.bits=candidateBits(current,node,options,bool(allowed));
    const auto proposals=proposeDct(targetCalls,node,proposalOptions,stats,others,allowed);
    for(const auto& p:proposals) {
        if(stats.fullEvaluations>=budget.full) {stats.fullBudgetExhausted=true;break;}
        auto config=others;auto candidate=p.config;
        if(allowed) candidate.unit=applicationBaselineUnit(app,node);
        config.push_back(candidate);evaluate(config);
    }
    return pool.states();
}
}

std::size_t bitflipStructureDistance(const JointBitflipConfiguration& a,const JointBitflipConfiguration& b) {
    // A changed monitor and/or bit at an existing node is one edit. Replacing
    // the node identity is a removal plus an addition, independent of L/U.
    std::map<int,std::pair<core::MonitorSignal,int>> left,right;
    for(const auto& c:a) left.emplace(c.nodeId,std::make_pair(c.monitorInput,c.bitIndex));
    for(const auto& c:b) right.emplace(c.nodeId,std::make_pair(c.monitorInput,c.bitIndex));
    std::size_t edits=0;
    for(const auto& c:left) {
        const auto found=right.find(c.first);
        if(found==right.end() || c.second!=found->second) ++edits;
    }
    for(const auto& c:right) if(!left.count(c.first)) ++edits;
    return edits;
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
    for(const auto& seed:options.structureSeeds) validateStructureSeed(app,seed,maxActive);
    const auto binary=binaryMask(image,mask);const auto reference=app.runExact(image);
    CircuitBitflipResult result;
    result.baseline=measureBitflipQuality(reference,app.runApprox(image,{}),binary);result.stats.fullEvaluations=1;
    std::vector<State> structureSeeds;std::set<std::string> seedKeys;
    for(const auto& seed:options.structureSeeds) {
        if(!seedKeys.insert(key(seed)).second) continue;
        // Warm validation is initialization, as in fixed-H search. Search DCT
        // reconstructions still share maxFullEvaluations (baseline included).
        cv::Mat output;
        try {output=app.runApprox(image,seed);} catch(const std::runtime_error& e) {
            if(!rangeError(e)) throw;
            ++result.stats.rejectedStructureSeeds;++result.stats.rangeRejected;continue;
        }
        structureSeeds.push_back({seed,measureBitflipQuality(reference,output,binary)});
        ++result.stats.acceptedStructureSeeds;
    }
    std::vector<core::AddSample> cachedSamples;
    std::string traceKey;bool hasTrace=false;
    const auto trace=[&](const Config& current)->const std::vector<core::AddSample>& {
        std::ostringstream fingerprint;
        for(const auto& c:current) fingerprint<<c.nodeId<<':'<<int(c.unit)<<':'<<int(c.payload)<<':'<<int(c.monitorInput)<<':'<<c.bitIndex<<':'<<c.lower<<':'<<c.upper<<';';
        const auto id=fingerprint.str();
        if(!hasTrace || traceKey!=id) {
            hasTrace=false;cachedSamples.clear();app.collectConfiguredAddSamples(image,binary,current,cachedSamples);
            traceKey=id;hasTrace=true;++result.stats.traceBuilds;
        } else ++result.stats.traceCacheHits;
        return cachedSamples;
    };
    std::vector<int> nodeOrder;for(std::size_t n=0;n<nodes;++n) nodeOrder.push_back(int(n));
    if(options.sensitivityOrder) {
        const auto begin=std::chrono::steady_clock::now();
        const auto priority=prioritizeBitflipNodes(trace({}),nodes,options.bits);
        nodeOrder=priority.order;result.stats.sensitivityOrder=nodeOrder;
        for(auto count:priority.sampledPerNode) result.stats.sensitivitySamples+=count;
        result.stats.sensitivitySeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    }
    const State initial{{},result.baseline};std::vector<State> archive;
    std::map<std::string,std::vector<State>> cache;
    const auto remember=[&](const std::vector<State>& states) {
        for(const auto& state:states) {
            if(!feasible(state.metrics,options.globalPsnrThreshold)) ++result.stats.retainedQualityFailures;
            if(state.configuration.size()>=2 && feasible(state.metrics,options.globalPsnrThreshold) &&
                    state.metrics.roiMse>result.baseline.roiMse+1e-10) archive.push_back(state);
        }
        archive=retain(archive,options.candidatesPerCount,options.globalPsnrThreshold,maxActive);
    };
    remember(structureSeeds);
    // This predicate always compares with original, immutable anchors. The
    // retained beam is never promoted into a new structural radius centre.
    const std::function<bool(const Config&)> localAllowed=[&](const Config& c) {
        return std::any_of(structureSeeds.begin(),structureSeeds.end(),[&](const auto& seed) {
            return bitflipStructureDistance(c,seed.configuration)<=options.structureRadius;
        });
    };
    const auto phase=[&](bool local,const std::vector<State>& starts,std::size_t phaseContexts,bool legacy) {
      if(!phaseContexts) return;
      const auto contextStart=result.stats.contextsBuilt;
      const std::function<bool(const Config&)> allowed=local?localAllowed:std::function<bool(const Config&)>{};
      for(std::size_t restart=0;restart<options.restarts;++restart) {
        // Cumulative budget shares reserve work for alternate visitation orders.
        // Unused earlier shares are automatically available to later restarts.
        const auto share=[&](std::uint64_t total) {
            const auto count=std::uint64_t(options.restarts),index=std::uint64_t(restart+1);
            return (total/count)*index+std::min(total%count,index);
        };
        const Budget budget{contextStart+share(phaseContexts),legacy?1+share(options.maxFullEvaluations-1):options.maxFullEvaluations};
        auto beam=starts;bool exhausted=false;
        for(std::size_t pass=0;pass<=options.refinementRounds && !exhausted;++pass) {
            auto order=nodeOrder;
            if((restart+pass)%2) std::reverse(order.begin(),order.end());
            for(int node:order) {
                std::vector<State> expanded=beam;std::size_t done=0;
                for(const auto& state:beam) {
                    if(without(state.configuration,node).size()>=maxActive) {++done;continue;}
                    if(local) {
                        const auto others=without(state.configuration,node);
                        bool possible=others.size()!=state.configuration.size() && allowed(others);
                        for(int bit:candidateBits(state.configuration,node,options,true)) for(auto signal:signals) {
                            auto candidate=others;candidate.push_back(attack(node,bit,signal,0,0));
                            possible=possible || allowed(candidate);
                        }
                        if(!possible) {++result.stats.localCandidatesFiltered;++done;continue;}
                    }
                    // Static DCT nodes repeat within and across transforms:
                    // their own current interval changes later monitored calls.
                    auto identity=std::string(local?"local|":"global|")+std::to_string(node)+"|"+
                        key(kind==Kind::Dct?state.configuration:without(state.configuration,node));
                    // An existing out-of-list bit adds an interval-only group
                    // to local expansion; that group is part of cache identity.
                    if(local) for(int bit:candidateBits(state.configuration,node,options,true)) identity+="|"+std::to_string(bit);
                    auto found=cache.find(identity);
                    if(found!=cache.end()) {++result.stats.cacheHits;expanded.insert(expanded.end(),found->second.begin(),found->second.end());}
                    else {
                        if(result.stats.contextsBuilt>=budget.contexts) {
                            if(legacy || result.stats.contextsBuilt>=options.maxContexts) result.stats.contextBudgetExhausted=true;
                            exhausted=true;break;
                        }
                        if(kind==Kind::Dct && result.stats.fullEvaluations>=budget.full) {result.stats.fullBudgetExhausted=true;exhausted=true;break;}
                        const auto& currentSamples=trace(state.configuration);
                        auto states=kind==Kind::Dct?expandDct(app,image,binary,reference,state.configuration,node,options,result.stats,budget,&currentSamples,allowed):
                            expandPixels(app,image,binary,reference,state.configuration,node,options,result.stats,&currentSamples,allowed);
                        if(local) ++result.stats.localContexts;else ++result.stats.globalContexts;
                        // A partial DCT expansion is not reusable as a completed
                        // context under the next restart's larger budget.
                        if(kind!=Kind::Dct || result.stats.fullEvaluations<budget.full) cache.emplace(identity,states);
                        expanded.insert(expanded.end(),states.begin(),states.end());
                    }
                    if(progress) progress({restart,pass,std::size_t(node),++done,beam.size(),result.stats});
                }
                beam=retain(expanded,options.beamWidth,options.globalPsnrThreshold,maxActive);
                remember(beam);
                if(exhausted) break;
            }
        }
      }
    };
    if(structureSeeds.empty()) phase(false,{initial},options.maxContexts,true);
    else {
        const auto global=std::min(options.globalExplorationContexts,options.maxContexts);
        // Give the reserved baseline exploration first access to DCT's shared
        // full-score budget as well; local work cannot consume that reserve.
        phase(false,{initial},global,false);
        phase(true,structureSeeds,options.maxContexts-global,false);
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

namespace analysis {
namespace fixed_circuit {
using Clock=std::chrono::steady_clock;
using Signal=core::MonitorSignal;
double seconds(Clock::time_point start) {return std::chrono::duration<double>(Clock::now()-start).count();}
std::pair<int,int> domain(Signal s) {
    if(s==Signal::Input1 || s==Signal::Input2) return {-2048,2047};
    if(s==Signal::BaselineOutput) return {-4096,4095};
    throw std::invalid_argument("Unknown fixed circuit monitor signal.");
}
bool validHardware(const FixedBitflipHardware& h,std::size_t nodes) {
    if(h.size()<2 || h.size()>nodes) return false;
    std::set<int> seen;
    for(const auto& n:h) {
        if(n.nodeId<0 || std::size_t(n.nodeId)>=nodes || !seen.insert(n.nodeId).second ||
                n.bitIndex<0 || n.bitIndex>=12) return false;
        if(n.monitorInput!=Signal::Input1 && n.monitorInput!=Signal::Input2 && n.monitorInput!=Signal::BaselineOutput) return false;
    }
    return true;
}
void validateHardware(const FixedBitflipHardware& h,std::size_t nodes) {
    if(!validHardware(h,nodes)) throw std::invalid_argument("Fixed circuit hardware requires 2..native unique nodes, valid monitors and bits 0..11.");
}
void validateSeed(const Config& c,const FixedBitflipHardware& h,std::size_t nodes) {
    if(!matchesCircuitFixedBitflipHardware(c,h,nodes)) throw std::invalid_argument("Warm seed does not match fixed circuit hardware.");
    for(const auto& n:c) {
        const auto d=domain(n.monitorInput);
        if(n.lower>n.upper || n.lower<d.first || n.upper>d.second)
            throw std::invalid_argument("Fixed circuit interval exceeds its signed monitor domain.");
    }
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
int value(const core::AddSample& s,Signal m) {return monitored(m,s.input1,s.input2,s.baselineOutput);}
Config replaced(Config c,int node,int lo,int hi) {
    const auto it=std::find_if(c.begin(),c.end(),[&](const auto& x){return x.nodeId==node;});
    if(it==c.end()) throw std::runtime_error("Installed fixed circuit target disappeared.");
    it->lower=lo;it->upper=hi;return c;
}
approximate::ApproxUnitId baselineUnit(const core::Application& app,int node) {
    return applicationBaselineUnit(app,node);
}
std::optional<Config> dormant(const core::Application& app,const cv::Mat& image,const cv::Mat& mask,
    const FixedBitflipHardware& h,const cv::Mat& baseline,const std::vector<core::AddSample>& trace) {
    Config c;
    for(const auto& n:h) {
        std::set<int> seen;for(const auto& s:trace) if(s.nodeId==n.nodeId) seen.insert(value(s,n.monitorInput));
        const auto idle=unobserved(seen,n.monitorInput);if(!idle) return std::nullopt;
        c.push_back({n.nodeId,baselineUnit(app,n.nodeId),n.monitorInput,*idle,*idle,core::AttackPayload::BitFlip,n.bitIndex});
    }
    // No-hit is defined over the real configured trace, including every DCT
    // row/column invocation. This also guards against accidental graph changes.
    std::vector<core::AddSample> actual;app.collectConfiguredAddSamples(image,mask,c,actual);
    for(const auto& n:c) for(const auto& s:actual) if(s.nodeId==n.nodeId) {
        const int v=value(s,n.monitorInput);
        if(v>=n.lower && v<=n.upper) throw std::runtime_error("Dormant fixed circuit hardware unexpectedly triggers.");
    }
    if(measureBitflipQuality(baseline,app.runApprox(image,c),mask).globalMse!=0)
        throw std::runtime_error("Dormant fixed circuit hardware changed baseline output.");
    return c;
}
// Only H's selected monitor is profiled. The other fixed nodes still execute
// with refreshed downstream operands, signs and absolute-value edges.
JointBitflipProfile profile(const Context& c,const FixedBitflipNode& target) {
    JointBitflipProfile p;p.others=c.others;p.node=target.nodeId;p.bit=target.bitIndex;p.monitor=target.monitorInput;
    p.roiPixels=c.roi;p.nonRoiPixels=c.non;p.offRoiSse=c.roiSse;p.offNonRoiSse=c.nonSse;p.offInvalid=c.invalid;
    for(const auto& pixel:c.pixels) {
        int final=0;const bool valid=replay(c,pixel,target.bitIndex,true,final);
        auto& bin=p.bins[value(pixel.trace[target.nodeId],target.monitorInput)];
        const Sum delta=(valid?square(final-pixel.reference):0)-pixel.offSse;
        if(pixel.roi) {bin.roiDelta+=delta;++bin.roiHits;} else {bin.nonRoiDelta+=delta;++bin.nonRoiHits;}
        bin.invalidDelta+=Sum(!valid)-Sum(!pixel.offValid);
    }
    return p;
}
Bin sumBin(Bin a,const Bin& b,int sign=1) {
    a.roiDelta+=sign*b.roiDelta;a.nonRoiDelta+=sign*b.nonRoiDelta;a.invalidDelta+=sign*b.invalidDelta;
    a.roiHits+=sign*b.roiHits;a.nonRoiHits+=sign*b.nonRoiHits;
    return a;
}
std::vector<State> expandPixel(const core::Application& app,const cv::Mat& image,const cv::Mat& mask,
    const cv::Mat& reference,const Config& current,const FixedBitflipNode& target,
    const std::vector<core::AddSample>& samples,const CircuitFixedBitflipOptions& o,CircuitFixedBitflipStats& stats) {
    const auto begin=Clock::now();
    const auto p=profile(makeContext(app,image,mask,reference,current,target.nodeId,&samples),target);
    stats.profileSeconds+=seconds(begin);const auto intervalBegin=Clock::now();
    std::vector<int> values;std::vector<Bin> prefix(1);std::set<int> seen;
    const auto d=domain(target.monitorInput);
    for(const auto& x:p.bins) {
        seen.insert(x.first);
        if(x.first<d.first || x.first>d.second) continue;
        values.push_back(x.first);prefix.push_back(sumBin(prefix.back(),x.second));
    }
    auto summary=p;summary.bins.clear();summary.bins.emplace(0,Bin{});
    Representatives pool(o.representativeCount,o.globalPsnrThreshold);
    const auto evaluate=[&](int lo,int hi,const Bin& bin) {
        ++stats.evaluatedIntervals;summary.bins.begin()->second=bin;
        const auto score=scoreJointInterval(summary,0,0);
        if(!score.valid) {++stats.rangeRejected;return;}
        // Never remove the target, even when the chosen interval has no hits.
        pool.offer(score.metrics,[&]{return replaced(current,target.nodeId,lo,hi);});
    };
    if(const auto idle=unobserved(seen,target.monitorInput)) evaluate(*idle,*idle,Bin{});
    intervals(values.size(),o.boundaryBudget,[&](std::size_t lo,std::size_t hi) {
        evaluate(values[lo],values[hi],sumBin(prefix[hi+1],prefix[lo],-1));
    });
    stats.intervalSeconds+=seconds(intervalBegin);return pool.states();
}
std::vector<Proposal> proposals(const std::vector<core::AddSample>& trace,const FixedBitflipNode& target,
    const CircuitFixedBitflipOptions& o,CircuitFixedBitflipStats& stats) {
    const auto begin=Clock::now();std::map<int,Local> bins;std::set<int> seen;
    const auto d=domain(target.monitorInput);
    for(const auto& sample:trace) if(sample.nodeId==target.nodeId) {
        const int v=value(sample,target.monitorInput);seen.insert(v);
        if(v<d.first || v>d.second) continue;
        // This proxy orders proposals only. Every retained state is scored by
        // the full configured DCT and IDCT image reconstruction below.
        if(sample.baselineOutput < -4096 || sample.baselineOutput>4095) continue;
        const int flipped=core::flipNonSignOutputBit(sample.baselineOutput,target.bitIndex);
        const double oldError=double(sample.output)-sample.input1-sample.input2;
        const double newError=double(flipped)-sample.input1-sample.input2;
        const double delta=newError*newError-oldError*oldError;
        const double shift=double(flipped)-sample.output,w=sample.roiWeight;
        bins[v]+={delta*w,delta*(1-w),shift*w,shift*(1-w),w,1};
    }
    std::vector<int> values;std::vector<Local> prefix(1);
    for(const auto& b:bins) {values.push_back(b.first);auto x=prefix.back();x+=b.second;prefix.push_back(x);}
    std::array<std::vector<Proposal>,6> lists;
    intervals(values.size(),o.boundaryBudget,[&](std::size_t lo,std::size_t hi) {
        ++stats.evaluatedIntervals;
        Proposal p{attack(target.nodeId,target.bitIndex,target.monitorInput,values[lo],values[hi]),subtract(prefix[hi+1],prefix[lo])};
        for(int role=0;role<6;++role) {
            auto& list=lists[role];
            list.push_back(p);std::stable_sort(list.begin(),list.end(),[&](const auto& a,const auto& b){return proposalRank(a,role)>proposalRank(b,role);});
            if(list.size()>o.fullCandidatesPerContext) list.resize(o.fullCandidatesPerContext);
        }
    });
    std::vector<Proposal> out;
    const auto offer=[&](const Proposal& p) {
        if(std::none_of(out.begin(),out.end(),[&](const auto& old){return sameProposal(old,p);})) out.push_back(p);
    };
    // A no-hit interval is an ordinary full-H candidate, including for a
    // currently active target. A cached dormant state consumes no new score.
    if(const auto idle=unobserved(seen,target.monitorInput)) offer({attack(target.nodeId,target.bitIndex,target.monitorInput,*idle,*idle),{}});
    for(std::size_t depth=0;depth<o.fullCandidatesPerContext;++depth) for(int role=0;role<6;++role)
        if(depth<lists[role].size()) offer(lists[role][depth]);
    stats.intervalSeconds+=seconds(begin);return out;
}
bool better(const State& a,const State& b) {
    if(a.metrics.roiMse!=b.metrics.roiMse) return a.metrics.roiMse>b.metrics.roiMse;
    if(a.metrics.globalMse!=b.metrics.globalMse) return a.metrics.globalMse<b.metrics.globalMse;
    return key(a.configuration)<key(b.configuration);
}
void verify(const BitflipQuality& score,const BitflipQuality& oracle) {
    for(auto p:{std::pair<double,double>{score.globalMse,oracle.globalMse},{score.roiMse,oracle.roiMse},{score.nonRoiMse,oracle.nonRoiMse}})
        if(std::abs(p.first-p.second)>1e-8*std::max(1.0,std::abs(p.second)))
            throw std::runtime_error("Fixed circuit interval score disagrees with original full application.");
}
}

bool matchesCircuitFixedBitflipHardware(const JointBitflipConfiguration& c,const FixedBitflipHardware& h,std::size_t nodes) {
    if(!fixed_circuit::validHardware(h,nodes) || c.size()!=h.size()) return false;
    std::set<int> seen;
    for(const auto& x:c) {
        if(x.payload!=core::AttackPayload::BitFlip || !seen.insert(x.nodeId).second) return false;
        const auto it=std::find_if(h.begin(),h.end(),[&](const auto& n){return n.nodeId==x.nodeId;});
        if(it==h.end() || x.monitorInput!=it->monitorInput || x.bitIndex!=it->bitIndex) return false;
    }
    return true;
}

std::optional<JointBitflipConfiguration> dormantCircuitFixedBitflipSeed(const core::Application& app,
    const cv::Mat& image,const cv::Mat& mask,const FixedBitflipHardware& hardware) {
    kindOf(app);fixed_circuit::validateHardware(hardware,app.addNodes().size());
    const auto binary=binaryMask(image,mask);const auto baseline=app.runApprox(image,{});
    std::vector<core::AddSample> trace;app.collectBaselineAddSamples(image,binary,trace);
    return fixed_circuit::dormant(app,image,binary,hardware,baseline,trace);
}

CircuitFixedBitflipResult searchCircuitFixedBitflipIntervals(const core::Application& app,
    const cv::Mat& image,const cv::Mat& mask,const FixedBitflipHardware& hardware,
    const CircuitFixedBitflipOptions& options,const std::function<void(const CircuitFixedBitflipProgress&)>& progress) {
    using namespace fixed_circuit;
    const auto kind=kindOf(app);const auto nodes=app.addNodes().size();validateHardware(hardware,nodes);
    if(!options.beamWidth || !options.representativeCount || !options.restarts || !options.maxContexts ||
            !options.maxFullEvaluations || !options.fullCandidatesPerContext || !std::isfinite(options.globalPsnrThreshold))
        throw std::invalid_argument("Invalid fixed circuit interval search options.");
    if(options.continuous && (!std::isfinite(options.convergenceTolerance) || options.convergenceTolerance<0 ||
            options.refinementRounds==std::numeric_limits<std::size_t>::max() ||
            options.restarts>std::numeric_limits<std::size_t>::max()/(options.refinementRounds+1)))
        throw std::invalid_argument("Invalid continuous fixed circuit convergence options.");
    for(const auto& c:options.seeds) validateSeed(c,hardware,nodes);
    const auto binary=binaryMask(image,mask);
    if(kind!=Kind::Dct && (image.rows<3 || image.cols<3))
        throw std::invalid_argument("Fixed pixel circuit search requires an interior pixel.");
    CircuitFixedBitflipResult result;
    result.stats.contextsByNode.resize(nodes);auto& stats=result.stats;
    stats.continuous=options.continuous;
    const auto referenceBegin=Clock::now();const auto reference=app.runExact(image);stats.referenceSeconds=seconds(referenceBegin);
    const auto initBegin=Clock::now();const auto baseline=app.runApprox(image,{});
    result.baseline=measureBitflipQuality(reference,baseline,binary);
    std::vector<State> seeds;std::set<std::string> seedKeys;
    std::map<std::string,std::optional<State>> fullCache;
    const auto accept=[&](const Config& config,bool warm) {
        if(seedKeys.count(key(config))) return;
        cv::Mat output;
        try {output=app.runApprox(image,config);} catch(const std::runtime_error& e) {
            if(!warm || !rangeError(e)) throw;
            ++stats.rejectedSeeds;++stats.rangeRejected;return;
        }
        State s{config,measureBitflipQuality(reference,output,binary)};
        seedKeys.insert(key(config));seeds.push_back(s);fullCache.emplace(key(config),s);++stats.acceptedSeeds;
    };
    for(const auto& config:options.seeds) accept(config,true);
    {
        std::vector<core::AddSample> baselineTrace;app.collectBaselineAddSamples(image,binary,baselineTrace);
        if(const auto idle=dormant(app,image,binary,hardware,baseline,baselineTrace)) accept(*idle,false);
    }
    stats.initializationSeconds=seconds(initBegin);
    if(seeds.empty()) return result;
    // One full trace at a time: a 512px DCT trace is large. Immutable seed-first
    // traversal reuses this trace across all installed targets; compact scored
    // contexts remain cached across later seed/restart visits.
    std::optional<std::string> traceKey;std::vector<core::AddSample> cachedTrace;
    const auto traceFor=[&](const Config& config)->const std::vector<core::AddSample>& {
        const auto id=key(config);
        if(traceKey && *traceKey==id) {++stats.traceCacheHits;return cachedTrace;}
        const auto begin=Clock::now();traceKey.reset();cachedTrace.clear();
        app.collectConfiguredAddSamples(image,binary,config,cachedTrace);
        const auto expected=kind==Kind::Dct?
            std::size_t((image.rows+7)/8)*std::size_t((image.cols+7)/8)*16*nodes:
            std::size_t(image.rows-2)*std::size_t(image.cols-2)*nodes;
        if(cachedTrace.size()!=expected) throw std::runtime_error("Unexpected fixed circuit configured trace size.");
        for(std::size_t i=0;i<cachedTrace.size();++i)
            if(cachedTrace[i].nodeId!=int(i%nodes) || !std::isfinite(cachedTrace[i].roiWeight) || cachedTrace[i].roiWeight<0 || cachedTrace[i].roiWeight>1)
                throw std::runtime_error("Unexpected fixed circuit configured trace order or ROI weight.");
        stats.traceSeconds+=seconds(begin);++stats.traceBuilds;
        traceKey=id;return cachedTrace;
    };
    Representatives legal(options.beamWidth,options.globalPsnrThreshold),numeric(options.representativeCount,options.globalPsnrThreshold);
    const auto remember=[&](const std::vector<State>& states) {
        for(const auto& s:states) {
            validateSeed(s.configuration,hardware,nodes);legal.offer(s.metrics,[&]{return s.configuration;});
            if(feasible(s.metrics,options.globalPsnrThreshold) && s.metrics.roiMse>result.baseline.roiMse+1e-10)
                numeric.offer(s.metrics,[&]{return s.configuration;});
            if(!feasible(s.metrics,options.globalPsnrThreshold)) ++stats.retainedQualityFailures;
        }
    };
    remember(seeds);auto beam=retain(seeds,options.beamWidth,options.globalPsnrThreshold,hardware.size());
    auto order=hardware;std::sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});
    const auto coverageShare=[&](std::size_t total) {return std::min(total,std::max(hardware.size(),total/2));};
    const auto coverageContexts=coverageShare(options.maxContexts),coverageFull=coverageShare(options.maxFullEvaluations);
    std::size_t contextCeiling=coverageContexts,fullCeiling=coverageFull;
    std::map<std::string,std::vector<State>> contextCache;
    const auto visit=[&](const State& state,const FixedBitflipNode& target,std::size_t restart,std::size_t pass,
            std::size_t done,std::size_t total,std::vector<State>& expanded) {
        std::size_t quota=options.fullCandidatesPerContext;
        if(kind==Kind::Dct) {
            std::size_t uncovered=0;for(const auto& n:hardware) uncovered+=stats.contextsByNode[n.nodeId]==0;
            const auto remaining=fullCeiling-stats.fullEvaluations;
            // Reserve at least one complete image score for each untouched H
            // target before spending additional evaluations on another beam.
            if(uncovered) quota=std::min(quota,std::max<std::size_t>(1,remaining/uncovered));
            // A phase-limited expansion must have a different cache identity
            // when a later restart can afford a larger complete shortlist.
            quota=std::min<std::size_t>(quota,remaining);
        }
        const auto id=std::to_string(target.nodeId)+"|"+
            key(kind==Kind::Dct?state.configuration:without(state.configuration,target.nodeId))+"|"+std::to_string(quota);
        auto found=contextCache.find(id);
        if(found==contextCache.end()) {
            if(stats.contextsBuilt>=contextCeiling) {
                if(stats.contextsBuilt>=options.maxContexts) stats.contextBudgetExhausted=true;
                return false;
            }
            if(kind==Kind::Dct && stats.fullEvaluations>=fullCeiling) {
                if(stats.fullEvaluations>=options.maxFullEvaluations) stats.fullBudgetExhausted=true;
                return false;
            }
            ++stats.contextsBuilt;++stats.contextsByNode[target.nodeId];
            const auto& trace=traceFor(state.configuration);std::vector<State> states;
            if(kind!=Kind::Dct) states=expandPixel(app,image,binary,reference,state.configuration,target,trace,options,stats);
            else {
                Representatives pool(options.representativeCount,options.globalPsnrThreshold);
                const auto proposed=proposals(trace,target,options,stats);const auto before=stats.fullEvaluations;
                for(const auto& p:proposed) {
                    auto config=replaced(state.configuration,target.nodeId,p.config.lower,p.config.upper);
                    auto scored=fullCache.find(key(config));
                    if(scored==fullCache.end()) {
                        if(stats.fullEvaluations-before>=quota) break;
                        if(stats.fullEvaluations>=fullCeiling) {
                            if(stats.fullEvaluations>=options.maxFullEvaluations) stats.fullBudgetExhausted=true;
                            break;
                        }
                        const auto begin=Clock::now();++stats.fullEvaluations;std::optional<State> s;
                        try {s=State{config,measureBitflipQuality(reference,app.runApprox(image,config),binary)};}
                        catch(const std::runtime_error& e) {if(!rangeError(e)) throw;++stats.rangeRejected;}
                        stats.fullEvaluationSeconds+=seconds(begin);scored=fullCache.emplace(key(config),std::move(s)).first;
                    }
                    if(scored->second) pool.offer(scored->second->metrics,[&]{return config;});
                }
                states=pool.states();
            }
            found=contextCache.emplace(id,std::move(states)).first;
        } else ++stats.cacheHits;
        remember(found->second);expanded.insert(expanded.end(),found->second.begin(),found->second.end());
        if(progress) progress({restart,pass,std::size_t(target.nodeId),done,total,stats});
        return true;
    };
    // Immutable seed round robin gives every installed target first coverage.
    // Retained multi-beam refinement and alternate orders follow this pass.
    // The first phase gets half the common budgets, or enough for one visit
    // per H target when possible. Unused shares carry forward. A phase ceiling
    // is scheduling, while exhaustion flags describe the actual user limits.
    if(options.continuous) {
        // A single evolving beam, reference, trace/context/full-score caches,
        // and best archives survive every sweep. Sweep zero retains the
        // immutable seed-first traversal for installed-node coverage.
        contextCeiling=options.maxContexts;fullCeiling=options.maxFullEvaluations;
        const auto sweeps=std::max<std::size_t>(2,options.restarts*(options.refinementRounds+1));
        std::optional<double> previousBest;
        for(std::size_t sweep=0;sweep<sweeps;++sweep) {
            bool complete=true;
            if(!sweep) {
                for(std::size_t i=0;i<seeds.size() && complete;++i) {
                    auto traversal=order;if(i%2) std::reverse(traversal.begin(),traversal.end());
                    for(const auto& target:traversal) {
                        auto expanded=beam;
                        if(!visit(seeds[i],target,0,0,i+1,seeds.size(),expanded)) {complete=false;break;}
                        beam=retain(expanded,options.beamWidth,options.globalPsnrThreshold,hardware.size());
                    }
                }
            } else {
                auto traversal=order;if(sweep%2) std::reverse(traversal.begin(),traversal.end());
                for(const auto& target:traversal) {
                    auto expanded=beam;std::size_t done=0;
                    for(const auto& s:beam) if(!visit(s,target,sweep,1,++done,beam.size(),expanded)) {complete=false;break;}
                    beam=retain(expanded,options.beamWidth,options.globalPsnrThreshold,hardware.size());
                    if(!complete) break;
                }
            }
            // An interrupted traversal is never numeric convergence evidence.
            if(!complete) break;
            ++stats.completedSweeps;
            std::optional<double> currentBest;
            for(const auto& s:numeric.states())
                if(!currentBest || s.metrics.roiMse>*currentBest) currentBest=s.metrics.roiMse;
            if(stats.completedSweeps>=2 && previousBest && currentBest &&
                    *currentBest-*previousBest<=options.convergenceTolerance*std::max(1.0,std::abs(*previousBest))) {
                stats.stoppedForConvergence=true;break;
            }
            previousBest=currentBest;
        }
    } else {
    bool phaseLimited=false;
    for(std::size_t i=0;i<seeds.size() && !phaseLimited;++i) {
        auto traversal=order;if(i%2) std::reverse(traversal.begin(),traversal.end());
        for(const auto& target:traversal) {
            auto expanded=beam;
            if(!visit(seeds[i],target,0,0,i+1,seeds.size(),expanded)) {phaseLimited=true;break;}
            beam=retain(expanded,options.beamWidth,options.globalPsnrThreshold,hardware.size());
        }
    }
    for(std::size_t restart=0;restart<options.restarts;++restart) {
        const auto share=[&](std::size_t total,std::size_t first) {
            const auto remaining=total-first,index=restart+1;
            return first+(remaining/options.restarts)*index+std::min(remaining%options.restarts,index);
        };
        contextCeiling=share(options.maxContexts,coverageContexts);
        fullCeiling=share(options.maxFullEvaluations,coverageFull);phaseLimited=false;
        if(restart) beam=retain(seeds,options.beamWidth,options.globalPsnrThreshold,hardware.size());
        for(std::size_t pass=0;pass<=options.refinementRounds && !phaseLimited;++pass) {
            auto traversal=order;if((restart+pass)%2) std::reverse(traversal.begin(),traversal.end());
            for(const auto& target:traversal) {
                auto expanded=beam;std::size_t done=0;
                for(const auto& s:beam) if(!visit(s,target,restart,pass+1,++done,beam.size(),expanded)) {phaseLimited=true;break;}
                beam=retain(expanded,options.beamWidth,options.globalPsnrThreshold,hardware.size());if(phaseLimited) break;
            }
        }
    }
    }
    const auto replayBegin=Clock::now();std::map<std::string,State> finalists;
    for(const auto& s:legal.states()) finalists.emplace(key(s.configuration),s);
    for(const auto& s:numeric.states()) finalists.emplace(key(s.configuration),s);
    for(auto& entry:finalists) {
        auto& s=entry.second;validateSeed(s.configuration,hardware,nodes);++stats.finalReplayEvaluations;
        const auto actual=measureBitflipQuality(reference,app.runApprox(image,s.configuration),binary);
        verify(s.metrics,actual);s.metrics=actual;
        if(!result.bestLegal || better(s,*result.bestLegal)) result.bestLegal=s;
        if(feasible(actual,options.globalPsnrThreshold) && actual.roiMse>result.baseline.roiMse+1e-10) result.candidates.push_back(s);
    }
    std::stable_sort(result.candidates.begin(),result.candidates.end(),better);stats.finalReplaySeconds=seconds(replayBegin);
    return result;
}
}

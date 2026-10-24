#include "analysis/sharpen_bitflip_joint.hpp"
#include "core/output_bitflip.hpp"
#include "region/region_mask.hpp"
#include <algorithm>
#include <array>
#include <cmath>
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
constexpr std::array<core::MonitorSignal,3> signals{
    core::MonitorSignal::Input1,core::MonitorSignal::Input2,core::MonitorSignal::BaselineOutput};
Sum square(int x) {return static_cast<Sum>(x)*x;}
int monitored(core::MonitorSignal signal,int input,int rhs,int raw) {
    if(signal==core::MonitorSignal::Input1) return input;
    if(signal==core::MonitorSignal::Input2) return rhs;
    if(signal==core::MonitorSignal::BaselineOutput) return raw;
    throw std::runtime_error("Unknown joint monitor signal.");
}
Config without(const Config& current,int node) {
    Config out;for(const auto& c:current) if(c.nodeId!=node) out.push_back(c);return out;
}
std::string key(Config config) {
    std::sort(config.begin(),config.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});
    std::ostringstream out;
    for(const auto& c:config) out<<c.nodeId<<':'<<c.bitIndex<<':'<<int(c.monitorInput)<<':'<<c.lower<<':'<<c.upper<<';';
    return out.str();
}
Bin plus(Bin a,const Bin& b) {
    a.roiDelta+=b.roiDelta;a.nonRoiDelta+=b.nonRoiDelta;a.invalidDelta+=b.invalidDelta;
    a.roiHits+=b.roiHits;a.nonRoiHits+=b.nonRoiHits;return a;
}
Bin minus(Bin a,const Bin& b) {
    a.roiDelta-=b.roiDelta;a.nonRoiDelta-=b.nonRoiDelta;a.invalidDelta-=b.invalidDelta;
    a.roiHits-=b.roiHits;a.nonRoiHits-=b.nonRoiHits;return a;
}
struct Pixel {
    int input=0,raw=0,ref=0;
    std::array<int,4> rhs{};
    bool roi=false,offValid=false;
    Sum offSse=0;
};
struct Context {
    int node=0;
    Config others;
    applications::SharpenApplication::BaselineConfig units;
    std::array<std::optional<core::AttackConfig>,4> active;
    std::vector<Pixel> pixels;
    std::uint64_t nr=0,nn=0;
    Sum er=0,en=0,invalid=0;
};
bool suffix(const Context& context,const Pixel& p,int value,int& output) {
    for(int n=context.node+1;n<4;++n) {
        const auto& unit=context.units[n];const int rhs=p.rhs[n];
        if(!unit.exact && (value < -2048 || value > 2047 || rhs < -2048 || rhs > 2047)) return false;
        const int raw=unit.exact?value+rhs:approximate::addSigned12(value,rhs,unit.unit);
        int result=raw;
        if(context.active[n]) {
            const auto& attack=*context.active[n];
            const int v=monitored(attack.monitorInput,value,rhs,raw);
            if(v>=attack.lower && v<=attack.upper) {
                if(raw < -4096 || raw > 4095) return false;
                result=core::flipNonSignOutputBit(raw,attack.bitIndex);
            }
        }
        value=result;
    }
    output=std::clamp(value,0,255);return true;
}
Context contextFor(const applications::SharpenApplication& app,const cv::Mat& image,
    const cv::Mat& mask,const Config& current,int node) {
    if(node<0 || node>=4 || image.rows<3 || image.cols<3)
        throw std::runtime_error("Joint profiling needs a valid node and image >=3x3.");
    for(const auto& c:current) {
        if(c.payload!=core::AttackPayload::BitFlip) throw std::runtime_error("Joint profiler accepts only bit flips.");
        monitored(c.monitorInput,0,0,0);
    }
    // This original execution validates configuration, prefix legality and mask.
    std::vector<core::AddSample> samples;
    app.collectConfiguredAddSamples(image,mask,current,samples);
    const auto reference=app.runExact(image);
    Context context;context.node=node;context.others=without(current,node);context.units=app.baselineConfig();
    for(const auto& c:context.others) context.active[c.nodeId]=c;
    for(int r=0;r<image.rows;++r) for(int c=0;c<image.cols;++c)
        region_mask::isImportantPixel(mask,r,c)?++context.nr:++context.nn;
    if(!context.nr || !context.nn) throw std::runtime_error("Both ROI and background pixels are required.");
    const auto count=static_cast<std::size_t>(image.rows-2)*(image.cols-2);
    if(samples.size()!=count*4) throw std::runtime_error("Unexpected Sharpen trace size.");
    context.pixels.reserve(count);std::size_t i=0;
    for(int r=1;r<image.rows-1;++r) for(int c=1;c<image.cols-1;++c,++i) {
        Pixel p;const auto& sample=samples.at(i*4+node);
        p.input=sample.input1;p.raw=sample.baselineOutput;p.ref=reference.at<unsigned char>(r,c);
        p.roi=region_mask::isImportantPixel(mask,r,c);
        for(int n=0;n<4;++n) p.rhs[n]=samples.at(i*4+n).input2;
        int final=0;p.offValid=suffix(context,p,p.raw,final);
        if(p.offValid) {
            p.offSse=square(final-p.ref);(p.roi?context.er:context.en)+=p.offSse;
        } else ++context.invalid;
        context.pixels.push_back(p);
    }
    // Original Sharpen borders copy input in both exact and approximate paths.
    return context;
}
std::array<JointBitflipProfile,3> profilesFor(const Context& context,int bit) {
    core::flipNonSignOutputBit(0,bit);
    std::array<JointBitflipProfile,3> profiles;
    for(int m=0;m<3;++m) {
        auto& p=profiles[m];p.others=context.others;p.node=context.node;p.bit=bit;p.monitor=signals[m];
        p.roiPixels=context.nr;p.nonRoiPixels=context.nn;
        p.offRoiSse=context.er;p.offNonRoiSse=context.en;p.offInvalid=context.invalid;
    }
    for(const auto& p:context.pixels) {
        int final=0;bool valid=false;
        if(p.raw>=-4096 && p.raw<=4095)
            valid=suffix(context,p,core::flipNonSignOutputBit(p.raw,bit),final);
        const Sum delta=(valid?square(final-p.ref):0)-p.offSse;
        for(int m=0;m<3;++m) {
            auto& bin=profiles[m].bins[monitored(signals[m],p.input,p.rhs[context.node],p.raw)];
            if(p.roi) {bin.roiDelta+=delta;++bin.roiHits;}
            else {bin.nonRoiDelta+=delta;++bin.nonRoiHits;}
            bin.invalidDelta+=Sum(!valid)-Sum(!p.offValid);
        }
    }
    return profiles;
}
JointIntervalScore finish(const JointBitflipProfile& p,const Bin& delta) {
    JointIntervalScore score;score.invalidPixels=p.offInvalid+delta.invalidDelta;
    if(score.invalidPixels<0) throw std::runtime_error("Negative invalid pixel count in conditional profile.");
    score.roiTriggers=delta.roiHits;score.nonRoiTriggers=delta.nonRoiHits;
    if(score.invalidPixels) return score;
    const Sum er=p.offRoiSse+delta.roiDelta,en=p.offNonRoiSse+delta.nonRoiDelta;
    if(er<0 || en<0 || !p.roiPixels || !p.nonRoiPixels)
        throw std::runtime_error("Invalid conditional SSE or region denominator.");
    score.valid=true;score.metrics.roiMse=double(er)/p.roiPixels;
    score.metrics.nonRoiMse=double(en)/p.nonRoiPixels;
    score.metrics.globalMse=double(er+en)/(p.roiPixels+p.nonRoiPixels);return score;
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
class Representatives {
    std::array<std::vector<State>,6> slots;
    std::size_t capacity;double threshold;
public:
    Representatives(std::size_t n,double t):capacity(n),threshold(t) {}
    template<class Maker> void offer(const BitflipQuality& q,const Maker& make) {
        std::optional<State> state;
        for(int role=0;role<6;++role) {
            if(role==0 && !feasible(q,threshold)) continue;
            auto& list=slots[role];const auto score=rankFor(q,role);
            if(list.size()>=capacity && score<=rankFor(list.back().metrics,role)) continue;
            if(!state) state=State{make(),q};
            const auto identity=key(state->configuration);
            if(std::any_of(list.begin(),list.end(),[&](const auto& x){return key(x.configuration)==identity;})) continue;
            list.push_back(*state);
            std::stable_sort(list.begin(),list.end(),[&](const auto& a,const auto& b){return rankFor(a.metrics,role)>rankFor(b.metrics,role);});
            if(list.size()>capacity) list.resize(capacity);
        }
    }
    std::vector<State> states() const {
        std::map<std::string,State> unique;
        for(const auto& list:slots) for(const auto& state:list) unique.emplace(key(state.configuration),state);
        std::vector<State> out;for(const auto& x:unique) out.push_back(x.second);return out;
    }
};
std::vector<State> retain(const std::vector<State>& input,std::size_t width,double threshold) {
    std::array<std::map<std::string,State>,5> buckets;
    for(const auto& state:input) buckets.at(state.configuration.size()).emplace(key(state.configuration),state);
    std::vector<State> out;
    for(auto& bucket:buckets) {
        std::vector<State> states;for(auto& x:bucket) states.push_back(std::move(x.second));
        std::array<std::vector<std::size_t>,6> orders;
        for(int role=0;role<6;++role) {
            for(std::size_t i=0;i<states.size();++i)
                if(role!=0 || feasible(states[i].metrics,threshold)) orders[role].push_back(i);
            std::stable_sort(orders[role].begin(),orders[role].end(),[&](auto a,auto b){return rankFor(states[a].metrics,role)>rankFor(states[b].metrics,role);});
        }
        std::set<std::size_t> selected;
        for(std::size_t depth=0;selected.size()<width && depth<states.size();++depth)
            for(int role:{0,2,1,3,4,5}) {
                if(depth<orders[role].size() && selected.size()<width) selected.insert(orders[role][depth]);
            }
        for(auto i:selected) out.push_back(std::move(states[i]));
    }
    return out;
}
std::vector<State> expand(const applications::SharpenApplication& app,const cv::Mat& image,
    const cv::Mat& mask,const Config& current,int node,const JointBitflipOptions& options,JointBitflipStats& stats) {
    const auto context=contextFor(app,image,mask,current,node);++stats.contextsBuilt;
    Representatives pool(options.representativeCount,options.globalPsnrThreshold);
    if(!context.invalid) {
        BitflipQuality q;q.roiMse=double(context.er)/context.nr;q.nonRoiMse=double(context.en)/context.nn;
        q.globalMse=double(context.er+context.en)/(context.nr+context.nn);
        pool.offer(q,[&]{return context.others;});
    }
    for(int bit:options.bits) for(const auto& profile:profilesFor(context,bit)) {
        std::vector<int> values;std::vector<Bin> prefix(1);
        for(const auto& x:profile.bins) {values.push_back(x.first);prefix.push_back(plus(prefix.back(),x.second));}
        const auto evaluate=[&](std::size_t lo,std::size_t hi) {
            ++stats.evaluatedIntervals;
            const auto q=finish(profile,minus(prefix[hi+1],prefix[lo]));
            if(!q.valid) {++stats.rangeRejected;return;}
            if(!q.roiTriggers && !q.nonRoiTriggers) return;
            pool.offer(q.metrics,[&] {
                Config config=profile.others;
                config.push_back({node,app.baselineConfig()[node].unit,profile.monitor,values[lo],values[hi],core::AttackPayload::BitFlip,bit});
                return config;
            });
        };
        if(options.boundaryBudget==0) {
            for(std::size_t lo=0;lo<values.size();++lo) for(std::size_t hi=lo;hi<values.size();++hi) evaluate(lo,hi);
        } else {
            std::set<std::pair<std::size_t,std::size_t>> intervals;
            for(std::size_t lo=0;lo<values.size();++lo) for(std::size_t width:{1,2,3,4,8,16})
                if(lo+width<=values.size()) intervals.emplace(lo,lo+width-1);
            const auto count=std::min(options.boundaryBudget,values.size());
            std::vector<std::size_t> ends;
            for(std::size_t i=0;i<count;++i) ends.push_back(count==1?0:i*(values.size()-1)/(count-1));
            for(auto lo:ends) for(auto hi:ends) if(hi>=lo) intervals.emplace(lo,hi);
            for(const auto& interval:intervals) evaluate(interval.first,interval.second);
        }
    }
    return pool.states();
}
}

JointBitflipProfile profileJointBitflip(const applications::SharpenApplication& app,const cv::Mat& image,
    const cv::Mat& mask,const Config& current,int node,int bit,core::MonitorSignal monitor) {
    monitored(monitor,0,0,0);
    auto profiles=profilesFor(contextFor(app,image,mask,current,node),bit);
    for(auto& p:profiles) if(p.monitor==monitor) return std::move(p);
    throw std::runtime_error("Unknown monitor signal.");
}
JointIntervalScore scoreJointInterval(const JointBitflipProfile& profile,int lower,int upper) {
    if(lower>upper) throw std::runtime_error("Interval lower bound exceeds upper bound.");
    Bin delta;
    for(auto it=profile.bins.lower_bound(lower);it!=profile.bins.end() && it->first<=upper;++it) delta=plus(delta,it->second);
    return finish(profile,delta);
}

JointBitflipResult searchJointBitflips(const applications::SharpenApplication& app,const cv::Mat& image,
    const cv::Mat& mask,const JointBitflipOptions& options,const std::function<void(const JointBitflipProgress&)>& progress) {
    if(!options.beamWidth || !options.representativeCount || !options.candidatesPerCount || !options.restarts ||
            options.boundaryBudget==1 || options.bits.empty() || !std::isfinite(options.globalPsnrThreshold))
        throw std::runtime_error("Invalid joint search options.");
    std::set<int> bits;
    for(int bit:options.bits) {
        core::flipNonSignOutputBit(0,bit);
        if(!bits.insert(bit).second) throw std::runtime_error("Duplicate joint candidate bit.");
    }
    JointBitflipResult result;
    result.baseline=measureBitflipQuality(app.runExact(image),app.runApprox(image,{}),mask);
    const State initial{{},result.baseline};
    std::map<std::string,std::vector<State>> cache;
    std::vector<State> archive;
    for(std::size_t restart=0;restart<options.restarts;++restart) {
        std::vector<State> beam{initial};
        for(std::size_t pass=0;pass<=options.refinementRounds;++pass) {
            std::array<int,4> order{0,1,2,3};
            if((restart+pass)%2) std::reverse(order.begin(),order.end());
            for(int node:order) {
                std::vector<State> expanded=beam;std::size_t done=0;
                for(const auto& state:beam) {
                    const auto identity=std::to_string(node)+"|"+key(without(state.configuration,node));
                    auto found=cache.find(identity);
                    if(found==cache.end()) found=cache.emplace(identity,expand(app,image,mask,state.configuration,node,options,result.stats)).first;
                    else ++result.stats.cacheHits;
                    expanded.insert(expanded.end(),found->second.begin(),found->second.end());
                    if(progress) progress({restart,pass,static_cast<std::size_t>(node),++done,beam.size(),result.stats});
                }
                beam=retain(expanded,options.beamWidth,options.globalPsnrThreshold);
                for(const auto& state:beam) {
                    if(!feasible(state.metrics,options.globalPsnrThreshold)) ++result.stats.retainedQualityFailures;
                    if(state.configuration.size()>=2 && feasible(state.metrics,options.globalPsnrThreshold) &&
                            state.metrics.roiMse>result.baseline.roiMse+1e-10) archive.push_back(state);
                }
                archive=retain(archive,options.candidatesPerCount,options.globalPsnrThreshold);
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

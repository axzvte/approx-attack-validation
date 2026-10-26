#include "analysis/circuit_bitflip_ten_train.hpp"
#include "analysis/detail/ten_train_schedule.hpp"
#include "analysis/bitflip_sensitivity.hpp"
#include "applications/conv.hpp"
#include "applications/sobel.hpp"
#include "applications/dct.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace analysis {
namespace {
using Clock=std::chrono::steady_clock;
using Signal=core::MonitorSignal;
constexpr std::array<Signal,3> monitors{Signal::Input1,Signal::Input2,Signal::BaselineOutput};
double seconds(Clock::time_point start) {return std::chrono::duration<double>(Clock::now()-start).count();}
std::string monitorName(Signal s) {
    if(s==Signal::BaselineOutput) return "baseline_output";
    if(s==Signal::Input1) return "input1";
    if(s==Signal::Input2) return "input2";
    throw std::invalid_argument("Unknown training monitor signal.");
}
using HardwareKey=std::vector<std::tuple<int,std::string,int>>;
HardwareKey hardwareKey(const FixedBitflipHardware& h) {
    HardwareKey out;for(const auto& n:h) out.emplace_back(n.nodeId,monitorName(n.monitorInput),n.bitIndex);
    std::sort(out.begin(),out.end());return out;
}
approximate::ApproxUnitId baselineUnit(const core::Application& app,int node) {
    if(const auto* p=dynamic_cast<const applications::ConvApplication*>(&app)) return p->baselineConfig()[node].unit;
    if(const auto* p=dynamic_cast<const applications::SobelApplication*>(&app)) return p->baselineConfig()[node].unit;
    if(const auto* p=dynamic_cast<const applications::DctApplication*>(&app)) return p->baselineConfig()[node].unit;
    throw std::invalid_argument("Ten-image training supports Conv, Sobel and DCT only.");
}
double cappedPsnr(double mse) {return mse<=0?100:std::min(100.0,10*std::log10(255.0*255.0/mse));}
double score(const TenTrainEvaluation& e) {
    return e.numericEligible?std::max(0.0,cappedPsnr(e.baseline.roiMse)-cappedPsnr(e.selected.metrics.roiMse)):0;
}
void summarize(TenTrainCandidate& c,std::size_t totalImages) {
    std::sort(c.evaluations.begin(),c.evaluations.end(),[](const auto& a,const auto& b){return a.imageId<b.imageId;});
    c.numericSuccesses=0;c.worstRoiDrop=0;c.meanRoiDrop=0;
    if(c.evaluations.empty()) return;
    c.worstRoiDrop=100;
    // Sort retained evaluations before summing, independently of scheduling,
    // future completion order and caller image order.
    for(const auto& e:c.evaluations) {
        c.numericSuccesses+=e.numericEligible;const auto s=score(e);
        c.worstRoiDrop=std::min(c.worstRoiDrop,s);c.meanRoiDrop+=s;
    }
    // For partial rows this is a conservative partial-evidence mean over the
    // declared training set; the report retains both actual and total counts.
    c.meanRoiDrop/=totalImages;
}
struct Priority {
    std::vector<int> nodes;
    std::vector<double> nodeScores;
    std::vector<std::array<double,3>> signalScores;
};
// Consumes one image trace at a time. Each image contributes one normalized
// node/signal summary, so large images and DCT's repeated calls do not dominate.
void accumulate(Priority& priority,const std::vector<core::AddSample>& trace,std::size_t imageCount) {
    const std::size_t nodes=priority.nodes.size();
    const auto p=prioritizeBitflipNodes(trace,nodes,{0,1,2,3,4,5,6,7,8,9,10,11});
    using Bins=std::array<std::array<std::array<double,32>,3>,2>;
    std::vector<Bins> hist(nodes);std::vector<std::array<double,2>> weights(nodes);
    for(const auto& s:trace) {
        const auto n=std::size_t(s.nodeId);const std::array<int,3> values{s.input1,s.input2,s.baselineOutput};
        for(std::size_t region=0;region<2;++region) {
            const double w=region?1-s.roiWeight:s.roiWeight;weights[n][region]+=w;
            for(std::size_t m=0;m<3;++m) {
                const int half=m==2?4096:2048;
                const auto b=(std::clamp(values[m],-half,half-1)+half)*32/(half*2);
                hist[n][region][m][std::size_t(b)]+=w;
            }
        }
    }
    for(std::size_t n=0;n<nodes;++n) {
        priority.nodeScores[n]+=(p.separability[n]+.25*p.disturbance[n])/imageCount;
        if(weights[n][0]<=0 || weights[n][1]<=0) continue;
        for(std::size_t m=0;m<3;++m) {
            double distance=0;for(std::size_t b=0;b<32;++b)
                distance+=std::abs(hist[n][0][m][b]/weights[n][0]-hist[n][1][m][b]/weights[n][1]);
            priority.signalScores[n][m]+=distance/(2*imageCount);
        }
    }
}
Signal preferredSignal(const Priority& p,int node) {
    const auto& scores=p.signalScores[std::size_t(node)];
    return monitors[std::size_t(std::max_element(scores.begin(),scores.end())-scores.begin())];
}
FixedBitflipHardware globalProposal(const Priority& p,std::size_t serial) {
    const auto nodes=p.nodes.size();std::size_t count;
    if(serial==0) count=2;
    else if(serial==1) count=std::max<std::size_t>(2,(nodes+1)/2);
    else if(serial==2) count=nodes;
    else count=2+serial%(nodes-1);
    FixedBitflipHardware h;
    for(std::size_t j=0;j<count;++j) {
        // First three structures cover small/medium/full H. Later reserved
        // global proposals rotate nodes, monitors and bits independently of H*.
        const auto offset=serial<3?j:(serial%2?nodes-1-j:j);
        const int n=p.nodes[(offset+(serial<3?0:serial))%nodes];
        auto signal=preferredSignal(p,n);
        if(serial>=3) signal=monitors[(serial+j)%3];
        h.push_back({n,signal,int((serial<3?3+serial:serial+j)%12)});
    }
    std::sort(h.begin(),h.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});return h;
}
FixedBitflipHardware localProposal(const TenTrainCandidate& parent,const Priority& p,std::size_t serial) {
    auto h=parent.hardware;const auto index=serial%h.size();
    switch(serial%4) {
        case 0:
            if(h.size()<p.nodes.size()) {
                for(int n:p.nodes) if(std::none_of(h.begin(),h.end(),[&](const auto& x){return x.nodeId==n;})) {
                    h.push_back({n,preferredSignal(p,n),int((serial+3)%12)});break;
                }
            } else h[index].bitIndex=(h[index].bitIndex+1)%12;
            break;
        case 1:
            if(h.size()>2) h.erase(h.begin()+std::ptrdiff_t(index));
            else h[index].bitIndex=(h[index].bitIndex+1)%12;
            break;
        case 2: {
            const auto it=std::find(monitors.begin(),monitors.end(),h[index].monitorInput);
            h[index].monitorInput=monitors[(std::size_t(it-monitors.begin())+1)%3];break;
        }
        case 3:h[index].bitIndex=(h[index].bitIndex+1)%12;break;
    }
    std::sort(h.begin(),h.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});return h;
}
using Snapshot=std::vector<std::vector<JointBitflipConfiguration>>;
Snapshot warmSnapshot(const core::Application& app,const FixedBitflipHardware& h,
        const TenTrainCandidate* parent,const std::vector<TenTrainImage>& images) {
    Snapshot out(images.size());if(!parent) return out;
    for(std::size_t i=0;i<images.size();++i) {
        const auto e=std::find_if(parent->evaluations.begin(),parent->evaluations.end(),
            [&](const auto& x){return x.imageId==images[i].imageId;});
        if(e==parent->evaluations.end() || !e->legal) continue;
        JointBitflipConfiguration config;
        for(const auto& n:h) {
            const auto old=std::find_if(e->selected.configuration.begin(),e->selected.configuration.end(),
                [&](const auto& x){return x.nodeId==n.nodeId && x.monitorInput==n.monitorInput;});
            const int lower=old==e->selected.configuration.end()?
                (n.monitorInput==Signal::BaselineOutput?-4096:-2048):old->lower;
            const int upper=old==e->selected.configuration.end()?lower:old->upper;
            // A newly added/changed monitor's domain-min singleton is only a
            // proposal, never assumed dormant or executable. Native acceptance
            // rejects arithmetic range errors and independently adds its proven
            // dormant fallback. Every node retains the application's real unit.
            config.push_back({n.nodeId,baselineUnit(app,n.nodeId),n.monitorInput,lower,upper,
                core::AttackPayload::BitFlip,n.bitIndex});
        }
        out[i].push_back(std::move(config));
    }
    return out;
}
bool eliminate(const TenTrainCandidate& c,const TenTrainCandidate& incumbent,std::size_t total) {
    // Minimum observed drop is an upper bound on final worst drop. Equality
    // cannot prune: mean, node count and canonical H may still win. Pair calls
    // here used exactly the same full interval budgets as the incumbent.
    return detail::strictlyEliminated(c,incumbent,total);
}
}

bool betterTenTrainCandidate(const TenTrainCandidate& a,const TenTrainCandidate& b) {
    const bool ac=a.status=="complete",bc=b.status=="complete";if(ac!=bc) return ac;
    if(a.numericSuccesses!=b.numericSuccesses) return a.numericSuccesses>b.numericSuccesses;
    if(a.worstRoiDrop!=b.worstRoiDrop) return a.worstRoiDrop>b.worstRoiDrop;
    if(a.meanRoiDrop!=b.meanRoiDrop) return a.meanRoiDrop>b.meanRoiDrop;
    if(a.hardware.size()!=b.hardware.size()) return a.hardware.size()<b.hardware.size();
    return hardwareKey(a.hardware)<hardwareKey(b.hardware);
}

TenTrainResult searchCircuitTenTrain(const core::Application& app,const std::vector<TenTrainImage>& input,
        const TenTrainOptions& options,const std::function<void(const TenTrainProgress&)>& progress) {
    if(input.size()!=10) throw std::invalid_argument("Ten-image training requires exactly images 1..10.");
    if(!options.maxEvaluations || !options.maxCandidates || !options.initialCandidates ||
            !options.proposalsPerRound || !options.workers || !options.patience || !options.initialBatch ||
            (options.scheduling!="interleaved" && options.scheduling!="sequential") || !options.intervals.seeds.empty())
        throw std::invalid_argument("Invalid ten-image budgets or external interval seeds.");
    const auto nodes=app.addNodes().size();if(nodes<2) throw std::invalid_argument("Training needs at least two native nodes.");
    (void)baselineUnit(app,0);
    auto images=input;std::sort(images.begin(),images.end(),[](const auto& a,const auto& b){return a.imageId<b.imageId;});
    for(std::size_t i=0;i<images.size();++i) if(images[i].imageId!=int(i+1))
        throw std::invalid_argument("Ten-image training requires exactly images 1..10 without duplicates.");
    TenTrainResult result;result.scheduling=options.scheduling;const auto prepBegin=Clock::now();
    std::vector<std::shared_ptr<const CircuitFixedPreparedImage>> prepared;
    Priority priority;priority.nodes.resize(nodes);std::iota(priority.nodes.begin(),priority.nodes.end(),0);
    priority.nodeScores.resize(nodes);priority.signalScores.resize(nodes);
    for(const auto& image:images) {
        prepared.push_back(prepareCircuitFixedImage(app,image.image,image.mask));++result.referenceBuilds;
        result.baselines.push_back(prepared.back()->baselineQuality());
        // Match the fixed-image oracle's mask >=128 convention before any
        // application builds its baseline trace. DCT weights nonzero pixels per
        // block, so passing the original mask would count a 127 background ROI.
        cv::Mat binary;cv::compare(image.mask,127,binary,cv::CMP_GT);
        std::vector<core::AddSample> trace;app.collectBaselineAddSamples(image.image,binary,trace);
        accumulate(priority,trace,images.size());
    }
    std::stable_sort(priority.nodes.begin(),priority.nodes.end(),[&](int a,int b){return priority.nodeScores[a]>priority.nodeScores[b];});
    std::vector<std::size_t> imageOrder(images.size());std::iota(imageOrder.begin(),imageOrder.end(),0);
    if(options.scheduling=="interleaved") {
        std::stable_sort(imageOrder.begin(),imageOrder.end(),[&](auto a,auto b) {
            // PSNR is monotone decreasing in MSE. Comparing MSE avoids the
            // reporting score's 100 dB cap and handles exact baselines (MSE=0).
            return result.baselines[a].roiMse>result.baselines[b].roiMse;
        });
        const auto difficulty=imageOrder;const std::array<std::size_t,10> positions{0,9,4,2,7,1,8,3,6,5};
        for(std::size_t i=0;i<positions.size();++i) imageOrder[i]=difficulty[positions[i]];
        const auto remaining=options.maxEvaluations-std::min(options.maxEvaluations,images.size());
        result.effectiveCompletionReserve=std::min(options.completionReserve,
            remaining>options.initialBatch?remaining-options.initialBatch:std::size_t(0));
    }
    for(const auto i:imageOrder) result.evaluationImageOrder.push_back(images[i].imageId);
    result.preparationSeconds=seconds(prepBegin);const auto searchBegin=Clock::now();
    std::set<HardwareKey> seen;std::vector<Snapshot> snapshots;
    const auto add=[&](FixedBitflipHardware h,std::size_t generation,const TenTrainCandidate* parent)->bool {
        const auto k=hardwareKey(h);if(seen.count(k)) {++result.duplicateStructuresSkipped;return false;}
        if(result.candidates.size()>=options.maxCandidates) return false;
        // Copy all parent-derived seeds before adding/reallocating candidates.
        auto snapshot=warmSnapshot(app,h,parent,images);seen.insert(k);
        TenTrainCandidate c;c.hardwareId="ten_h"+std::to_string(result.candidates.size()+1);
        c.hardware=std::move(h);c.generation=generation;c.status="budget_incomplete";
        c.reason="not_evaluated";result.candidates.push_back(std::move(c));snapshots.push_back(std::move(snapshot));return true;
    };
    const auto evaluate=[&](std::size_t index,std::size_t limit,std::size_t allowance,const std::string& phase) {
        auto& candidate=result.candidates[index];std::size_t used=0;
        while(candidate.evaluations.size()<images.size() && result.actualEvaluations<limit && used<allowance) {
            std::vector<std::size_t> pending;
            for(const auto i:imageOrder) if(std::none_of(candidate.evaluations.begin(),candidate.evaluations.end(),
                    [&](const auto& e){return e.imageId==images[i].imageId;})) pending.push_back(i);
            const auto count=std::min({options.workers,pending.size(),limit-std::size_t(result.actualEvaluations),allowance-used});
            std::vector<std::future<TenTrainEvaluation>> tasks;tasks.reserve(count);
            // Reserve the entire batch before any worker can start a call.
            result.actualEvaluations+=count;
            if(index==0) result.referenceEvaluations+=count;
            else if(phase=="completion") result.completionEvaluations+=count;
            else result.explorationEvaluations+=count;
            for(std::size_t j=0;j<count;++j) {
                const auto i=pending[j];auto intervalOptions=options.intervals;intervalOptions.seeds=snapshots[index][i];
                tasks.push_back(std::async(std::launch::async,[&,i,intervalOptions] {
                    const auto begin=Clock::now();const auto& image=images[i];
                    const auto found=searchCircuitFixedBitflipIntervals(app,image.image,image.mask,candidate.hardware,
                        intervalOptions,{},prepared[i].get());
                    TenTrainEvaluation e;e.phase=phase;e.imageId=image.imageId;e.baseline=found.baseline;e.stats=found.stats;
                    if(!found.candidates.empty()) {e.selected=found.candidates.front();e.legal=true;e.numericEligible=true;}
                    else if(found.bestLegal) {e.selected=*found.bestLegal;e.legal=true;}
                    e.elapsedSeconds=seconds(begin);return e;
                }));
            }
            for(std::size_t j=0;j<count;++j) {
                candidate.evaluations.push_back(tasks[j].get());
                if(progress) progress({result.candidates.size(),std::size_t(result.actualEvaluations),
                    candidate.hardwareId,candidate.evaluations.back().imageId});
            }
            used+=count;summarize(candidate,images.size());
            if(candidate.evaluations.size()<images.size() && result.bestIndex && eliminate(candidate,result.candidates[*result.bestIndex],images.size())) {
                candidate.status="bound_eliminated";candidate.reason="strict_full_pair_success_or_worst_bound";return;
            }
        }
        if(candidate.evaluations.size()==images.size()) {
            if(phase=="completion") ++result.completionCandidates;
            candidate.status="complete";candidate.reason="all_training_images_evaluated";
            if(!result.bestIndex || betterTenTrainCandidate(candidate,result.candidates[*result.bestIndex])) result.bestIndex=index;
        } else {candidate.status="budget_incomplete";candidate.reason=result.actualEvaluations>=options.maxEvaluations?
            "pair_evaluation_budget_exhausted":"suspended_for_completion";}
    };
    std::size_t globalSerial=0,localSerial=0;
    if(options.scheduling=="interleaved") {
        add(globalProposal(priority,globalSerial++),0,nullptr);
        evaluate(0,options.maxEvaluations,images.size(),"reference");
        const auto explorationLimit=options.maxEvaluations-result.effectiveCompletionReserve;
        const auto prunePending=[&] {
            if(!result.bestIndex) return;
            for(auto& c:result.candidates) if(c.status=="budget_incomplete" &&
                    detail::strictlyEliminated(c,result.candidates[*result.bestIndex],images.size())) {
                c.status="bound_eliminated";c.reason="strict_full_pair_success_or_worst_bound";
            }
        };
        const auto wave=[&](std::size_t count,std::size_t generation,const TenTrainCandidate* parent) {
            std::vector<std::size_t> added;
            // Freeze all proposals and seeds for this wave before any pair runs.
            for(std::size_t p=0;p<count && result.candidates.size()<options.maxCandidates &&
                    result.actualEvaluations<explorationLimit;++p) {
                const bool global=generation==0 || p==0 || !parent;
                auto h=global?globalProposal(priority,globalSerial++):localProposal(*parent,priority,localSerial++);
                if(add(std::move(h),generation,global?nullptr:parent)) added.push_back(result.candidates.size()-1);
            }
            // Every wave member gets its initial batch before any promotion.
            for(const auto i:added) {
                if(result.actualEvaluations>=explorationLimit) break;
                evaluate(i,explorationLimit,options.initialBatch,"exploration");
            }
            prunePending();
            if(result.actualEvaluations<explorationLimit) {
                const auto next=detail::completionChoice(result.candidates,result.bestIndex,images.size());
                if(next) evaluate(*next,explorationLimit,images.size(),"exploration");
                prunePending();
            }
        };
        if(result.actualEvaluations<explorationLimit)
            wave(options.initialCandidates-1,0,nullptr);
        std::size_t stale=0;
        for(std::size_t round=1;round<=options.maxRounds && result.actualEvaluations<explorationLimit &&
                result.candidates.size()<options.maxCandidates;++round) {
            const auto before=result.bestIndex;
            const auto parent=before?std::optional<TenTrainCandidate>(result.candidates[*before]):std::nullopt;
            wave(options.proposalsPerRound,round,parent?&*parent:nullptr);
            if(result.bestIndex==before) ++stale;else stale=0;
            if(stale>=options.patience) {result.stopReason="patience_exhausted";break;}
        }
        prunePending();
        while(result.actualEvaluations<options.maxEvaluations) {
            const auto next=detail::completionChoice(result.candidates,result.bestIndex,images.size());
            if(!next) break;
            evaluate(*next,options.maxEvaluations,images.size(),"completion");prunePending();
        }
    } else {
    for(std::size_t i=0;i<options.initialCandidates && result.candidates.size()<options.maxCandidates &&
            result.actualEvaluations<options.maxEvaluations;++i) {
        if(add(globalProposal(priority,globalSerial++),0,nullptr)) evaluate(result.candidates.size()-1,options.maxEvaluations,images.size(),"sequential");
    }
    std::size_t stale=0;
    for(std::size_t round=1;round<=options.maxRounds && result.actualEvaluations<options.maxEvaluations &&
            result.candidates.size()<options.maxCandidates;++round) {
        const auto before=result.bestIndex;
        // Every round reserves one independent global proposal. Local proposals
        // share the incumbent captured at the start of the round, never a seed
        // revised after an already calibrated duplicate H.
        const auto parent=before?std::optional<TenTrainCandidate>(result.candidates[*before]):std::nullopt;
        for(std::size_t p=0;p<options.proposalsPerRound && result.actualEvaluations<options.maxEvaluations &&
                result.candidates.size()<options.maxCandidates;++p) {
            // Dispatch the reserved global proposal first so a short remaining
            // pair budget cannot spend the entire round on local mutations.
            const bool global=p==0 || !parent;
            auto h=global?globalProposal(priority,globalSerial++):localProposal(*parent,priority,localSerial++);
            if(add(std::move(h),round,global?nullptr:&*parent)) evaluate(result.candidates.size()-1,options.maxEvaluations,images.size(),"sequential");
        }
        if(result.bestIndex==before) ++stale;else stale=0;
        if(stale>=options.patience) {result.stopReason="patience_exhausted";break;}
    }
    }
    if(result.actualEvaluations>=options.maxEvaluations) result.stopReason="evaluation_budget_exhausted";
    else if(result.stopReason.empty()) {
        if(result.candidates.size()>=options.maxCandidates) result.stopReason="candidate_budget_exhausted";
        else result.stopReason="round_budget_exhausted";
    }
    for(auto& c:result.candidates) if(c.status=="budget_incomplete")
        c.reason=result.actualEvaluations>=options.maxEvaluations?
            "pair_evaluation_budget_exhausted":"search_terminated_before_completion";
    result.searchSeconds=seconds(searchBegin);return result;
}
}

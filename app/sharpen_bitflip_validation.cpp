#include "analysis/sharpen_bitflip_search.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
using Candidate=analysis::SharpenBitflipCandidate;
using Quality=analysis::BitflipQuality;
std::string two(int i) { std::ostringstream s; s<<std::setfill('0')<<std::setw(2)<<i; return s.str(); }
const char* signal(core::MonitorSignal s) {
    return s==core::MonitorSignal::Input1 ? "input1" : s==core::MonitorSignal::Input2 ? "input2" : "baseline_output";
}
std::ofstream open(const fs::path& path) {
    std::ofstream out(path); if(!out) throw std::runtime_error("Cannot write "+path.string());
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out<<std::setprecision(12); return out;
}
void header(std::ofstream& out) {
    out<<"stage,image,node_id,node_name,monitor,bit,has_interval,success,lower,upper,"
        "baseline_global_psnr,baseline_roi_psnr,baseline_non_roi_psnr,global_psnr,roi_psnr,non_roi_psnr,"
        "delta_roi_mse,delta_non_roi_mse,advantage,trigger_rate,roi_trigger_rate,non_roi_trigger_rate,"
        "evaluated_intervals,range_rejected_intervals\n";
}
void row(std::ofstream& out,int stage,int image,const Candidate& c,const Quality& b) {
    out<<stage<<','<<image<<','<<c.config.nodeId<<','
       <<applications::SharpenApplication::nodeName(c.config.nodeId)<<','<<signal(c.config.monitorInput)<<','
       <<c.config.bitIndex<<','<<c.hasInterval<<','<<c.successful(b)<<',';
    if(c.hasInterval) out<<c.config.lower<<','<<c.config.upper; else out<<',';
    out<<','<<b.globalPsnr()<<','<<b.roiPsnr()<<','<<b.nonRoiPsnr()<<',';
    if(c.hasInterval) {
        double dr=c.quality.roiMse-b.roiMse, dn=c.quality.nonRoiMse-b.nonRoiMse;
        out<<c.quality.globalPsnr()<<','<<c.quality.roiPsnr()<<','<<c.quality.nonRoiPsnr()<<','
           <<dr<<','<<dn<<','<<dr-dn<<','<<c.triggerRate<<','<<c.roiTriggerRate<<','<<c.nonRoiTriggerRate;
    } else out<<",,,,,,,,";
    out<<','<<c.evaluatedIntervals<<','<<c.rangeRejectedIntervals<<'\n';
}
void verify(const applications::SharpenApplication& app,const cv::Mat& input,const cv::Mat& mask,
            const Candidate& c,const fs::path& prefix,bool save) {
    if(!c.hasInterval) return;
    const auto exact=app.runExact(input), baseline=app.runApprox(input,{}), attack=app.runApprox(input,{c.config});
    const auto q=analysis::measureBitflipQuality(exact,attack,mask);
    if(std::abs(q.globalMse-c.quality.globalMse)>1e-7 || std::abs(q.roiMse-c.quality.roiMse)>1e-7 ||
       std::abs(q.nonRoiMse-c.quality.nonRoiMse)>1e-7 || q.globalPsnr()+1e-9<30)
        throw std::runtime_error("Selected interval failed full application verification.");
    if(save) {
        for(const auto& item : std::vector<std::pair<std::string,cv::Mat>>{
            {"_exact.png",exact},{"_baseline.png",baseline},{"_attack.png",attack},{"_roi.png",mask}})
            if(!cv::imwrite(prefix.string()+item.first,item.second)) throw std::runtime_error("Cannot save image.");
    }
}
const Candidate* best(const std::vector<Candidate>& candidates) {
    const Candidate* found=nullptr;
    for(const auto& c:candidates) if(c.hasInterval && (!found || c.quality.roiMse>found->quality.roiMse ||
        (c.quality.roiMse==found->quality.roiMse && c.quality.globalMse<found->quality.globalMse))) found=&c;
    return found;
}
struct Score {
    int success=0, feasible=0;
    double worstRoi=-1, sumRoi=0, sumAdvantage=0;
};

int runFixed(int argc,char** argv) {
    if(argc<7 || argc>10) throw std::runtime_error(
        "Usage: sharpen_bitflip_validation --fixed DATA_DIR OUTPUT_DIR NODE_ID MONITOR BIT [boundary_budget=0] [first_image=21] [last_image=30]");
    const fs::path data=argv[2], output=argv[3];
    const std::string name=argv[5];
    if(name!="input1" && name!="input2" && name!="baseline_output") throw std::runtime_error("Unknown monitor name.");
    core::AttackConfig fixed{std::stoi(argv[4]),approximate::ApproxUnitId::Add12se5RP,
        name=="input1" ? core::MonitorSignal::Input1 : name=="input2" ? core::MonitorSignal::Input2 : core::MonitorSignal::BaselineOutput,
        0,0,core::AttackPayload::BitFlip,std::stoi(argv[6])};
    const int budget=argc>=8 ? std::stoi(argv[7]) : 0;
    const int first=argc>=9 ? std::stoi(argv[8]) : 21, last=argc>=10 ? std::stoi(argv[9]) : 30;
    if(budget<0 || budget==1 || first<1 || last>30 || first>last) throw std::runtime_error("Invalid fixed evaluation range/budget.");
    if(fs::exists(output/"summary.csv")) throw std::runtime_error("Output already contains a trial; use a new directory.");
    fs::create_directories(output);
    auto summary=open(output/"summary.csv"); header(summary); summary.flush();
    applications::SharpenApplication app;
    const auto shared=image_io::loadGrayImage((data/"mask"/"roi_mask.jpg").string());
    for(int i=first;i<=last;++i) {
        const int stage=(i-1)/10+1;
        const auto input=image_io::loadGrayImage((data/("stage"+std::to_string(stage))/"input"/("image_"+two(i)+".jpg")).string());
        const auto mask=region_mask::resizeMaskToImage(shared,input);
        const auto result=analysis::SharpenBitflipSearch::evaluate(app,input,mask,budget,30,&fixed);
        const auto& candidate=result.candidates.at(0);
        row(summary,stage,i,candidate,result.baseline); summary.flush();
        verify(app,input,mask,candidate,output/("stage"+std::to_string(stage)+"_image_"+two(i)),true);
        std::cout<<"Fixed image "<<i<<" success="<<candidate.successful(result.baseline);
        if(candidate.hasInterval) std::cout<<" G/ROI="<<candidate.quality.globalPsnr()<<'/'<<candidate.quality.roiPsnr();
        std::cout<<std::endl;
    }
    summary.close();
    auto hardware=open(output/"fixed_hardware.csv");
    hardware<<"node_id,node_name,monitor,bit,baseline,global_psnr_threshold,boundary_budget\n"
            <<fixed.nodeId<<','<<applications::SharpenApplication::nodeName(fixed.nodeId)<<','<<name<<','
            <<fixed.bitIndex<<",all_5RP,30,"<<budget<<'\n'; hardware.close();
    auto notes=open(output/"RUN.txt");
    notes<<"Externally supplied fixed hardware; only intervals adapted. No structure search or selection on these images.\n"
         <<"Existing images reused; no synthesis evidence. Exact Sharpening reference; existing data/mask/roi_mask.jpg.\n"
         <<"Boundary budget "<<budget<<"; image range "<<first<<".."<<last<<".\n";
    notes.close(); return 0;
}
}

int main(int argc,char** argv) {
    try {
        if(argc>=2 && std::string(argv[1])=="--fixed") return runFixed(argc,argv);
        if(argc<3 || argc>5) {
            std::cerr<<"Usage: sharpen_bitflip_validation DATA_DIR OUTPUT_DIR [boundary_budget=24] [images_per_stage=10]\n";
            return 2;
        }
        const fs::path data=argv[1], output=argv[2];
        const int budget=argc>=4 ? std::stoi(argv[3]) : 24;
        const int count=argc>=5 ? std::stoi(argv[4]) : 10;
        if(budget<0 || budget==1 || count<1 || count>10) throw std::runtime_error("Invalid budget or image count.");
        // Refuse accidental overwrite of a previous trial.
        if(fs::exists(output/"summary.csv")) throw std::runtime_error("Output already contains a trial; use a new directory.");
        fs::create_directories(output);
        auto candidates=open(output/"candidates.csv"), summary=open(output/"summary.csv");
        header(candidates); header(summary);
        candidates.flush(); summary.flush();
        applications::SharpenApplication app;
        const auto sharedMask=image_io::loadGrayImage((data/"mask"/"roi_mask.jpg").string());
        std::vector<Score> scores;
        std::vector<core::AttackConfig> structures;
        core::AttackConfig frozen{};
        for(int stage=1;stage<=3;++stage) {
            if(stage==3) {
                std::vector<std::size_t> order(scores.size()); std::iota(order.begin(),order.end(),0);
                std::stable_sort(order.begin(),order.end(),[&](auto a,auto b) {
                    const auto& x=scores[a]; const auto& y=scores[b];
                    if(x.success!=y.success) return x.success>y.success;
                    if(x.feasible!=y.feasible) return x.feasible>y.feasible;
                    if(x.worstRoi!=y.worstRoi) return x.worstRoi<y.worstRoi;
                    if(x.sumRoi!=y.sumRoi) return x.sumRoi<y.sumRoi;
                    return x.sumAdvantage>y.sumAdvantage;
                });
                if(order.empty() || scores[order[0]].feasible==0) throw std::runtime_error("No feasible Stage2 hardware; see candidates.csv.");
                frozen=structures[order[0]];
                auto ranking=open(output/"hardware_ranking.csv");
                ranking<<"rank,node_id,node_name,monitor,bit,success_count,feasible_count,image_count,worst_roi_psnr,mean_roi_psnr,mean_advantage\n";
                for(std::size_t rank=0;rank<order.size();++rank) {
                    const auto i=order[rank]; const auto& c=structures[i]; const auto& s=scores[i];
                    ranking<<rank+1<<','<<c.nodeId<<','<<applications::SharpenApplication::nodeName(c.nodeId)<<','
                           <<signal(c.monitorInput)<<','<<c.bitIndex<<','<<s.success<<','<<s.feasible<<','<<count<<','
                           <<s.worstRoi<<','<<(s.feasible ? s.sumRoi/s.feasible : 0)<<','
                           <<(s.feasible ? s.sumAdvantage/s.feasible : 0)<<'\n';
                }
                ranking.close();
                auto hardware=open(output/"fixed_hardware.csv");
                hardware<<"node_id,node_name,monitor,bit,baseline,global_psnr_threshold,boundary_budget\n"
                        <<frozen.nodeId<<','<<applications::SharpenApplication::nodeName(frozen.nodeId)<<','
                        <<signal(frozen.monitorInput)<<','<<frozen.bitIndex<<",all_5RP,30,"<<budget<<'\n';
                hardware.close();
                std::cout<<"Frozen hardware: "<<applications::SharpenApplication::nodeName(frozen.nodeId)
                         <<' '<<signal(frozen.monitorInput)<<" bit "<<frozen.bitIndex<<std::endl;
            }
            for(int i=1;i<=count;++i) {
                const int imageIndex=(stage-1)*10+i;
                const auto input=image_io::loadGrayImage((data/("stage"+std::to_string(stage))/"input"/
                    ("image_"+two(imageIndex)+".jpg")).string());
                const auto mask=region_mask::resizeMaskToImage(sharedMask,input);
                const auto result=analysis::SharpenBitflipSearch::evaluate(app,input,mask,budget,30,
                    stage==3 ? &frozen : nullptr);
                for(const auto& c:result.candidates) row(candidates,stage,imageIndex,c,result.baseline);
                if(stage==2) {
                    if(scores.empty()) {
                        scores.resize(result.candidates.size());
                        for(const auto& c:result.candidates) structures.push_back(c.config);
                    }
                    for(std::size_t j=0;j<result.candidates.size();++j) {
                        const auto& c=result.candidates[j]; auto& s=scores[j];
                        s.success+=c.successful(result.baseline);
                        if(c.hasInterval) {
                            ++s.feasible; s.worstRoi=std::max(s.worstRoi,c.quality.roiPsnr());
                            s.sumRoi+=c.quality.roiPsnr();
                            s.sumAdvantage+=c.quality.roiMse-result.baseline.roiMse-c.quality.nonRoiMse+result.baseline.nonRoiMse;
                            // Check every Stage2 hardware's chosen interval, not just the winner.
                            verify(app,input,mask,c,{},false);
                        } else s.worstRoi=std::numeric_limits<double>::infinity();
                    }
                }
                const auto* selected=stage==3 ? &result.candidates.at(0) : best(result.candidates);
                if(!selected) selected=&result.candidates.at(0);
                row(summary,stage,imageIndex,*selected,result.baseline);
                verify(app,input,mask,*selected,output/("stage"+std::to_string(stage)+"_image_"+two(imageIndex)),stage!=2);
                std::cout<<"Stage "<<stage<<" image "<<imageIndex<<": baseline G/ROI "
                         <<result.baseline.globalPsnr()<<'/'<<result.baseline.roiPsnr();
                if(selected->hasInterval) std::cout<<" -> "<<selected->quality.globalPsnr()<<'/'<<selected->quality.roiPsnr()
                    <<", "<<applications::SharpenApplication::nodeName(selected->config.nodeId)<<' '
                    <<signal(selected->config.monitorInput)<<" bit "<<selected->config.bitIndex
                    <<" ["<<selected->config.lower<<','<<selected->config.upper<<"] success="<<selected->successful(result.baseline);
                else std::cout<<" -> NO FEASIBLE INTERVAL";
                std::cout<<std::endl; candidates.flush(); summary.flush();
            }
        }
        candidates.close(); summary.close();
        auto notes=open(output/"RUN.txt");
        notes<<"Single-node Sharpening bit-flip feasibility trial. Existing images reused; not unseen-data evidence.\n"
             <<"Stage1 free search; Stage2 screens all 144 fixed structures; Stage3 frozen structure, per-image intervals.\n"
             <<"Stage2 summary rows are free-search diagnostics; actual hardware ranking uses all Stage2 candidates.\n"
             <<"No hardware synthesis or area/delay claim. Fixed output bit needs no runtime bit selector.\n"
             <<"Errors relative to exact Sharpening output. ROI is existing resized data/mask/roi_mask.jpg, >=128.\n"
             <<"Success: feasible Global>=30, ROI MSE increases, and deltaROI-deltaNonROI>0.\n"
             <<"Boundary budget "<<budget<<"; 0 enumerates all observed endpoints, otherwise evenly spaced distinct-value grid.\n"
             <<"Images per stage "<<count<<"; indices 1..N,11..10+N,21..20+N.\n"
             <<"trigger_rate: interior evaluated pixels; ROI/non-ROI trigger rates: all pixels in each region.\n";
        notes.close();
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

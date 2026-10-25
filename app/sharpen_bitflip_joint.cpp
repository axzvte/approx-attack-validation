#include "analysis/sharpen_bitflip_joint.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
using Config=analysis::JointBitflipConfiguration;
using Quality=analysis::BitflipQuality;
using Row=std::map<std::string,std::string>;
std::string number(double x) {std::ostringstream s;s<<std::setprecision(15)<<x;return s.str();}
std::ofstream open(const fs::path& p,std::ios::openmode mode=std::ios::out) {
    std::ofstream f;f.exceptions(std::ios::failbit|std::ios::badbit);f.open(p,mode);return f;
}
std::string quoted(const std::string& value) {
    if(value.find_first_of(",\"\r\n")==std::string::npos) return value;
    std::string result="\"";for(char c:value) {if(c=='\"') result+='\"';result+=c;}return result+'\"';
}
struct Csv {
    std::ofstream stream;std::vector<std::string> fields;
    Csv(const fs::path& path,std::vector<std::string> names):stream(open(path)),fields(std::move(names)) {
        for(std::size_t i=0;i<fields.size();++i) stream<<(i?",":"")<<fields[i];stream<<'\n';
    }
    void row(const Row& values) {
        for(std::size_t i=0;i<fields.size();++i) {
            if(i) stream<<',';auto it=values.find(fields[i]);if(it!=values.end()) stream<<quoted(it->second);
        }
        stream<<'\n';
    }
    void close() {stream.close();}
};
const char* signal(core::MonitorSignal m) {
    return m==core::MonitorSignal::Input1?"input1":m==core::MonitorSignal::Input2?"input2":"baseline_output";
}
std::string identity(Config config) {
    std::sort(config.begin(),config.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});
    std::ostringstream s;for(const auto& c:config) s<<c.nodeId<<':'<<c.bitIndex<<':'<<int(c.monitorInput)<<':'<<c.lower<<':'<<c.upper<<';';return s.str();
}
void quality(Row& row,const Quality& q) {
    row["global_psnr"]=number(q.globalPsnr());row["roi_psnr"]=number(q.roiPsnr());row["non_roi_psnr"]=number(q.nonRoiPsnr());
    row["global_mse"]=number(q.globalMse);row["roi_mse"]=number(q.roiMse);row["non_roi_mse"]=number(q.nonRoiMse);
}
void compare(const Quality& a,const Quality& b) {
    if(std::abs(a.globalMse-b.globalMse)>1e-7 || std::abs(a.roiMse-b.roiMse)>1e-7 ||
            std::abs(a.nonRoiMse-b.nonRoiMse)>1e-7)
        throw std::runtime_error("Joint conditional metrics disagree with original application replay.");
}
std::array<std::array<std::uint64_t,2>,4> triggers(const applications::SharpenApplication& app,
    const cv::Mat& input,const cv::Mat& mask,const Config& config) {
    std::array<std::array<std::uint64_t,2>,4> count{};
    std::vector<core::AddSample> trace;app.collectConfiguredAddSamples(input,mask,config,trace);
    for(const auto& sample:trace) for(const auto& c:config) if(c.nodeId==sample.nodeId) {
        const int v=c.monitorInput==core::MonitorSignal::Input1?sample.input1:
            c.monitorInput==core::MonitorSignal::Input2?sample.input2:sample.baselineOutput;
        if(v>=c.lower && v<=c.upper) ++count[c.nodeId][sample.roiWeight>=0.5?1:0];
    }
    return count;
}
void save(const fs::path& path,const cv::Mat& image) {
    if(!cv::imwrite(path.string(),image)) throw std::runtime_error("Cannot save "+path.string());
}
cv::Mat comparison(const std::array<cv::Mat,3>& images) {
    cv::Mat panel;cv::hconcat(std::vector<cv::Mat>{images[0],images[1],images[2]},panel);
    cv::copyMakeBorder(panel,panel,24,0,0,0,cv::BORDER_CONSTANT,cv::Scalar(0));
    const std::array<std::string,3> names{"exact","baseline","joint"};
    for(int i=0;i<3;++i) cv::putText(panel,names[i],cv::Point(i*images[0].cols+3,17),
        cv::FONT_HERSHEY_SIMPLEX,0.45,cv::Scalar(255),1,cv::LINE_AA);
    return panel;
}
struct Spatial {
    std::uint64_t count=0,changed=0,severe=0,severeChange=0;
    std::uint64_t sse=0;
};
std::array<Spatial,2> spatial(const cv::Mat& ref,const cv::Mat& baseline,const cv::Mat& out,
    const cv::Mat& mask,const cv::Rect& box,int threshold) {
    std::array<Spatial,2> stats{};
    for(int r=box.y;r<box.y+box.height;++r) for(int c=box.x;c<box.x+box.width;++c) {
        auto& s=stats[mask.at<unsigned char>(r,c)>=128?1:0];++s.count;
        const int error=int(out.at<unsigned char>(r,c))-ref.at<unsigned char>(r,c);
        const int change=int(out.at<unsigned char>(r,c))-baseline.at<unsigned char>(r,c);
        s.sse+=error*error;s.changed+=change!=0;s.severe+=std::abs(error)>=threshold;
        s.severeChange+=std::abs(change)>=threshold;
    }
    return stats;
}
void visualReports(const fs::path& folder,const std::string& id,const cv::Mat& ref,
    const cv::Mat& base,const cv::Mat& out,const cv::Mat& mask,Csv& tiles,Row& summary,
    Csv& imageFiles,bool exportImages,bool fullImages) {
    if(exportImages) {
        const auto image=[&](const std::string& kind,const cv::Mat& pixels) {
            const auto name=id+"_"+kind+".png";save(folder/name,pixels);
            imageFiles.row({{"candidate_id",id},{"image_kind",kind},{"path",name}});
        };
        cv::Mat binary;cv::compare(mask,cv::Scalar(128),binary,cv::CMP_GE);
        const auto box=cv::boundingRect(binary);
        image("output",out);
        cv::Mat roi=comparison({ref(box),base(box),out(box)});
        cv::resize(roi,roi,cv::Size(),2,2,cv::INTER_NEAREST);image("roi_comparison",roi);
        std::array<cv::Mat,3> backgrounds{ref.clone(),base.clone(),out.clone()};
        for(auto& pixels:backgrounds) pixels.setTo(127,binary);
        image("background_comparison",comparison(backgrounds));
        if(fullImages) {
            image("comparison",comparison({ref,base,out}));
            cv::Mat diff;cv::absdiff(base,out,diff);diff.convertTo(diff,CV_8U,4);image("diff_baseline_x4",diff);
            cv::absdiff(ref,out,diff);diff.convertTo(diff,CV_8U,4);image("diff_exact_x4",diff);
        }
    }
    const auto all=spatial(ref,base,out,mask,cv::Rect(0,0,out.cols,out.rows),16);
    for(int region=0;region<2;++region) {
        const std::string name=region?"roi":"non_roi";
        summary[name+"_changed_fraction"]=number(double(all[region].changed)/all[region].count);
        summary[name+"_error_ge16_fraction"]=number(double(all[region].severe)/all[region].count);
        summary[name+"_change_ge16_fraction"]=number(double(all[region].severeChange)/all[region].count);
    }
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
        const cv::Rect tile(x*out.cols/8,y*out.rows/8,(x+1)*out.cols/8-x*out.cols/8,(y+1)*out.rows/8-y*out.rows/8);
        if(!tile.width || !tile.height) continue;
        const auto stats=spatial(ref,base,out,mask,tile,16);
        for(int region=0;region<2;++region) {
            const auto& s=stats[region];
            Row row{{"candidate_id",id},{"tile_row",std::to_string(y)},{"tile_col",std::to_string(x)},
                {"region",region?"roi":"non_roi"},{"pixels",std::to_string(s.count)}};
            if(s.count) {
                row["mse_vs_exact"]=number(double(s.sse)/s.count);
                row["changed_fraction"]=number(double(s.changed)/s.count);
                row["error_ge16_fraction"]=number(double(s.severe)/s.count);
            }
            tiles.row(row);
        }
    }
}
int integer(const char* value) {
    std::string text=value;std::size_t used=0;int result=std::stoi(text,&used);
    if(used!=text.size()) throw std::runtime_error("Arguments must be complete integer values.");return result;
}
std::size_t contextLimit(const char* value) {
    const std::string text=value;
    if(text.empty() || text.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("Context budget must be a nonnegative integer.");
    const auto result=std::stoull(text);
    if(result>std::numeric_limits<std::size_t>::max() || (result && result<4))
        throw std::runtime_error("Context budget must be 0 (unlimited) or at least 4.");
    return static_cast<std::size_t>(result);
}
struct Verified {
    Config config;Quality metrics;cv::Mat image;
    std::array<std::array<std::uint64_t,2>,4> counts{};
};
}

int main(int argc,char** argv) {
    try {
        std::string imageMode="compact";bool imageModeSeen=false,contextBudgetSeen=false,noAblations=false;
        std::size_t contextBudget=0;
        std::vector<char*> positional{argv[0]};
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--image-mode") {
                if(imageModeSeen || i+1>=argc) throw std::runtime_error("Supply --image-mode once with compact, full or none.");
                imageMode=argv[++i];imageModeSeen=true;
            } else if(arg=="--context-budget") {
                if(contextBudgetSeen || i+1>=argc) throw std::runtime_error("Supply --context-budget once with 0 or a value >=4.");
                contextBudget=contextLimit(argv[++i]);contextBudgetSeen=true;
            } else if(arg=="--no-ablations") {
                if(noAblations) throw std::runtime_error("Supply --no-ablations at most once.");
                noAblations=true;
            } else if(arg.rfind("--",0)==0) throw std::runtime_error("Unknown option: "+arg);
            else positional.push_back(argv[i]);
        }
        argc=static_cast<int>(positional.size());argv=positional.data();
        if(imageMode!="compact" && imageMode!="full" && imageMode!="none")
            throw std::runtime_error("Image mode must be compact, full or none.");
        if(argc<3 || argc>10) throw std::runtime_error(
            "Usage: sharpen_bitflip_joint DATA_DIR NEW_OUTPUT_DIR [image=1] [beam=4] [refinement=1] [boundary_budget=24] [top_per_count=2] [restarts=2] [global_psnr=30] [--image-mode compact|full|none] [--context-budget N] [--no-ablations]");
        const fs::path data=argv[1];fs::path output=fs::absolute(argv[2]);
        while(output.has_relative_path() && (output.filename().empty() || output.filename()==".")) output=output.parent_path();
        const int imageIndex=argc>3?integer(argv[3]):1,beam=argc>4?integer(argv[4]):4;
        const int rounds=argc>5?integer(argv[5]):1,budget=argc>6?integer(argv[6]):24;
        const int top=argc>7?integer(argv[7]):2,restarts=argc>8?integer(argv[8]):2;
        std::size_t used=0;const double threshold=argc>9?std::stod(argv[9],&used):30;
        if(imageIndex<1 || imageIndex>30 || beam<1 || beam>64 || rounds<0 || rounds>10 ||
                budget<0 || budget==1 || budget>8192 || top<1 || top>10 || restarts<1 || restarts>8 ||
                !std::isfinite(threshold) || threshold<0 || threshold>1000 || (argc>9 && used!=std::string(argv[9]).size()))
            throw std::runtime_error("Invalid joint search arguments.");
        if(!output.parent_path().empty()) fs::create_directories(output.parent_path());
        if(!fs::create_directory(output)) throw std::runtime_error("Output already exists; use a new directory.");
        {
            auto notes=open(output/"RUN.txt");
            notes<<"completed=0\napplication=Sharpening\nbaseline=all_5RP\nroi="<<fs::absolute(data/"mask/roi_mask.jpg").string()
                 <<"\nimage="<<imageIndex<<"\nbeam_per_node_count="<<beam<<"\nrefinement_rounds="<<rounds
                 <<"\nboundary_budget="<<budget<<"\nrestarts="<<restarts<<"\nglobal_psnr_threshold="<<threshold
                 <<"\ncontext_budget="<<contextBudget<<"\nablations="<<(noAblations?"disabled":"enabled")
                 <<"\nimage_mode="<<imageMode
                 <<"\nvisual_status=pending_user_review\n"
                 <<"Bits 0..11 of signed 13-bit outputs; original units and ROI preserved.\n"
                 <<"ROI/background use all region pixels, including unchanged borders.\n"
                 <<"Intermediate quality failures allowed; range-invalid final paths rejected.\n"
                 <<"No hard background metric, no fixed global-MSE equality, no deltaROI>deltaNonROI gate.\n"
                 <<"Candidates have 2..4 actually-triggered nodes after zero-hit configurations are removed.\n"
                 <<"Final numeric candidates are replayed through original application. Visual approval is NOT automatic.\n"
                 <<"Comparisons left to right: exact / baseline / joint; background panels mask ROI to gray 127.\n"
                 <<"Difference images use fixed x4 saturation. Threshold16 coverage is diagnostic only, not a visual-success rule.\n"
                 <<(noAblations?"Ablations disabled; leave-one-node-out executions and reports omitted.\n":
                     "Ablations: without-minus-joint error; positive means removal increases error.\n")
                 <<"Per-image exploratory heuristic search; no optimality, unseen-data or synthesis claim.\n";
            notes.close();
        }
        std::ostringstream index;index<<std::setw(2)<<std::setfill('0')<<imageIndex;
        const auto input=image_io::loadGrayImage((data/("stage"+std::to_string((imageIndex-1)/10+1))/"input"/("image_"+index.str()+".jpg")).string());
        const auto mask=region_mask::resizeMaskToImage(image_io::loadGrayImage((data/"mask/roi_mask.jpg").string()),input);
        const applications::SharpenApplication app;
        const auto exact=app.runExact(input),baseline=app.runApprox(input,{});
        const auto baseQuality=analysis::measureBitflipQuality(exact,baseline,mask);
        Csv imageFiles(output/"images.csv",{"candidate_id","image_kind","path"});
        if(imageMode=="full") {
            for(const auto& entry:std::vector<std::pair<std::string,cv::Mat>>{{"exact",exact},{"baseline",baseline},{"roi_mask",mask}}) {
                const auto name=entry.first+".png";save(output/name,entry.second);
                imageFiles.row({{"candidate_id","reference"},{"image_kind",entry.first},{"path",name}});
            }
        }
        analysis::JointBitflipOptions options;options.beamWidth=beam;options.refinementRounds=rounds;
        options.boundaryBudget=budget;options.restarts=restarts;options.candidatesPerCount=std::max(top*4,8);
        options.globalPsnrThreshold=threshold;
        options.maxContexts=contextBudget;
        const auto result=analysis::searchJointBitflips(app,input,mask,options,[&](const auto& progress) {
            std::cout<<"Restart "<<progress.restart+1<<" pass "<<progress.pass+1<<" target A"<<progress.node+1
                     <<" state "<<progress.completedStates<<'/'<<progress.totalStates
                     <<" contexts="<<progress.stats.contextsBuilt<<" cache="<<progress.stats.cacheHits
                     <<" coverage="<<progress.stats.contextsByNode[0]<<'/'<<progress.stats.contextsByNode[1]<<'/'
                     <<progress.stats.contextsByNode[2]<<'/'<<progress.stats.contextsByNode[3]<<std::endl;
        });
        std::array<std::vector<Verified>,5> buckets;std::set<std::string> seen;
        std::size_t collapsed=0;
        for(const auto& state:result.candidates) {
            const auto full=app.runApprox(input,state.configuration);
            compare(analysis::measureBitflipQuality(exact,full,mask),state.metrics);
            const auto counts=triggers(app,input,mask,state.configuration);
            Config active;for(const auto& c:state.configuration) if(counts[c.nodeId][0]+counts[c.nodeId][1]) active.push_back(c);
            if(active.size()<state.configuration.size()) ++collapsed;
            if(active.size()<2 || !seen.insert(identity(active)).second) continue;
            std::sort(active.begin(),active.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});
            const auto cleaned=app.runApprox(input,active);
            if(cv::countNonZero(cleaned!=full)) throw std::runtime_error("Removing zero-hit nodes changed output.");
            const auto q=analysis::measureBitflipQuality(exact,cleaned,mask);
            if(q.globalPsnr()+1e-10<threshold || q.roiMse<=baseQuality.roiMse+1e-10)
                throw std::runtime_error("Final numeric candidate violated its reported constraints.");
            buckets[active.size()].push_back({active,q,cleaned,counts});
        }
        int bestCount=0;
        for(int count=2;count<=4;++count) {
            auto& candidates=buckets[count];
            std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
                return a.metrics.roiMse!=b.metrics.roiMse?a.metrics.roiMse>b.metrics.roiMse:a.metrics.globalMse<b.metrics.globalMse;
            });
            if(!candidates.empty() && (!bestCount || candidates.front().metrics.roiMse>buckets[bestCount].front().metrics.roiMse ||
                    (candidates.front().metrics.roiMse==buckets[bestCount].front().metrics.roiMse &&
                     candidates.front().metrics.globalMse<buckets[bestCount].front().metrics.globalMse))) bestCount=count;
        }
        Csv summary(output/"summary.csv",{"candidate_id","active_nodes","status","visual_status","images_exported","global_psnr","roi_psnr","non_roi_psnr",
            "global_mse","roi_mse","non_roi_mse","delta_roi_mse","delta_non_roi_mse","roi_changed_fraction","non_roi_changed_fraction",
            "roi_error_ge16_fraction","non_roi_error_ge16_fraction","roi_change_ge16_fraction","non_roi_change_ge16_fraction"});
        Csv configs(output/"configurations.csv",{"candidate_id","node_id","node_name","monitor","bit","lower","upper","roi_triggers","non_roi_triggers"});
        std::unique_ptr<Csv> ablations;
        if(!noAblations) ablations=std::make_unique<Csv>(output/"ablations.csv",std::vector<std::string>{"candidate_id","removed_node","status","global_psnr","roi_psnr","non_roi_psnr","global_mse","roi_mse","non_roi_mse",
            "global_mse_without_minus_joint","roi_mse_without_minus_joint","non_roi_mse_without_minus_joint"});
        Csv tiles(output/"tiles.csv",{"candidate_id","tile_row","tile_col","region","pixels","mse_vs_exact","changed_fraction","error_ge16_fraction"});
        Row base{{"candidate_id","baseline"},{"active_nodes","0"},{"status","reference"},{"visual_status","reference"},{"images_exported","0"}};
        quality(base,baseQuality);summary.row(base);
        for(int count=2;count<=4;++count) {
            auto& candidates=buckets[count];
            if(candidates.empty()) {
                summary.row({{"candidate_id","n"+std::to_string(count)+"_none"},{"active_nodes",std::to_string(count)},
                    {"status","no_numeric_candidate"},{"visual_status","not_available"},{"images_exported","0"}});continue;
            }
            for(std::size_t rank=0;rank<candidates.size() && rank<std::size_t(top);++rank) {
                const auto& candidate=candidates[rank];const auto id="n"+std::to_string(count)+"_rank"+std::to_string(rank+1);
                Row row{{"candidate_id",id},{"active_nodes",std::to_string(count)},{"status","numeric_eligible"},{"visual_status","pending_user_review"}};
                quality(row,candidate.metrics);row["delta_roi_mse"]=number(candidate.metrics.roiMse-baseQuality.roiMse);
                row["delta_non_roi_mse"]=number(candidate.metrics.nonRoiMse-baseQuality.nonRoiMse);
                const bool exportImages=imageMode=="full" || (imageMode=="compact" && count==bestCount && rank==0);
                row["images_exported"]=exportImages?"1":"0";
                visualReports(output,id,exact,baseline,candidate.image,mask,tiles,row,imageFiles,exportImages,imageMode=="full");summary.row(row);
                for(const auto& c:candidate.config) {
                    configs.row({{"candidate_id",id},{"node_id",std::to_string(c.nodeId)},{"node_name",applications::SharpenApplication::nodeName(c.nodeId)},
                        {"monitor",signal(c.monitorInput)},{"bit",std::to_string(c.bitIndex)},{"lower",std::to_string(c.lower)},{"upper",std::to_string(c.upper)},
                        {"roi_triggers",std::to_string(candidate.counts[c.nodeId][1])},{"non_roi_triggers",std::to_string(candidate.counts[c.nodeId][0])}});
                    if(noAblations) continue;
                    Config remaining;for(const auto& x:candidate.config) if(x.nodeId!=c.nodeId) remaining.push_back(x);
                    Row ablation{{"candidate_id",id},{"removed_node",std::to_string(c.nodeId)}};
                    cv::Mat removed;
                    try {removed=app.runApprox(input,remaining);} catch(const std::runtime_error&) {
                        ablation["status"]="range_invalid_after_removal";ablations->row(ablation);continue;
                    }
                    const auto q=analysis::measureBitflipQuality(exact,removed,mask);quality(ablation,q);
                    ablation["status"]="evaluated";ablation["global_mse_without_minus_joint"]=number(q.globalMse-candidate.metrics.globalMse);
                    ablation["roi_mse_without_minus_joint"]=number(q.roiMse-candidate.metrics.roiMse);
                    ablation["non_roi_mse_without_minus_joint"]=number(q.nonRoiMse-candidate.metrics.nonRoiMse);
                    ablations->row(ablation);
                    if(imageMode=="full") {
                        const auto kind="without_A"+std::to_string(c.nodeId+1),name=id+"_"+kind+".png";
                        save(output/name,removed);imageFiles.row({{"candidate_id",id},{"image_kind",kind},{"path",name}});
                    }
                }
                std::cout<<id<<": Global/ROI/background PSNR="<<candidate.metrics.globalPsnr()<<'/'
                         <<candidate.metrics.roiPsnr()<<'/'<<candidate.metrics.nonRoiPsnr()<<"; visual review pending"<<std::endl;
            }
        }
        summary.close();configs.close();if(ablations) ablations->close();tiles.close();imageFiles.close();
        auto stats=open(output/"search_stats.csv");
        stats<<"contexts_built,cache_hits,evaluated_intervals,range_rejected_intervals,retained_intermediate_quality_failures,inactive_collapses,context_budget,context_budget_exhausted,contexts_node_0,contexts_node_1,contexts_node_2,contexts_node_3\n"
             <<result.stats.contextsBuilt<<','<<result.stats.cacheHits<<','<<result.stats.evaluatedIntervals<<','<<result.stats.rangeRejected<<','
             <<result.stats.retainedQualityFailures<<','<<collapsed<<','<<contextBudget<<','<<result.stats.contextBudgetExhausted;
        for(auto count:result.stats.contextsByNode) stats<<','<<count;
        stats<<'\n';stats.close();
        auto notes=open(output/"RUN.txt",std::ios::out|std::ios::app);
        notes<<"best_numeric_candidate="<<(bestCount?"n"+std::to_string(bestCount)+"_rank1":"none")
             <<"\ncontexts_built="<<result.stats.contextsBuilt<<"\ncontext_budget_exhausted="<<result.stats.contextBudgetExhausted;
        for(std::size_t node=0;node<4;++node) notes<<"\ncontexts_node_"<<node<<'='<<result.stats.contextsByNode[node];
        notes<<"\ncompleted=1\n";notes.close();
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

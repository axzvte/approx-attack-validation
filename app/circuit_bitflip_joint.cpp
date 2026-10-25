#include "analysis/circuit_bitflip_joint.hpp"
#include "applications/conv.hpp"
#include "applications/sobel.hpp"
#include "applications/dct.hpp"
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
std::string number(double x){std::ostringstream s;s<<std::setprecision(17)<<x;return s.str();}
std::ofstream open(const fs::path& p,std::ios::openmode mode=std::ios::out){std::ofstream f;f.exceptions(std::ios::failbit|std::ios::badbit);f.open(p,mode);return f;}
std::string quoted(const std::string& v){if(v.find_first_of(",\"\r\n")==std::string::npos)return v;std::string s="\"";for(char c:v){if(c=='\"')s+='\"';s+=c;}return s+'\"';}
struct Csv {
    std::ofstream stream;std::vector<std::string> fields;
    Csv(const fs::path& p,std::vector<std::string> f):stream(open(p)),fields(std::move(f)){for(std::size_t i=0;i<fields.size();++i)stream<<(i?",":"")<<fields[i];stream<<'\n';}
    void row(const Row& r){for(std::size_t i=0;i<fields.size();++i){if(i)stream<<',';const auto p=r.find(fields[i]);if(p!=r.end())stream<<quoted(p->second);}stream<<'\n';}
    void close(){stream.close();}
};
int integer(const std::string& text){std::size_t n=0;int v=std::stoi(text,&n);if(n!=text.size())throw std::runtime_error("Expected complete integer argument.");return v;}
void quality(Row& r,const Quality& q){r["global_psnr"]=number(q.globalPsnr());r["roi_psnr"]=number(q.roiPsnr());r["non_roi_psnr"]=number(q.nonRoiPsnr());r["global_mse"]=number(q.globalMse);r["roi_mse"]=number(q.roiMse);r["non_roi_mse"]=number(q.nonRoiMse);}
void equalQuality(const Quality& a,const Quality& b){if(std::abs(a.globalMse-b.globalMse)>1e-7||std::abs(a.roiMse-b.roiMse)>1e-7||std::abs(a.nonRoiMse-b.nonRoiMse)>1e-7)throw std::runtime_error("Search score disagrees with original application replay.");}
std::string identity(Config c){std::sort(c.begin(),c.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});std::ostringstream s;for(const auto& v:c)s<<v.nodeId<<':'<<v.bitIndex<<':'<<int(v.monitorInput)<<':'<<v.lower<<':'<<v.upper<<';';return s.str();}
const char* signal(core::MonitorSignal s){return s==core::MonitorSignal::Input1?"input1":s==core::MonitorSignal::Input2?"input2":"baseline_output";}
struct Hits {std::uint64_t total=0;double roi=0,background=0;};
std::vector<Hits> triggers(const core::Application& app,const cv::Mat& input,const cv::Mat& mask,const Config& config){
    // Binary diagnostic mask avoids the legacy DCT !=0 interpretation of JPEG mask halos.
    cv::Mat binary;cv::compare(mask,cv::Scalar(128),binary,cv::CMP_GE);
    std::vector<Hits> counts(app.addNodes().size());std::vector<const core::AttackConfig*> byNode(counts.size(),nullptr);
    for(const auto& c:config)byNode.at(c.nodeId)=&c;
    std::vector<core::AddSample> trace;app.collectConfiguredAddSamples(input,binary,config,trace);
    for(const auto& s:trace){const auto* c=byNode.at(s.nodeId);if(!c)continue;const int v=c->monitorInput==core::MonitorSignal::Input1?s.input1:c->monitorInput==core::MonitorSignal::Input2?s.input2:s.baselineOutput;
        if(v>=c->lower&&v<=c->upper){auto& h=counts[s.nodeId];++h.total;h.roi+=s.roiWeight;h.background+=1-s.roiWeight;}}
    return counts;
}
cv::Mat comparison(const std::array<cv::Mat,3>& images){cv::Mat p;cv::hconcat(std::vector<cv::Mat>{images[0],images[1],images[2]},p);cv::copyMakeBorder(p,p,24,0,0,0,cv::BORDER_CONSTANT,cv::Scalar(0));const std::array<std::string,3> names{"exact","baseline","joint"};for(int i=0;i<3;++i)cv::putText(p,names[i],cv::Point(i*images[0].cols+3,17),cv::FONT_HERSHEY_SIMPLEX,.45,cv::Scalar(255),1,cv::LINE_AA);return p;}
void save(const fs::path& p,const cv::Mat& image){if(!cv::imwrite(p.string(),image))throw std::runtime_error("Cannot save "+p.string());}
void visuals(const fs::path& out,const std::string& id,const cv::Mat& exact,const cv::Mat& base,const cv::Mat& pixels,const cv::Mat& mask,Csv& manifest,bool full){
    auto image=[&](const std::string& kind,const cv::Mat& v){const auto name=id+"_"+kind+".png";save(out/name,v);manifest.row({{"candidate_id",id},{"image_kind",kind},{"path",name}});};
    cv::Mat binary;cv::compare(mask,cv::Scalar(128),binary,cv::CMP_GE);const auto box=cv::boundingRect(binary);
    image("output",pixels);cv::Mat roi=comparison({exact(box),base(box),pixels(box)});cv::resize(roi,roi,cv::Size(),2,2,cv::INTER_NEAREST);image("roi_comparison",roi);
    std::array<cv::Mat,3> bg{exact.clone(),base.clone(),pixels.clone()};for(auto& p:bg)p.setTo(127,binary);image("background_comparison",comparison(bg));
    if(full){image("comparison",comparison({exact,base,pixels}));cv::Mat diff;cv::absdiff(base,pixels,diff);diff.convertTo(diff,CV_8U,4);image("diff_baseline_x4",diff);cv::absdiff(exact,pixels,diff);diff.convertTo(diff,CV_8U,4);image("diff_exact_x4",diff);}
}
void spatial(Row& row,const cv::Mat& exact,const cv::Mat& base,const cv::Mat& out,const cv::Mat& mask){
    std::array<std::uint64_t,2> total{},changed{},error16{},change16{};
    for(int y=0;y<out.rows;++y)for(int x=0;x<out.cols;++x){int r=mask.at<unsigned char>(y,x)>=128;const int v=out.at<unsigned char>(y,x),b=base.at<unsigned char>(y,x),e=exact.at<unsigned char>(y,x);++total[r];changed[r]+=v!=b;error16[r]+=std::abs(v-e)>=16;change16[r]+=std::abs(v-b)>=16;}
    for(int r=0;r<2;++r){const std::string p=r?"roi":"non_roi";row[p+"_changed_fraction"]=number(double(changed[r])/total[r]);row[p+"_error_ge16_fraction"]=number(double(error16[r])/total[r]);row[p+"_change_ge16_fraction"]=number(double(change16[r])/total[r]);}
}
struct Verified {Config config;Quality metrics;cv::Mat image;std::vector<Hits> hits;};
}

int main(int argc,char** argv){try{
    if(argc<4)throw std::runtime_error("Usage: circuit_bitflip_joint conv|sobel|dct DATA_DIR NEW_OUTPUT_DIR [--image 1] [--beam 4] [--refinement 1] [--boundary-budget 24] [--top-per-count 2] [--restarts 2] [--global-psnr 30] [--max-active 0] [--max-contexts 256] [--max-full-evals 4096] [--dct-full-candidates 16] [--bits 0,1,...,11] [--image-mode compact|full|none] [--sensitivity-order]");
    const std::string application=argv[1];std::unique_ptr<core::Application> app;
    if(application=="conv")app=std::make_unique<applications::ConvApplication>();else if(application=="sobel")app=std::make_unique<applications::SobelApplication>();else if(application=="dct")app=std::make_unique<applications::DctApplication>();else throw std::runtime_error("Unknown circuit.");
    std::map<std::string,std::string> flags;const std::set<std::string> allowed{"--image","--beam","--refinement","--boundary-budget","--top-per-count","--restarts","--global-psnr","--max-active","--max-contexts","--max-full-evals","--dct-full-candidates","--bits","--image-mode"};
    for(int i=4;i<argc;){const std::string flag=argv[i];
        if(flag=="--sensitivity-order") {if(!flags.emplace(flag,"1").second)throw std::runtime_error("Duplicate --sensitivity-order");++i;continue;}
        if(!allowed.count(flag)||i+1>=argc||!flags.emplace(flag,argv[i+1]).second)throw std::runtime_error("Unknown, duplicate or incomplete option: "+flag);
        i+=2;
    }
    auto value=[&](const std::string& key,int fallback,int lo,int hi){const int n=flags.count(key)?integer(flags.at(key)):fallback;if(n<lo||n>hi)throw std::runtime_error("Invalid "+key);return n;};
    analysis::CircuitBitflipOptions options;
    options.sensitivityOrder=flags.count("--sensitivity-order")!=0;
    const int image=value("--image",1,1,40),top=value("--top-per-count",2,1,10),nodes=static_cast<int>(app->addNodes().size());
    options.beamWidth=value("--beam",4,1,64);options.refinementRounds=value("--refinement",1,0,10);options.boundaryBudget=value("--boundary-budget",24,0,8192);
    if(options.boundaryBudget==1)throw std::runtime_error("Boundary budget must be0 or>=2.");
    options.restarts=value("--restarts",2,1,8);options.maxActiveNodes=value("--max-active",0,0,nodes);if(options.maxActiveNodes==1)throw std::runtime_error("Multi-node search needs max-active0 or>=2.");
    options.maxContexts=value("--max-contexts",256,1,1000000);options.maxFullEvaluations=value("--max-full-evals",4096,1,10000000);options.dctFullCandidates=value("--dct-full-candidates",16,1,1024);options.candidatesPerCount=std::max(8,top*4);
    if(flags.count("--global-psnr")){std::size_t n=0;options.globalPsnrThreshold=std::stod(flags.at("--global-psnr"),&n);if(n!=flags.at("--global-psnr").size())throw std::runtime_error("Invalid PSNR argument.");}
    if(!std::isfinite(options.globalPsnrThreshold)||options.globalPsnrThreshold<0||options.globalPsnrThreshold>1000)throw std::runtime_error("Invalid PSNR threshold.");
    if(flags.count("--bits")){options.bits.clear();std::istringstream list(flags.at("--bits"));std::string bit;std::set<int> seen;if(flags.at("--bits").empty()||flags.at("--bits").back()==',')throw std::runtime_error("Invalid bits list.");while(std::getline(list,bit,',')){const int b=integer(bit);if(b<0||b>11||!seen.insert(b).second)throw std::runtime_error("Bits must be distinct and in0..11.");options.bits.push_back(b);}}
    const std::string imageMode=flags.count("--image-mode")?flags.at("--image-mode"):"compact";if(imageMode!="compact"&&imageMode!="full"&&imageMode!="none")throw std::runtime_error("Invalid image mode.");
    const fs::path data=fs::absolute(argv[2]);fs::path output=fs::absolute(argv[3]);while(output.has_relative_path()&&(output.filename().empty()||output.filename()=="."))output=output.parent_path();fs::create_directories(output.parent_path());if(!fs::create_directory(output))throw std::runtime_error("Output already exists; use a new directory.");
    {auto run=open(output/"RUN.txt");run<<"completed=0\napplication="<<application<<"\nimage="<<image<<"\nimage_mode="<<imageMode<<"\nbaseline=original_application_default\nmask_threshold=128\noutput_bits=signed13_non_sign_0..11\nreference=application_exact_output\nablations=disabled\n"
        <<"sensitivity_order="<<options.sensitivityOrder<<"\nsensitivity_basis=baseline_stratified_signal_separation_and_relative_raw_disturbance_priority_only\n"<<"beam="<<options.beamWidth<<"\nrefinement="<<options.refinementRounds<<"\nboundary_budget="<<options.boundaryBudget<<"\nrestarts="<<options.restarts<<"\ntop_per_count="<<top<<"\nmax_active="<<options.maxActiveNodes<<"\nmax_contexts="<<options.maxContexts<<"\nmax_full_evaluations="<<options.maxFullEvaluations<<"\ndct_full_candidates="<<options.dctFullCandidates<<"\nglobal_psnr_threshold="<<number(options.globalPsnrThreshold)<<"\nbits=";for(std::size_t i=0;i<options.bits.size();++i)run<<(i?",":"")<<options.bits[i];run<<"\nroi="<<(data/"mask/roi_mask.jpg").string()<<"\nvisual_status=pending_user_review\nPer-image exploratory joint search; no frozen-hardware/generalization/optimality claim.\nComparisons: exact / baseline / joint. DCT uses complete reconstructed-image metrics.\n";run.close();}
    const auto input=image_io::loadGrayImage(image_io::resolveStagedImagePath(data.string(),image));
    const auto mask=region_mask::resizeMaskToImage(image_io::loadGrayImage((data/"mask/roi_mask.jpg").string()),input);
    const auto exact=app->runExact(input),baseline=app->runApprox(input,{});const auto baselineQuality=analysis::measureBitflipQuality(exact,baseline,mask);
    std::set<std::size_t> contextNodes;std::uint64_t lastContexts=0;
    const auto result=analysis::searchCircuitBitflips(*app,input,mask,options,[&](const auto& p){if(p.stats.contextsBuilt>lastContexts)contextNodes.insert(p.node);lastContexts=p.stats.contextsBuilt;std::cout<<application<<" image "<<image<<" restart "<<p.restart+1<<" pass "<<p.pass+1<<" node "<<p.node<<" state "<<p.completedStates<<'/'<<p.totalStates<<" contexts="<<p.stats.contextsBuilt<<" cache="<<p.stats.cacheHits<<std::endl;});
    equalQuality(result.baseline,baselineQuality);
    const std::string searchStatus=(result.stats.contextBudgetExhausted||result.stats.fullBudgetExhausted)?"budget_exhausted":"completed";
    std::vector<std::vector<Verified>> buckets(nodes+1);std::set<std::string> seen;std::size_t inactive=0;
    for(const auto& state:result.candidates){const auto full=app->runApprox(input,state.configuration);equalQuality(analysis::measureBitflipQuality(exact,full,mask),state.metrics);const auto counts=triggers(*app,input,mask,state.configuration);Config active;for(const auto& c:state.configuration)if(counts.at(c.nodeId).total)active.push_back(c);if(active.size()!=state.configuration.size())++inactive;
        if(active.size()<2||!seen.insert(identity(active)).second)continue;std::sort(active.begin(),active.end(),[](const auto& a,const auto& b){return a.nodeId<b.nodeId;});const auto cleaned=app->runApprox(input,active);if(cv::countNonZero(cleaned!=full))throw std::runtime_error("Zero-trigger cleanup changed output.");const auto q=analysis::measureBitflipQuality(exact,cleaned,mask);if(q.globalPsnr()+1e-10<options.globalPsnrThreshold||q.roiMse<=baselineQuality.roiMse+1e-10)throw std::runtime_error("Final candidate violates constraints.");buckets.at(active.size()).push_back({active,q,cleaned,counts});}
    int bestCount=0;const int limit=options.maxActiveNodes?static_cast<int>(options.maxActiveNodes):nodes;
    for(int n=2;n<=limit;++n){auto& list=buckets[n];std::stable_sort(list.begin(),list.end(),[](const auto& a,const auto& b){return a.metrics.roiMse!=b.metrics.roiMse?a.metrics.roiMse>b.metrics.roiMse:a.metrics.globalMse<b.metrics.globalMse;});if(!list.empty()&&(!bestCount||list[0].metrics.roiMse>buckets[bestCount][0].metrics.roiMse||(list[0].metrics.roiMse==buckets[bestCount][0].metrics.roiMse&&list[0].metrics.globalMse<buckets[bestCount][0].metrics.globalMse)))bestCount=n;}
    Csv summary(output/"summary.csv",{"application","image","candidate_id","active_nodes","status","visual_status","search_status","images_exported","baseline_global_meets_threshold","global_psnr","roi_psnr","non_roi_psnr","global_mse","roi_mse","non_roi_mse","delta_roi_mse","delta_non_roi_mse","roi_changed_fraction","non_roi_changed_fraction","roi_error_ge16_fraction","non_roi_error_ge16_fraction","roi_change_ge16_fraction","non_roi_change_ge16_fraction"});
    Csv configs(output/"configurations.csv",{"candidate_id","node_id","node_name","monitor","bit","lower","upper","dynamic_triggers","roi_triggers","non_roi_triggers","trigger_region_basis"});
    Csv manifest(output/"images.csv",{"candidate_id","image_kind","path"});
    const Row common{{"application",application},{"image",std::to_string(image)},{"search_status",searchStatus},{"baseline_global_meets_threshold",baselineQuality.globalPsnr()+1e-10>=options.globalPsnrThreshold?"1":"0"}};
    auto base=common;base.insert({{"candidate_id","baseline"},{"active_nodes","0"},{"status","reference"},{"visual_status","reference"},{"images_exported","0"}});quality(base,baselineQuality);summary.row(base);
    if(imageMode=="full")for(const auto& v:std::vector<std::pair<std::string,cv::Mat>>{{"exact",exact},{"baseline",baseline},{"roi_mask",mask}}){const auto file=v.first+".png";save(output/file,v.second);manifest.row({{"candidate_id","reference"},{"image_kind",v.first},{"path",file}});}
    for(int n=2;n<=limit;++n){const auto& list=buckets[n];if(list.empty()){auto row=common;row.insert({{"candidate_id","n"+std::to_string(n)+"_none"},{"active_nodes",std::to_string(n)},{"status","no_numeric_candidate"},{"visual_status","not_available"},{"images_exported","0"}});summary.row(row);continue;}
        for(std::size_t rank=0;rank<list.size()&&rank<std::size_t(top);++rank){const auto& candidate=list[rank];const auto id="n"+std::to_string(n)+"_rank"+std::to_string(rank+1);const bool exportImages=imageMode=="full"||(imageMode=="compact"&&n==bestCount&&rank==0);auto row=common;row.insert({{"candidate_id",id},{"active_nodes",std::to_string(n)},{"status","numeric_eligible"},{"visual_status","pending_user_review"},{"images_exported",exportImages?"1":"0"}});quality(row,candidate.metrics);row["delta_roi_mse"]=number(candidate.metrics.roiMse-baselineQuality.roiMse);row["delta_non_roi_mse"]=number(candidate.metrics.nonRoiMse-baselineQuality.nonRoiMse);spatial(row,exact,baseline,candidate.image,mask);summary.row(row);if(exportImages)visuals(output,id,exact,baseline,candidate.image,mask,manifest,imageMode=="full");
            for(const auto& c:candidate.config){const auto& h=candidate.hits.at(c.nodeId);configs.row({{"candidate_id",id},{"node_id",std::to_string(c.nodeId)},{"node_name",app->addNodes().at(c.nodeId).name},{"monitor",signal(c.monitorInput)},{"bit",std::to_string(c.bitIndex)},{"lower",std::to_string(c.lower)},{"upper",std::to_string(c.upper)},{"dynamic_triggers",std::to_string(h.total)},{"roi_triggers",number(h.roi)},{"non_roi_triggers",number(h.background)},{"trigger_region_basis",application=="dct"?"block_roi_weight":"pixel_roi"}});}
            std::cout<<id<<" Global/ROI/background PSNR="<<candidate.metrics.globalPsnr()<<'/'<<candidate.metrics.roiPsnr()<<'/'<<candidate.metrics.nonRoiPsnr()<<" visual review pending"<<std::endl;}}
    summary.close();configs.close();manifest.close();
    std::ostringstream visited;for(auto n:contextNodes)visited<<(visited.tellp()>0?";":"")<<n;
    std::ostringstream priorityOrder;for(int n:result.stats.sensitivityOrder)priorityOrder<<(priorityOrder.tellp()>0?";":"")<<n;
    Csv stats(output/"search_stats.csv",{"contexts_built","cache_hits","evaluated_intervals","range_rejected_intervals","range_rejection_basis","retained_intermediate_quality_failures","full_evaluations","proposals_retained","inactive_collapses","context_budget_exhausted","full_budget_exhausted","search_status","max_contexts","max_full_evaluations","max_active","native_nodes","context_node_count","context_nodes","scoring","sensitivity_enabled","sensitivity_samples","sensitivity_seconds","sensitivity_node_order","trace_builds","trace_cache_hits"});
    stats.row({{"contexts_built",std::to_string(result.stats.contextsBuilt)},{"cache_hits",std::to_string(result.stats.cacheHits)},{"evaluated_intervals",std::to_string(result.stats.evaluatedIntervals)},{"range_rejected_intervals",std::to_string(result.stats.rangeRejected)},{"range_rejection_basis",application=="dct"?"full_configuration_evaluations":"conditional_interval_evaluations"},{"retained_intermediate_quality_failures",std::to_string(result.stats.retainedQualityFailures)},{"full_evaluations",std::to_string(result.stats.fullEvaluations)},{"proposals_retained",std::to_string(result.stats.proposalsRetained)},{"inactive_collapses",std::to_string(inactive)},{"context_budget_exhausted",result.stats.contextBudgetExhausted?"1":"0"},{"full_budget_exhausted",result.stats.fullBudgetExhausted?"1":"0"},{"search_status",searchStatus},{"max_contexts",std::to_string(options.maxContexts)},{"max_full_evaluations",std::to_string(options.maxFullEvaluations)},{"max_active",std::to_string(limit)},{"native_nodes",std::to_string(nodes)},{"context_node_count",std::to_string(contextNodes.size())},{"context_nodes",visited.str()},{"scoring",application=="dct"?"full_joint_image_reconstruction":"exact_conditional_pixel_replay"},{"sensitivity_enabled",options.sensitivityOrder?"1":"0"},{"sensitivity_samples",std::to_string(result.stats.sensitivitySamples)},{"sensitivity_seconds",number(result.stats.sensitivitySeconds)},{"sensitivity_node_order",priorityOrder.str()},{"trace_builds",std::to_string(result.stats.traceBuilds)},{"trace_cache_hits",std::to_string(result.stats.traceCacheHits)}});stats.close();
    auto notes=open(output/"RUN.txt",std::ios::app);notes<<"search_status="<<searchStatus<<"\nbest_numeric_candidate="<<(bestCount?"n"+std::to_string(bestCount)+"_rank1":"none")<<"\ncompleted=1\n";notes.close();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

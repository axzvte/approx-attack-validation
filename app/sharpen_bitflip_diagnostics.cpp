#include "analysis/sharpen_bitflip_diagnostics.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
using Sample=analysis::SharpenBaselineSample;
using Response=analysis::SharpenBitflipResponse;
using Count=std::uint64_t;
using Sum=std::int64_t;
Sum square(int x) {return static_cast<Sum>(x)*x;}
const char* region(bool roi) {return roi?"roi":"non_roi";}
const char* monitor(int i) {return i==0?"input1":i==1?"input2":"baseline_output";}
int monitorValue(const Sample& s,int i) {return i==0?s.input1:i==1?s.input2:s.baselineOutput;}
std::string two(int i) {std::ostringstream s;s<<std::setw(2)<<std::setfill('0')<<i;return s.str();}
std::ofstream open(const fs::path& path,std::ios::openmode mode=std::ios::out) {
    std::ofstream out;out.exceptions(std::ios::badbit|std::ios::failbit);
    out.open(path,mode);out<<std::setprecision(15);return out;
}
void mean(std::ostream& out,Sum sum,Count count) {
    out<<',';if(count) out<<static_cast<double>(sum)/count;
}
void psnr(std::ostream& out,Sum sse,Count count) {
    out<<',';if(count) out<<(sse?10*std::log10(65025.0*count/sse):INFINITY);
}

struct Stats {
    Count samples=0,bitZero=0,bitOne=0,localIncrease=0,localDecrease=0,localUnchanged=0;
    Count valid=0,invalid=0,increase=0,decrease=0,unchanged=0,pixelUnchanged=0;
    Sum localBaseline=0,localDelta=0,localIncreaseSse=0,localReductionSse=0;
    Sum accumulatedBaseline=0,accumulatedDelta=0;
    Sum finalBaseline=0,finalDelta=0,increaseSse=0,reductionSse=0,nodeDelta=0;
    void add(const Sample& s,const Response& f) {
        ++samples;
        if((static_cast<unsigned>(s.baselineOutput)>>f.bit)&1u) ++bitOne;else ++bitZero;
        localBaseline+=square(s.localError);localDelta+=f.localSquaredErrorDelta;
        accumulatedBaseline+=square(s.accumulatedError);accumulatedDelta+=f.accumulatedSquaredErrorDelta;
        if(f.localSquaredErrorDelta>0) {++localIncrease;localIncreaseSse+=f.localSquaredErrorDelta;}
        else if(f.localSquaredErrorDelta<0) {++localDecrease;localReductionSse-=f.localSquaredErrorDelta;}
        else ++localUnchanged;
        nodeDelta+=f.nodeDelta;
        if(!f.valid) {++invalid;return;}
        ++valid;finalBaseline+=square(s.baselinePixel-s.referencePixel);
        finalDelta+=f.finalSquaredErrorDelta;
        pixelUnchanged+=f.finalPixel==s.baselinePixel;
        if(f.finalSquaredErrorDelta>0) {++increase;increaseSse+=f.finalSquaredErrorDelta;}
        else if(f.finalSquaredErrorDelta<0) {++decrease;reductionSse-=f.finalSquaredErrorDelta;}
        else ++unchanged;
    }
    static void header(std::ostream& out) {
        out<<"samples,bit_zero,bit_one,local_baseline_sse,local_delta_sse,local_increase_count,"
            "local_decrease_count,local_unchanged_count,local_increase_sse,local_reduction_sse,"
            "accumulated_baseline_sse,accumulated_delta_sse,"
            "valid_final_samples,invalid_downstream,baseline_final_sse_valid,final_delta_sse,"
            "final_increase_count,final_decrease_count,final_unchanged_count,final_increase_sse,"
            "final_reduction_sse,pixel_unchanged_count,mean_local_delta,mean_final_delta_valid,"
            "final_increase_rate_valid,final_decrease_rate_valid,mean_node_delta,final_valid_rate\n";
    }
    void write(std::ostream& out) const {
        out<<samples<<','<<bitZero<<','<<bitOne<<','<<localBaseline<<','<<localDelta<<','
           <<localIncrease<<','<<localDecrease<<','<<localUnchanged<<','<<localIncreaseSse<<','
           <<localReductionSse<<','<<accumulatedBaseline<<','
           <<accumulatedDelta<<','<<valid<<','<<invalid<<','<<finalBaseline<<','<<finalDelta<<','
           <<increase<<','<<decrease<<','<<unchanged<<','<<increaseSse<<','<<reductionSse<<','<<pixelUnchanged;
        mean(out,localDelta,samples);mean(out,finalDelta,valid);
        mean(out,increase,valid);mean(out,decrease,valid);mean(out,nodeDelta,samples);
        mean(out,valid,samples);out<<'\n';
    }
};

void sampleHeader(std::ostream& out) {
    out<<"image,row,col,node_id,node_name,region,input1,input2,baseline_output,"
        "exact_same_input_output,exact_path_output,local_error,accumulated_error,"
        "baseline_final,reference_final,baseline_final_error\n";
}
void sampleRow(std::ostream& out,int image,const Sample& s) {
    out<<image<<','<<s.row<<','<<s.col<<','<<s.nodeId<<','
       <<applications::SharpenApplication::nodeName(s.nodeId)<<','<<region(s.roi)<<','
       <<s.input1<<','<<s.input2<<','<<s.baselineOutput<<','<<s.exactSameInputOutput<<','
       <<s.exactPathOutput<<','<<s.localError<<','<<s.accumulatedError<<','<<s.baselinePixel<<','
       <<s.referencePixel<<','<<s.baselinePixel-s.referencePixel<<'\n';
}
void responseHeader(std::ostream& out) {
    out<<"image,row,col,node_id,region,bit,flipped_output,node_delta,local_error_after,local_delta_sse,"
        "accumulated_error_after,accumulated_delta_sse,valid,final_pixel,final_error,final_delta_sse\n";
}
void responseRow(std::ostream& out,int image,const Sample& s,const Response& f) {
    out<<image<<','<<s.row<<','<<s.col<<','<<s.nodeId<<','<<region(s.roi)<<','<<f.bit<<','
       <<f.flippedOutput<<','<<f.nodeDelta<<','<<f.localErrorAfter<<','<<f.localSquaredErrorDelta<<','
       <<f.accumulatedErrorAfter<<','<<f.accumulatedSquaredErrorDelta<<','<<f.valid<<',';
    if(f.valid) out<<f.finalPixel<<','<<f.finalPixel-s.referencePixel<<','<<f.finalSquaredErrorDelta;
    else out<<",,";
    out<<'\n';
}
int integer(const std::string& s) {
    std::size_t end=0;const int n=std::stoi(s,&end);
    if(end!=s.size()) throw std::runtime_error("Image index must be an integer.");return n;
}
}

int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::runtime_error(
            "Usage: sharpen_bitflip_diagnostics DATA_DIR OUTPUT_DIR [first=1] [last=first] [--responses]");
        const fs::path data=argv[1];
        fs::path output=fs::absolute(argv[2]);
        // A trailing '/' or '/.' otherwise makes parent creation claim the target.
        while(output.has_relative_path() &&
                (output.filename().empty() || output.filename()=="."))
            output=output.parent_path();
        bool details=false;std::vector<int> indices;
        for(int i=3;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--responses" && !details) details=true;
            else indices.push_back(integer(arg));
        }
        const int first=indices.empty()?1:indices[0];
        const int last=indices.size()<2?first:indices[1];
        if(indices.size()>2 || first<1 || last>30 || first>last)
            throw std::runtime_error("Use an inclusive image range within 1..30.");
        if(!output.parent_path().empty()) fs::create_directories(output.parent_path());
        // Only the process that atomically creates the final directory owns it.
        if(!fs::create_directory(output))
            throw std::runtime_error("Output directory already exists; use a new directory.");
        {
            auto notes=open(output/"RUN.txt");
            notes<<"completed=0\nschema_version=1\nbaseline=all_5RP\napplication=Sharpening\noutput_width=13\nbits=0..11\n"
                 <<"data="<<fs::absolute(data).string()<<"\nroi="<<fs::absolute(data/"mask/roi_mask.jpg").string()
                 <<"\nfirst_image="<<first<<"\nlast_image="<<last<<"\nresponses="<<details<<'\n'
                 <<"Coordinates are zero-based. ROI uses nearest-neighbor resize and threshold >=128.\n"
                 <<"Node records/probes exclude borders; image metrics include borders.\n"
                 <<"Probes are independent from the unchanged baseline, not jointly applied.\n"
                 <<"Local reference: exact sum on baseline inputs. Accumulated reference: exact-path node.\n"
                 <<"Final reference: clipped exact Sharpening output. Invalid final fields/empty means are blank.\n"
                 <<"Local counts include all probes; final means/rates use valid_final_samples only.\n"
                 <<"Groups use ORIGINAL baseline monitor values. Net deltas are signed; reduction_sse is positive.\n"
                 <<"No interval selection, no global quality filtering, no claim of joint-node feasibility.\n"
                 <<"Existing development images reused; no synthesis or new-data generalization evidence.\n";
            notes.close();
        }
        auto samples=open(output/"baseline_samples.csv");sampleHeader(samples);
        auto summary=open(output/"bit_summary.csv");
        summary<<"image,node_id,node_name,bit,region,";Stats::header(summary);
        auto grouped=open(output/"monitor_groups.csv");
        grouped<<"image,node_id,node_name,bit,region,monitor,monitor_value,";Stats::header(grouped);
        auto images=open(output/"image_summary.csv");
        images<<"image,rows,cols,interior_pixels,roi_pixels,non_roi_pixels,roi_sse,non_roi_sse,"
            "baseline_global_mse,baseline_roi_mse,baseline_non_roi_mse,baseline_global_psnr,"
            "baseline_roi_psnr,baseline_non_roi_psnr\n";
        std::ofstream responses;
        if(details) {responses=open(output/"bit_responses.csv");responseHeader(responses);}
        const auto shared=image_io::loadGrayImage((data/"mask/roi_mask.jpg").string());
        const applications::SharpenApplication app;
        for(int image=first;image<=last;++image) {
            const int stage=(image-1)/10+1;
            const auto path=data/("stage"+std::to_string(stage))/"input"/("image_"+two(image)+".jpg");
            const auto input=image_io::loadGrayImage(path.string());
            const auto mask=region_mask::resizeMaskToImage(shared,input);
            const auto baseline=analysis::collectSharpenBaseline(app,input,mask);
            for(const auto& s:baseline.samples) sampleRow(samples,image,s);
            std::array<Stats,96> totals;
            std::array<std::map<int,Stats>,288> groups;
            analysis::visitSharpenBitflips(baseline,[&](const Sample& s,const Response& f) {
                const int key=(s.nodeId*12+f.bit)*2+int(s.roi);
                totals[key].add(s,f);
                for(int signal=0;signal<3;++signal)
                    groups[key*3+signal][monitorValue(s,signal)].add(s,f);
                if(details) responseRow(responses,image,s,f);
            });
            for(int node=0;node<4;++node) for(int bit=0;bit<12;++bit) for(int roi=0;roi<2;++roi) {
                const int key=(node*12+bit)*2+roi;
                const auto prefix=std::to_string(image)+','+std::to_string(node)+','+
                    applications::SharpenApplication::nodeName(node)+','+std::to_string(bit)+','+region(roi)+',';
                summary<<prefix;totals[key].write(summary);
                for(int signal=0;signal<3;++signal) for(const auto& [value,stats]:groups[key*3+signal]) {
                    grouped<<prefix<<monitor(signal)<<','<<value<<',';stats.write(grouped);
                }
            }
            images<<image<<','<<baseline.rows<<','<<baseline.cols<<','<<baseline.samples.size()/4<<','
                  <<baseline.roiPixels<<','<<baseline.nonRoiPixels<<','<<baseline.roiSquaredError<<','
                  <<baseline.nonRoiSquaredError;
            const auto sse=baseline.roiSquaredError+baseline.nonRoiSquaredError;
            const auto count=baseline.roiPixels+baseline.nonRoiPixels;
            mean(images,sse,count);mean(images,baseline.roiSquaredError,baseline.roiPixels);
            mean(images,baseline.nonRoiSquaredError,baseline.nonRoiPixels);
            psnr(images,sse,count);psnr(images,baseline.roiSquaredError,baseline.roiPixels);
            psnr(images,baseline.nonRoiSquaredError,baseline.nonRoiPixels);images<<'\n';
            samples.flush();summary.flush();grouped.flush();images.flush();if(details) responses.flush();
            std::cout<<"Image "<<image<<": "<<baseline.samples.size()<<" baseline records, "
                     <<baseline.samples.size()*12<<" independent probes\n"<<std::flush;
        }
        samples.close();summary.close();grouped.close();images.close();if(details) responses.close();
        auto notes=open(output/"RUN.txt",std::ios::out|std::ios::app);
        notes<<"completed=1\n";notes.close();
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <stdexcept>
#include "../app/circuit_profile.hpp"
namespace fs = std::filesystem;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need verifier path");
        const fs::path dir = fs::temp_directory_path() / "circuit_profile_cli_test";
        fs::create_directories(dir);
        cv::Mat input(1, 2, CV_8UC1); input.at<unsigned char>(0,0)=10; input.at<unsigned char>(0,1)=20;
        cv::imwrite((dir/"input.png").string(), input);
        auto run = [&](const std::string& app, int node, int bit, const std::string& extra="") {
            const std::string command = "\"" + std::string(argv[1]) + "\" " + app + " \"" + (dir/"input.png").string() + "\" \"" + dir.string() + "\" 0 0 1 1 " + std::to_string(node) + " " + std::to_string(bit) + extra + " --circuit \"" + (dir/"profile.json").string() + "\"";
            return std::system(command.c_str());
        };
        auto profile = [&](const std::string& json) { std::ofstream out(dir/"profile.json"); out << json; };
        profile(R"({"schema":1,"name":"exact-sharpen","application":"sharpen","baseline":[{"node":0,"unit":"exact"},{"node":1,"unit":"exact"},{"node":2,"unit":"exact"},{"node":3,"unit":"exact"}]})");
        require(run("sharpen",0,0)==0, "Exact built-in profile and exact-node flip must succeed");
        auto exact = cv::imread((dir/"exact.png").string(),0), base = cv::imread((dir/"baseline.png").string(),0);
        require(cv::countNonZero(exact != base)==0, "Exact baseline must match exact reference");
        profile(R"({"schema":1,"name":"graph","application":"graph","baseline":[{"node":0,"unit":"exact"}],"graph":{"border":"reflect101","inputs":{"p":{"dx":1,"dy":0,"scale":1,"bias":0}},"operations":[{"id":"0","op":"add","args":["p",2]}],"output":"0"}})");
        require(run("graph",0,0)==0, "Graph profile must execute");
        base = cv::imread((dir/"baseline.png").string(),0);
        auto processed = cv::imread((dir/"processed.png").string(),0);
        require(base.at<unsigned char>(0,0)==22 && base.at<unsigned char>(0,1)==12, "reflect101 and arithmetic must match hand-derived values");
        require(processed.at<unsigned char>(0,0)==23 && processed.at<unsigned char>(0,1)==12, "Only ROI arithmetic output must flip");
        require(run("graph",0,0," 0")!=0, "Originally exact nodes cannot be replaced");
        profile(R"({"schema":1,"name":"defaults","application":"graph","baseline":[{"node":0,"unit":"exact"}],"graph":{"border":"reflect101","inputs":{"p":{}},"operations":[{"id":"0","op":"add","args":["p",2]}],"output":"0"}})");
        require(run("graph",0,0)==0, "Missing input attributes must use default sampling");
        base = cv::imread((dir/"baseline.png").string(),0);
        require(base.at<unsigned char>(0,0)==12 && base.at<unsigned char>(0,1)==22, "Default input must sample original pixel");
        profile(R"({"schema":1,"name":"ops","application":"graph","baseline":[{"node":0,"unit":"exact"}],"graph":{"border":"replicate","inputs":{"p":{"dx":0,"dy":0,"scale":1,"bias":-30}},"operations":[{"id":"0","op":"sub","args":["p",-2]},{"id":"m","op":"abs","args":["0"]},{"id":"s","op":"scale","factor":2,"args":["m"]},{"id":"r","op":"shift","bits":2,"args":["s"]},{"id":"c","op":"clamp","min":0,"max":8,"args":["r"]}],"output":"c"}})");
        require(run("graph",0,0)==0, "Unary graph operations must execute");
        base = cv::imread((dir/"baseline.png").string(),0);
        require(base.at<unsigned char>(0,0)==8 && base.at<unsigned char>(0,1)==4, "sub/abs/scale/shift/clamp must match manual result");
        for (const auto& name : {"5RP","5QC","5L8","5QT","5TE","5PD","5PN","5SB","5Z0"}) {
            profile(std::string(R"({"schema":1,"name":"mixed","application":"graph","baseline":[{"node":0,"unit":")")+name+R"("}],"graph":{"border":"replicate","inputs":{"p":{"dx":0,"dy":0,"scale":1,"bias":0}},"operations":[{"id":"0","op":"add","args":["p",2]}],"output":"0"}})");
            require(run("graph",0,0)==0, "Every mix9 unit must execute");
            base = cv::imread((dir/"baseline.png").string(),0);
            const int want = std::clamp(approximate::addSigned12(10,2,circuit_profile::unit(name).unit),0,255);
            require(base.at<unsigned char>(0,0)==want, "Baseline must use selected arithmetic unit");
            require(run("graph",0,0," 0")==0, "Approximate node must support exact replacement");
            const auto replaced = cv::imread((dir/"compensated.png").string(),0);
            processed = cv::imread((dir/"processed.png").string(),0);
            require(replaced.at<unsigned char>(0,0)==12 && processed.at<unsigned char>(0,0)==13,
                    "Replaced selected node must flip compensated exact output");
        }
        profile(R"({"schema":1,"name":"width","application":"graph","baseline":[{"node":0,"unit":"exact"}],"graph":{"border":"replicate","inputs":{"p":{"dx":0,"dy":0,"scale":1,"bias":5000}},"operations":[{"id":"0","op":"add","args":["p",2]}],"output":"0"}})");
        require(run("graph",0,0)!=0, "Bitflip must reject values outside signed13");
        require(run("graph",-1,0)==0, "Output fallback may clip wider graph values");
        profile(R"({"schema":1,"name":"badunit","application":"sharpen","baseline":[{"node":0,"unit":"5XX"},{"node":1,"unit":"exact"},{"node":2,"unit":"exact"},{"node":3,"unit":"exact"}]})");
        require(run("sharpen",0,0)!=0, "Unknown units must fail");
        profile(R"({"schema":1,"name":"duplicate","application":"sharpen","baseline":[{"node":0,"unit":"exact"},{"node":0,"unit":"exact"},{"node":2,"unit":"exact"},{"node":3,"unit":"exact"}]})");
        require(run("sharpen",0,0)!=0, "Duplicate and missing baseline IDs must fail");
        profile(R"({"schema":1,"name":"bad","application":"graph","baseline":[{"node":0,"unit":"exact"}],"graph":{"border":"replicate","inputs":{},"operations":[{"id":"0","op":"add","args":["later",2]}],"output":"0"}})");
        require(run("graph",0,0)!=0, "Forward references must be rejected");
        fs::remove_all(dir);
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

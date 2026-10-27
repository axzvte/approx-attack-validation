#include "io/image_io.hpp"
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) { bool failed=false; try { f(); } catch(const std::exception&) { failed=true; } require(failed,"Invalid ROI accepted"); }
int main(int argc, char** argv) {
    try {
        require(argc==2,"Supply a fresh fixture directory");
        const fs::path root=argv[1]; require(!fs::exists(root),"Fixture already exists"); fs::create_directories(root);
        const cv::Mat input(6,8,CV_8UC1,cv::Scalar(120));
        cv::Mat mask=cv::Mat::zeros(input.size(),CV_8UC1); mask(cv::Rect(0,0,4,6)).setTo(255);
        const auto file=root/"image_01_mask.png"; cv::imwrite(file.string(),mask);
        require(image_io::resolveRoiMaskPath(root.string(),1)==file.string(),"Wrong pair");
        require(cv::countNonZero(image_io::loadBinaryRoiMask(file.string(),input))==24,"Wrong mask");
        mask/=255; cv::imwrite(file.string(),mask);
        require(image_io::loadBinaryRoiMask(file.string(),input).at<unsigned char>(0,0)==255,"0/1 not normalized");
        rejects([&]{ image_io::resolveRoiMaskPath(root.string(),2); });
        rejects([&]{ image_io::loadBinaryRoiMask(file.string(),cv::Mat(3,4,CV_8UC1)); });
        for(int value : {0,255}) { mask.setTo(value); cv::imwrite(file.string(),mask); rejects([&]{ image_io::loadBinaryRoiMask(file.string(),input); }); }
        mask.setTo(0); mask.at<unsigned char>(0,0)=127; cv::imwrite(file.string(),mask);
        rejects([&]{ image_io::loadBinaryRoiMask(file.string(),input); });
        mask.at<unsigned char>(0,0)=1; mask.at<unsigned char>(0,1)=255; cv::imwrite(file.string(),mask);
        rejects([&]{ image_io::loadBinaryRoiMask(file.string(),input); });
        cv::imwrite(file.string(),cv::Mat(6,8,CV_8UC3,cv::Scalar(0,0,0)));
        rejects([&]{ image_io::loadBinaryRoiMask(file.string(),input); });
        cv::imwrite(file.string(),cv::Mat(6,8,CV_16UC1,cv::Scalar(1)));
        rejects([&]{ image_io::loadBinaryRoiMask(file.string(),input); });
        fs::remove_all(root); std::cout<<"image ROI checks passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

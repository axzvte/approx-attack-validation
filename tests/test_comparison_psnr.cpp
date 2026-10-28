// Rendering-only test: no image dataset, model, training or interval search.
#include "io/bitflip_comparison.hpp"
#include <iostream>
#include <limits>

int main() {
    std::array<cv::Mat,3> frames{cv::Mat(5,7,CV_8UC1,cv::Scalar(10)),
        cv::Mat(5,7,CV_8UC1,cv::Scalar(20)),cv::Mat(5,7,CV_8UC1,cv::Scalar(30))};
    for (int i=0;i<3;++i) for(int y=0;y<5;++y) for(int x=0;x<7;++x)
        frames[i].at<unsigned char>(y,x)=static_cast<unsigned char>((i*70+y*11+x*5)%256);
    const double inf=std::numeric_limits<double>::infinity();
    const std::array<image_io::ComparisonPsnr,3> quality{{{inf,inf},{31.23456,36.12345},{29.12345,20.98765}}};
    const auto lines=image_io::comparisonLabelLines(quality,"quality_not_met");
    if(lines[0][1]!="Global PSNR: inf dB" || lines[1][1]!="Global PSNR: 31.235 dB" ||
       lines[1][2]!="ROI PSNR: 36.123 dB" || lines[2][2]!="ROI PSNR: 20.988 dB" ||
       lines[2][3]!="quality_not_met") return 1;
    const auto result=image_io::bitflipComparisonPanel(frames,quality,"quality_not_met");
    if(result.rows!=frames[0].rows+image_io::comparisonHeaderHeight || result.cols<720) return 2;
    for(int i=0;i<3;++i) {
        const int width=result.cols/3;
        const auto pixels=result(cv::Rect(i*width+(width-7)/2,image_io::comparisonHeaderHeight,7,5));
        if(cv::countNonZero(pixels!=frames[i])) return 3;
        const auto header=result(cv::Rect(i*width,0,width,image_io::comparisonHeaderHeight));
        if(!cv::countNonZero(header)) return 4;
    }
    bool invalid=false;
    try {image_io::comparisonPsnrText(std::numeric_limits<double>::quiet_NaN());}
    catch(const std::invalid_argument&) {invalid=true;}
    if(!invalid) return 5;
    std::cout << "Absolute PSNR labels and unchanged narrow-image pixels verified\n";
    return 0;
}

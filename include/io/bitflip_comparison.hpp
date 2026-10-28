#pragma once

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace image_io {
struct ComparisonPsnr { double global, roi; };
inline constexpr int comparisonHeaderHeight = 84;

inline std::string comparisonPsnrText(double value) {
    if (std::isinf(value) && value > 0) return "inf";
    if (!std::isfinite(value)) throw std::invalid_argument("Invalid comparison PSNR");
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

inline std::array<std::array<std::string,4>,3> comparisonLabelLines(
    const std::array<ComparisonPsnr,3>& quality, const std::string& status) {
    std::array<std::array<std::string,4>,3> lines;
    const std::array<std::string,3> titles{"exact", "baseline", "fixed H + intervals"};
    for (int i=0;i<3;++i) lines[i] = {titles[i],
        "Global PSNR: " + comparisonPsnrText(quality[i].global) + " dB",
        "ROI PSNR: " + comparisonPsnrText(quality[i].roi) + " dB",
        i==2 ? status : "ROI: original mask"};
    return lines;
}

// Quality always describes the uncropped outputs and original ROI mask.
// Only padding is added: pixels are neither scaled nor written over by text.
inline cv::Mat bitflipComparisonPanel(const std::array<cv::Mat,3>& images,
    const std::array<ComparisonPsnr,3>& quality, const std::string& status) {
    const auto lines = comparisonLabelLines(quality,status);
    int width=240;
    for (int i=0;i<3;++i) {
        if (images[i].empty() || images[i].type()!=CV_8UC1 || images[i].size()!=images[0].size())
            throw std::invalid_argument("Comparison needs three equal grayscale images");
        width=std::max(width,images[i].cols);
        for (const auto& text:lines[i])
            width=std::max(width,cv::getTextSize(text,cv::FONT_HERSHEY_SIMPLEX,.43,1,nullptr).width+16);
    }
    cv::Mat output(images[0].rows+comparisonHeaderHeight,3*width,CV_8UC1,cv::Scalar(0));
    for (int i=0;i<3;++i) {
        images[i].copyTo(output(cv::Rect(i*width+(width-images[i].cols)/2,
            comparisonHeaderHeight,images[i].cols,images[i].rows)));
        for (int row=0;row<4;++row)
            cv::putText(output,lines[i][row],cv::Point(i*width+8,17+18*row),
                cv::FONT_HERSHEY_SIMPLEX,.43,cv::Scalar(255),1,cv::LINE_AA);
    }
    return output;
}
} // namespace image_io

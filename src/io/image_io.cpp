#include "io/image_io.hpp"

#include <opencv2/imgcodecs.hpp>
#include <string>
#include <stdexcept>
#include <filesystem>

namespace image_io
{
std::string resolveStagedImagePath(const std::string& dataDir,int imageIndex)
{
    namespace fs=std::filesystem;
    if(imageIndex<1 || imageIndex>40) throw std::runtime_error("Image index must stay in1..40.");
    const auto stage=fs::path(dataDir)/("stage"+std::to_string((imageIndex-1)/10+1));
    const auto filename=std::string("image_")+(imageIndex<10?"0":"")+std::to_string(imageIndex)+".jpg";
    const auto nested=stage/"input"/filename,flat=stage/filename;
    if(fs::is_regular_file(nested)) return nested.string();
    if(fs::is_regular_file(flat)) return flat.string();
    throw std::runtime_error("Missing input image; checked: "+nested.string()+" and "+flat.string());
}

cv::Mat loadGrayImage(const std::string& path)
{
    cv::Mat image = cv::imread(path, cv::IMREAD_GRAYSCALE);
    if (image.empty())
    {
        throw std::runtime_error("Failed to load grayscale image from path: " + path);
    }
    return image;
}

cv::Mat loadColorImage(const std::string& path)
{
    cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
    if (image.empty())
    {
        throw std::runtime_error("Failed to load color image from path: " + path);
    }
    return image;
}

void saveImage(
    const std::string& path,
    const cv::Mat& image
)
{
    const bool success =
        cv::imwrite(
            path,
            image
        );

    if (!success)
    {
        throw std::runtime_error(
            "Failed to save image: " + path
        );
    }
}

} // namespace image_io

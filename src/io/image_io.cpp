#include "io/image_io.hpp"

#include <opencv2/imgcodecs.hpp>
#include <string>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <array>

namespace image_io
{
std::string resolveRoiMaskPath(const std::string& roiDir,int imageIndex)
{
    if(imageIndex<1 || imageIndex>40) throw std::runtime_error("ROI image index must stay in1..40.");
    const auto filename=std::string("image_")+(imageIndex<10?"0":"")+std::to_string(imageIndex)+"_mask.png";
    const auto path=std::filesystem::path(roiDir)/filename;
    if(!std::filesystem::is_regular_file(path))
        throw std::runtime_error("Missing per-image ROI (no shared-mask fallback): "+path.string());
    return path.string();
}

cv::Mat loadBinaryRoiMask(const std::string& path,const cv::Mat& image)
{
    // Check the container as well as decoded pixels: JPEG masks may introduce halos.
    const std::array<unsigned char,8> png{{137,80,78,71,13,10,26,10}};
    std::array<unsigned char,8> signature{};
    std::ifstream file(path,std::ios::binary);
    file.read(reinterpret_cast<char*>(signature.data()),signature.size());
    if(!file || signature!=png) throw std::runtime_error("ROI must be a lossless PNG: "+path);
    cv::Mat mask=cv::imread(path,cv::IMREAD_UNCHANGED);
    if(mask.empty() || mask.type()!=CV_8UC1)
        throw std::runtime_error("ROI must be a single-channel 8-bit/1-bit PNG: "+path);
    if(image.empty() || image.type()!=CV_8UC1 || mask.size()!=image.size())
        throw std::runtime_error("Image/ROI size or input type mismatch (no automatic resize): "+path);
    std::size_t count=0; bool ones=false,full=false;
    for(int y=0;y<mask.rows;++y) for(int x=0;x<mask.cols;++x) {
        auto& value=mask.at<unsigned char>(y,x);
        if(value!=0 && value!=1 && value!=255) throw std::runtime_error("ROI is not binary 0/1 or 0/255: "+path);
        ones|=value==1; full|=value==255;
        if(value) { ++count; value=255; }
    }
    if(ones && full) throw std::runtime_error("ROI mixes 0/1 and 0/255 encodings: "+path);
    if(count==0 || count==mask.total()) throw std::runtime_error("ROI and background must both be nonempty: "+path);
    return mask;
}

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

cv::Mat loadGrayImage(const std::string& path,bool ignoreExifOrientation)
{
    const int flags=cv::IMREAD_GRAYSCALE | (ignoreExifOrientation ? cv::IMREAD_IGNORE_ORIENTATION : 0);
    cv::Mat image = cv::imread(path, flags);
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

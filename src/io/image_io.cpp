#include "io/image_io.hpp"

#include <opencv2/imgcodecs.hpp>
#include <string>
#include <stdexcept>

namespace image_io
{
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
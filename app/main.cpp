#include "io/image_io.hpp"
#include "processing/exact_sharpen.hpp"

#include <exception>
#include <iostream>
#include <string>

int main()
{
    const std::string inputImagePath =
        "data/input/test_1.jpg";

    const std::string outputImagePath =
        "results/test_1_exact_sharpen.png";

    try
    {
        const cv::Mat inputImage =
            image_io::loadGrayImage(
                inputImagePath
            );

        const cv::Mat outputImage =
            image_processing::sharpenExact(
                inputImage
            );

        image_io::saveImage(
            outputImagePath,
            outputImage
        );

        std::cout
            << "Saved: "
            << outputImagePath
            << std::endl;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "Error: "
            << error.what()
            << std::endl;

        return 1;
    }

    return 0;
}

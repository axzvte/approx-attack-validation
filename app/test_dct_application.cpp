#include "applications/dct.hpp"
#include "io/image_io.hpp"
#include "metrics/psnr.hpp"

#include <iostream>


int main()
{
    try
    {
        const cv::Mat inputImage =
            image_io::loadGrayImage(
                "data/input/test_1.jpg"
            );


        applications::DctApplication
            dctApplication;


        const cv::Mat reconstructedImage =
            dctApplication.runExact(
                inputImage
            );


        const double psnr =
            metrics::calculateGlobalPSNR(
                inputImage,
                reconstructedImage
            );


        std::cout
            << "Application: "
            << dctApplication.name()
            << "\n";


        std::cout
            << "Input size : "
            << inputImage.cols
            << " x "
            << inputImage.rows
            << "\n";


        std::cout
            << "Reconstruction PSNR: "
            << psnr
            << " dB\n";


        return 0;
    }


    catch (const std::exception& error)
    {
        std::cerr
            << "Error: "
            << error.what()
            << "\n";


        return 1;
    }
}#include "applications/dct.hpp"
#include "io/image_io.hpp"
#include "metrics/psnr.hpp"

#include <iostream>


int main()
{
    try
    {
        const cv::Mat inputImage =
            image_io::loadGrayImage(
                "data/input/test_1.jpg"
            );


        applications::DctApplication
            dctApplication;


        const cv::Mat reconstructedImage =
            dctApplication.runExact(
                inputImage
            );


        const double psnr =
            metrics::calculateGlobalPSNR(
                inputImage,
                reconstructedImage
            );


        std::cout
            << "Application: "
            << dctApplication.name()
            << "\n";


        std::cout
            << "Input size : "
            << inputImage.cols
            << " x "
            << inputImage.rows
            << "\n";


        std::cout
            << "Reconstruction PSNR: "
            << psnr
            << " dB\n";


        return 0;
    }


    catch (const std::exception& error)
    {
        std::cerr
            << "Error: "
            << error.what()
            << "\n";


        return 1;
    }
}
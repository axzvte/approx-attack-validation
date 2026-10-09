#include "analysis/dct_interval_full_validator.hpp"

#include "applications/sobel.hpp"
#include "io/image_io.hpp"
#include "region/region_mask.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>


namespace
{

std::string twoDigit(
    int value
)
{
    std::ostringstream stream;

    stream
        << std::setw(2)
        << std::setfill('0')
        << value;

    return stream.str();
}

}


int main(
    int argc,
    char** argv
)
{
    try
    {
        const std::filesystem::path dataRoot =
            argc >= 2
            ?
            std::filesystem::path(
                argv[1]
            )
            :
            std::filesystem::path(
                "data"
            );


        applications::SobelApplication
            application;


        const cv::Mat sharedRoiMask =
            image_io::loadGrayImage(
                (
                    dataRoot
                    /
                    "mask"
                    /
                    "roi_mask.jpg"
                ).string()
            );


        double globalSum = 0.0;
        double roiSum = 0.0;
        double nonRoiSum = 0.0;


        std::cout
            << "Sobel selected baseline validation\n"
            << "=====================================\n"
            << "Baseline: nodes 0,2,4,7,9 = 5RP; others exact\n"
            << "Reference: exact Sobel output\n\n"
            << std::left
            << std::setw(10) << "Image"
            << std::setw(16) << "Global"
            << std::setw(16) << "ROI"
            << "Non-ROI\n"
            << std::fixed
            << std::setprecision(
                6
            );


        for (int index = 1;
             index <= 10;
             ++index)
        {
            const cv::Mat inputImage =
                image_io::loadGrayImage(
                    (
                        dataRoot
                        /
                        "stage1"
                        /
                        "input"
                        /
                        (
                            "image_"
                            +
                            twoDigit(
                                index
                            )
                            +
                            ".jpg"
                        )
                    ).string()
                );


            const cv::Mat roiMask =
                region_mask::resizeMaskToImage(
                    sharedRoiMask,
                    inputImage
                );


            const auto metrics =
                analysis::DctIntervalFullValidator::
                    evaluateConfiguration(
                        application,
                        inputImage,
                        roiMask,
                        {}
                    );


            globalSum +=
                metrics.globalPsnr;

            roiSum +=
                metrics.roiPsnr;

            nonRoiSum +=
                metrics.nonRoiPsnr;


            std::cout
                << std::left
                << std::setw(10)
                << (
                    "image_"
                    +
                    twoDigit(
                        index
                    )
                )
                << std::setw(16)
                << metrics.globalPsnr
                << std::setw(16)
                << metrics.roiPsnr
                << metrics.nonRoiPsnr
                << "\n";
        }


        std::cout
            << "\nMean\n"
            << "Global: "
            << globalSum / 10.0
            << "\n"
            << "ROI: "
            << roiSum / 10.0
            << "\n"
            << "Non-ROI: "
            << nonRoiSum / 10.0
            << "\n";


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Sobel baseline validation failed: "
            << exception.what()
            << "\n";

        return 1;
    }
}

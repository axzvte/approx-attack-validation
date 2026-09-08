#include "approximate/evoapprox_adapter.hpp"
#include "io/image_io.hpp"
#include "metrics/psnr.hpp"
#include "processing/approx_sharpen.hpp"
#include "processing/exact_sharpen.hpp"

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>


int main()
{
    const std::string inputImagePath =
        "data/input/test_1.jpg";

    const std::array<approximate::ApproxUnitId, 9> units =
    {
        approximate::ApproxUnitId::Add12se5QT,
        approximate::ApproxUnitId::Add12se5QC,
        approximate::ApproxUnitId::Add12se5TE,
        approximate::ApproxUnitId::Add12se5PN,
        approximate::ApproxUnitId::Add12se5SB,
        approximate::ApproxUnitId::Add12se5Z0,
        approximate::ApproxUnitId::Add12se5L8,
        approximate::ApproxUnitId::Add12se5PD,
        approximate::ApproxUnitId::Add12se5RP
    };

    const std::array<std::string, 9> unitNames =
    {
        "5QT",
        "5QC",
        "5TE",
        "5PN",
        "5SB",
        "5Z0",
        "5L8",
        "5PD",
        "5RP"
    };


    try
    {
        std::filesystem::create_directories(
            "results"
        );

        const cv::Mat inputImage =
            image_io::loadGrayImage(
                inputImagePath
            );

        // 精确锐化结果作为 PSNR 参考
        const cv::Mat exactImage =
            image_processing::sharpenExact(
                inputImage
            );

        std::ofstream report(
            "results/baseline_benchmark.csv"
        );

        report
            << "unit,global_psnr\n";

        std::cout
            << std::fixed
            << std::setprecision(3);


        for (std::size_t i = 0;
             i < units.size();
             ++i)
        {
            const auto unit =
                units[i];

            // A1、A2、A3、A4
            // 全部使用同一个近似加法器
            const image_processing::SharpenApproxConfig config =
            {
                unit,
                unit,
                unit,
                unit
            };

            const cv::Mat approximateImage =
                image_processing::sharpenApproximate(
                    inputImage,
                    config
                );

            const double globalPsnr =
                metrics::calculateGlobalPSNR(
                    exactImage,
                    approximateImage
                );

            std::cout
                << unitNames[i]
                << " : "
                << globalPsnr
                << " dB\n";

            report
                << unitNames[i]
                << ","
                << globalPsnr
                << "\n";
        }


        std::cout
            << "\nSaved: "
            << "results/baseline_benchmark.csv\n";

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
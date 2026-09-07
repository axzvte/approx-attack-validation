#include "analysis/interval_attack_search.hpp"

#include "io/image_io.hpp"
#include "processing/exact_sharpen.hpp"

#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>


int main()
{
    const std::string inputImagePath =
        "data/input/test_1.jpg";


    const std::string roiMaskPath =
        "data/input/test_1_mask.png";


    try
    {
        const cv::Mat inputImage =
            image_io::loadGrayImage(
                inputImagePath
            );


        const cv::Mat roiMask =
            image_io::loadGrayImage(
                roiMaskPath
            );


        const cv::Mat exactImage =
            image_processing::sharpenExact(
                inputImage
            );


        image_io::saveImage(
            "results/interval_exact.png",
            exactImage
        );


        std::cout
            << "Searching...\n";


        const auto candidates =
            interval_attack_search::
                searchBestCandidates(
                    inputImage,
                    roiMask,
                    30.0,
                    20
                );


        std::ofstream report(
            "results/interval_search.csv"
        );


        report
            << "rank,"
            << "attack_position,"
            << "monitor_signal,"
            << "unit,"
            << "lower,"
            << "upper,"
            << "global_psnr,"
            << "roi_psnr,"
            << "nonroi_psnr,"
            << "roi_mse,"
            << "nonroi_mse,"
            << "error_gap,"
            << "error_ratio,"
            << "roi_trigger_rate,"
            << "nonroi_trigger_rate\n";


        std::cout
            << std::fixed
            << std::setprecision(3);


        for (
            std::size_t i = 0;
            i < candidates.size();
            ++i
        )
        {
            const auto& candidate =
                candidates[i];


            const std::string attack =
                interval_attack_search::
                    attackPositionName(
                        candidate.attackPosition
                    );


            const std::string signal =
                interval_attack_search::
                    monitorSignalName(
                        candidate.monitorSignal
                    );


            const std::string unit =
                interval_attack_search::
                    unitName(
                        candidate.unit
                    );


            std::cout
                << "\n#"
                << i + 1
                << "\n"

                << "Attack position: "
                << attack
                << "\n"

                << "Monitor signal: "
                << signal
                << "\n"

                << "Unit: "
                << unit
                << "\n"

                << "Interval: ["
                << candidate.lower
                << ", "
                << candidate.upper
                << "]\n"

                << "Global PSNR: "
                << candidate.globalPsnr
                << " dB\n"

                << "ROI PSNR: "
                << candidate.roiPsnr
                << " dB\n"

                << "Non-ROI PSNR: "
                << candidate.nonRoiPsnr
                << " dB\n"

                << "ROI MSE: "
                << candidate.roiMse
                << "\n"

                << "Non-ROI MSE: "
                << candidate.nonRoiMse
                << "\n"

                << "Error gap: "
                << candidate.errorGap
                << "\n"

                << "Error ratio: "
                << candidate.errorRatio
                << "\n"

                << "ROI trigger rate: "
                << candidate.roiTriggerRate * 100.0
                << "%\n"

                << "Non-ROI trigger rate: "
                << candidate.nonRoiTriggerRate * 100.0
                << "%\n";


            report
                << i + 1
                << ","

                << attack
                << ","

                << signal
                << ","

                << unit
                << ","

                << candidate.lower
                << ","

                << candidate.upper
                << ","

                << candidate.globalPsnr
                << ","

                << candidate.roiPsnr
                << ","

                << candidate.nonRoiPsnr
                << ","

                << candidate.roiMse
                << ","

                << candidate.nonRoiMse
                << ","

                << candidate.errorGap
                << ","

                << candidate.errorRatio
                << ","

                << candidate.roiTriggerRate
                << ","

                << candidate.nonRoiTriggerRate
                << "\n";


            const cv::Mat attackImage =
                interval_attack_search::
                    renderAttack(
                        inputImage,
                        candidate
                    );


            const std::string outputPath =
                "results/interval_rank"
                +
                std::to_string(
                    i + 1
                )
                +
                "_"
                +
                attack
                +
                "_"
                +
                signal
                +
                "_"
                +
                unit
                +
                ".png";


            image_io::saveImage(
                outputPath,
                attackImage
            );
        }


        std::cout
            << "\nFinished.\n"
            << "Saved report: "
            << "results/interval_search.csv\n";


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
#include "analysis/dct_stage1_evaluator.hpp"

#include "io/image_io.hpp"
#include "metrics/psnr.hpp"

#include <stdexcept>
#include <utility>


namespace analysis
{

DctStage1Evaluator::DctStage1Evaluator(
    const TwoStageDataset& dataset
)
{
    TwoStageSearch::validateDataset(
        dataset
    );


    roiMask_ =
        image_io::loadGrayImage(
            dataset.roiMaskPath
        );


    inputImages_.reserve(
        dataset.stage1Images.size()
    );


    referenceImages_.reserve(
        dataset.stage1Images.size()
    );


    for (
        const auto& imageCase :
        dataset.stage1Images
    )
    {
        cv::Mat inputImage =
            image_io::loadGrayImage(
                imageCase.inputPath
            );


        if (
            inputImage.rows
                !=
                roiMask_.rows
            ||
            inputImage.cols
                !=
                roiMask_.cols
        )
        {
            throw std::runtime_error(
                "Stage 1 image size does not match the shared ROI mask."
            );
        }


        cv::Mat referenceImage =
            application_.runExact(
                inputImage
            );


        inputImages_.push_back(
            std::move(
                inputImage
            )
        );


        referenceImages_.push_back(
            std::move(
                referenceImage
            )
        );
    }
}


Stage1ImageMetrics
DctStage1Evaluator::evaluate(
    const AttackConfiguration& configuration,
    std::size_t imageIndex
) const
{
    if (
        imageIndex
        >=
        inputImages_.size()
    )
    {
        throw std::runtime_error(
            "DCT Stage 1 image index is out of range."
        );
    }


    const cv::Mat attackedImage =
        application_.runApprox(
            inputImages_[imageIndex],
            configuration
        );


    Stage1ImageMetrics
        result;


    result.globalMetric =
        metrics::calculateGlobalPSNR(
            referenceImages_[imageIndex],
            attackedImage
        );


    result.roiMetric =
        metrics::calculateImportantRegionPSNR(
            referenceImages_[imageIndex],
            attackedImage,
            roiMask_
        );


    return result;
}


std::size_t
DctStage1Evaluator::imageCount() const
{
    return
        inputImages_.size();
}

}

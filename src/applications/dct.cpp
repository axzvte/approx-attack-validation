#include "applications/dct.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <stdexcept>


namespace applications
{

DctApplication::DctApplication()
{
    // 32个ADD位置以后确定具体DCT结构后再加入。
}


std::string
DctApplication::name() const
{
    return "DCT";
}


const std::vector<core::AddNode>&
DctApplication::addNodes() const
{
    return addNodes_;
}


// =========================================================
// 精确 DCT -> IDCT
//
// 当前目的：
// 只验证完整图像的DCT处理和重建流程。
// 暂时使用OpenCV提供的精确DCT。
// =========================================================

cv::Mat DctApplication::runExact(
    const cv::Mat& inputImage
) const
{
    if (inputImage.empty())
    {
        throw std::runtime_error(
            "DCT input image is empty."
        );
    }


    if (inputImage.type() != CV_8UC1)
    {
        throw std::runtime_error(
            "DCT requires an 8-bit grayscale image."
        );
    }


    // =====================================================
    // 计算补齐后的尺寸
    //
    // DCT按照8×8 block处理。
    // 如果图像尺寸不是8的整数倍，就补到最近的8倍数。
    // =====================================================

    const int paddedRows =
        (
            (inputImage.rows + 7)
            /
            8
        )
        *
        8;


    const int paddedCols =
        (
            (inputImage.cols + 7)
            /
            8
        )
        *
        8;


    const int bottomPadding =
        paddedRows
        -
        inputImage.rows;


    const int rightPadding =
        paddedCols
        -
        inputImage.cols;


    cv::Mat paddedImage;


    cv::copyMakeBorder(
        inputImage,
        paddedImage,

        0,
        bottomPadding,

        0,
        rightPadding,

        cv::BORDER_REPLICATE
    );


    // =====================================================
    // 转成float
    //
    // OpenCV的DCT使用浮点数计算。
    // =====================================================

    cv::Mat floatImage;


    paddedImage.convertTo(
        floatImage,
        CV_32F
    );


    // =====================================================
    // level shift
    //
    // 图像像素：
    // 0 ~ 255
    //
    // 转成：
    // -128 ~ 127
    //
    // 这是图像DCT中常见的处理。
    // =====================================================

    floatImage -=
        128.0f;


    cv::Mat reconstructed =
        cv::Mat::zeros(
            floatImage.size(),
            CV_32F
        );


    // =====================================================
    // 逐个8×8 block处理
    // =====================================================

    for (int row = 0;
         row < floatImage.rows;
         row += 8)
    {
        for (int col = 0;
             col < floatImage.cols;
             col += 8)
        {
            // 当前8×8块
            cv::Rect blockRegion(
                col,
                row,
                8,
                8
            );


            cv::Mat inputBlock =
                floatImage(
                    blockRegion
                );


            cv::Mat dctBlock;


            // ---------------------------------------------
            // 精确二维DCT
            // ---------------------------------------------

            cv::dct(
                inputBlock,
                dctBlock
            );


            cv::Mat idctBlock;


            // ---------------------------------------------
            // 精确二维IDCT
            // ---------------------------------------------

            cv::idct(
                dctBlock,
                idctBlock
            );


            // 保存到重建图
            idctBlock.copyTo(
                reconstructed(
                    blockRegion
                )
            );
        }
    }


    // =====================================================
    // 恢复level shift
    // =====================================================

    reconstructed +=
        128.0f;


    // =====================================================
    // 转回8-bit图像
    //
    // convertTo会进行必要的舍入和饱和处理。
    // =====================================================

    cv::Mat reconstructed8Bit;


    reconstructed.convertTo(
        reconstructed8Bit,
        CV_8UC1
    );


    // =====================================================
    // 去掉之前补齐的边缘
    // =====================================================

    cv::Rect originalRegion(
        0,
        0,
        inputImage.cols,
        inputImage.rows
    );


    return
        reconstructed8Bit(
            originalRegion
        ).clone();
}


// =========================================================
// 近似DCT
//
// 当前阶段还没有加入具体DCT DFG和近似ADD。
// =========================================================

cv::Mat DctApplication::runApprox(
    const cv::Mat&,
    const std::vector<core::AttackConfig>&
) const
{
    throw std::runtime_error(
        "Approximate DCT is not implemented yet."
    );
}

}
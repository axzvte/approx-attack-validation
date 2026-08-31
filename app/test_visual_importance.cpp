#include <opencv2/opencv.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
    const std::string inputImagePath =
        "data/input/test_5.jpg";

    // 这里改成你自己的 U²-Net 显著程度图路径
    const std::string saliencyPath =
        "data/input/test_5_mask.png";

    const std::string gradientOutputPath =
        "results/test_5_gradientV3.png";

    const std::string importanceOutputPath =
        "results/test_5_visual_importanceV3.png";

    try
    {
        // 1. 读取原图，并转成灰度图
        cv::Mat inputImage =
            cv::imread(inputImagePath, cv::IMREAD_GRAYSCALE);

        cv::Mat saliencyMap =
            cv::imread(saliencyPath, cv::IMREAD_GRAYSCALE);

        if (inputImage.empty() || saliencyMap.empty())
        {
            throw std::runtime_error(
                "Input image or saliency map cannot be loaded."
            );
        }

        if (inputImage.size() != saliencyMap.size())
        {
            throw std::runtime_error(
                "Input image and saliency map sizes do not match."
            );
        }


        // 2. 计算水平方向和竖直方向的局部变化
        cv::Mat gradientX;
        cv::Mat gradientY;

        cv::Sobel(
            inputImage,
            gradientX,
            CV_32F,
            1,
            0,
            3
        );

        cv::Sobel(
            inputImage,
            gradientY,
            CV_32F,
            0,
            1,
            3
        );


        // 3. 合并两个方向，得到局部结构强度
        cv::Mat gradientMagnitude;

        cv::magnitude(
            gradientX,
            gradientY,
            gradientMagnitude
        );


        // 4. 把梯度归一化到 0~1
        cv::Mat gradientNormalized;

        cv::normalize(
            gradientMagnitude,
            gradientNormalized,
            0.0,
            1.0,
            cv::NORM_MINMAX
        );


        // 5. 把 U²-Net 显著图转换成 0~1
        cv::Mat saliencyNormalized;

        saliencyMap.convertTo(
            saliencyNormalized,
            CV_32F,
            1.0 / 255.0
        );


        // 6. 核心：
        // visualImportance = saliency × local structure
        cv::Mat visualImportance =
            saliencyNormalized.mul(
                gradientNormalized
            );


        // 7. 转成 0~255，方便保存和观察
        cv::Mat gradientOutput;
        cv::Mat importanceOutput;

        gradientNormalized.convertTo(
            gradientOutput,
            CV_8U,
            255.0
        );

        visualImportance.convertTo(
            importanceOutput,
            CV_8U,
            255.0
        );


        // 8. 保存
        cv::imwrite(
            gradientOutputPath,
            gradientOutput
        );

        cv::imwrite(
            importanceOutputPath,
            importanceOutput
        );

        std::cout
            << "Gradient image saved to: "
            << gradientOutputPath
            << std::endl;

        std::cout
            << "Visual importance image saved to: "
            << importanceOutputPath
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
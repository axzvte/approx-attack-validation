#include <opencv2/opencv.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>


// ============================================================
// 计算边缘方向丰富程度
// ============================================================

double calculateDirectionRichness(
    const cv::Mat& edgeMap,
    const cv::Mat& orientation
)
{
    constexpr int BIN_COUNT = 8;

    std::array<int, BIN_COUNT> histogram{};

    int totalEdgePixels = 0;

    for (int row = 0; row < edgeMap.rows; ++row)
    {
        for (int col = 0; col < edgeMap.cols; ++col)
        {
            if (edgeMap.at<unsigned char>(row, col) == 0)
            {
                continue;
            }

            float angle =
                orientation.at<float>(row, col);

            if (angle >= 180.0F)
            {
                angle -= 180.0F;
            }

            const float binWidth =
                180.0F / BIN_COUNT;

            int bin =
                static_cast<int>(
                    angle / binWidth
                );

            if (bin >= BIN_COUNT)
            {
                bin = BIN_COUNT - 1;
            }

            ++histogram[bin];
            ++totalEdgePixels;
        }
    }

    if (totalEdgePixels == 0)
    {
        return 0.0;
    }

    double entropy = 0.0;

    for (int count : histogram)
    {
        if (count == 0)
        {
            continue;
        }

        const double probability =
            static_cast<double>(count)
            / totalEdgePixels;

        entropy -=
            probability
            * std::log(probability);
    }

    const double maxEntropy =
        std::log(
            static_cast<double>(BIN_COUNT)
        );

    return entropy / maxEntropy;
}


// ============================================================
// 判断结构是否分布在整个 ROI 中
// ============================================================

double calculateSpatialSpread(
    const cv::Mat& edgeMap
)
{
    constexpr int GRID_SIZE = 3;

    int activeCells = 0;

    for (int gridRow = 0;
         gridRow < GRID_SIZE;
         ++gridRow)
    {
        for (int gridCol = 0;
             gridCol < GRID_SIZE;
             ++gridCol)
        {
            const int x1 =
                gridCol * edgeMap.cols
                / GRID_SIZE;

            const int x2 =
                (gridCol + 1)
                * edgeMap.cols
                / GRID_SIZE;

            const int y1 =
                gridRow * edgeMap.rows
                / GRID_SIZE;

            const int y2 =
                (gridRow + 1)
                * edgeMap.rows
                / GRID_SIZE;

            const cv::Rect cell(
                x1,
                y1,
                x2 - x1,
                y2 - y1
            );

            const cv::Mat cellEdges =
                edgeMap(cell);

            const double edgeRatio =
                static_cast<double>(
                    cv::countNonZero(cellEdges)
                )
                / cell.area();

            if (edgeRatio > 0.02)
            {
                ++activeCells;
            }
        }
    }

    return
        static_cast<double>(activeCells)
        / (GRID_SIZE * GRID_SIZE);
}


// ============================================================
// 主程序
// ============================================================

int main()
{
    // --------------------------------------------------------
    // 1. 路径
    // --------------------------------------------------------

    const std::string inputImagePath =
        "/home/axzvte/projects/u2net_roi/input/test_5.jpg";
    
    const std::string saliencyPath =
        "/home/axzvte/projects/u2net_roi/output/test_5_saliency.png";

    const std::string roiMaskOutputPath =
        "results/test_5_roi_mask_v3.png";

    const std::string roiOverlayOutputPath =
        "results/test_5_roi_overlay_v3.png";


    try
    {
        // ====================================================
        // 2. 读取图像
        // ====================================================

        cv::Mat grayImage =
            cv::imread(
                inputImagePath,
                cv::IMREAD_GRAYSCALE
            );

        cv::Mat colorImage =
            cv::imread(
                inputImagePath,
                cv::IMREAD_COLOR
            );

        cv::Mat saliencyMap =
            cv::imread(
                saliencyPath,
                cv::IMREAD_GRAYSCALE
            );

        if (grayImage.empty() ||
            colorImage.empty() ||
            saliencyMap.empty())
        {
            throw std::runtime_error(
                "Failed to load image."
            );
        }

        if (grayImage.size() != saliencyMap.size())
        {
            throw std::runtime_error(
                "Image and saliency map sizes do not match."
            );
        }


        // ====================================================
        // 3. U2-Net 显著图二值化，得到主体 mask
        // ====================================================

        cv::Mat subjectMask;

        cv::threshold(
            saliencyMap,
            subjectMask,
            128,
            255,
            cv::THRESH_BINARY
        );


        // ====================================================
        // 4. 找到主体外接矩形
        // ====================================================

        std::vector<cv::Point> subjectPoints;

        cv::findNonZero(
            subjectMask,
            subjectPoints
        );

        if (subjectPoints.empty())
        {
            throw std::runtime_error(
                "No subject found."
            );
        }

        const cv::Rect subjectBox =
            cv::boundingRect(subjectPoints);


        // ====================================================
        // 5. 计算 Sobel 梯度
        // ====================================================

        cv::Mat gradientX;
        cv::Mat gradientY;

        cv::Sobel(
            grayImage,
            gradientX,
            CV_32F,
            1,
            0,
            3
        );

        cv::Sobel(
            grayImage,
            gradientY,
            CV_32F,
            0,
            1,
            3
        );


        // ====================================================
        // 6. 计算梯度强度
        // ====================================================

        cv::Mat gradientMagnitude;

        cv::magnitude(
            gradientX,
            gradientY,
            gradientMagnitude
        );

        cv::Mat gradient8U;

        cv::normalize(
            gradientMagnitude,
            gradient8U,
            0,
            255,
            cv::NORM_MINMAX,
            CV_8U
        );


        // ====================================================
        // 7. 梯度二值化成边缘图
        // ====================================================

        cv::Mat edgeMap;

        cv::threshold(
            gradient8U,
            edgeMap,
            0,
            255,
            cv::THRESH_BINARY |
            cv::THRESH_OTSU
        );


        // ====================================================
        // 8. 计算梯度方向
        // ====================================================

        cv::Mat orientation;

        cv::phase(
            gradientX,
            gradientY,
            orientation,
            true
        );


        // ====================================================
        // 9. 统计主体面积和主体内部总结构量
        // ====================================================

        const int totalSubjectPixels =
            cv::countNonZero(subjectMask);


        cv::Mat subjectEdgeMap =
            cv::Mat::zeros(
                edgeMap.size(),
                CV_8UC1
            );

        edgeMap.copyTo(
            subjectEdgeMap,
            subjectMask
        );


        const int totalSubjectEdges =
            cv::countNonZero(
                subjectEdgeMap
            );


        if (totalSubjectEdges == 0)
        {
            throw std::runtime_error(
                "No edge structure found inside subject."
            );
        }


        // ====================================================
        // 10. 约束参数
        // ====================================================

        // 候选矩形自身至少 90% 位于主体内部
        const double minInsideSubjectRatio =
            0.90;

        // ROI 至少覆盖整个主体 10% 的面积
        const double minSubjectCoverage =
            0.10;

        // ROI 至少覆盖整个主体 15% 的结构
        const double minStructureCoverage =
            0.15;


        // ====================================================
        // 11. 候选 ROI 尺寸
        // ====================================================

        const std::vector<double> widthRatios =
        {
            0.25,
            0.35,
            0.45,
            0.55,
            0.65
        };

        const std::vector<double> heightRatios =
        {
            0.12,
            0.18,
            0.25,
            0.32,
            0.40
        };


        // ====================================================
        // 12. 保存当前最优结果
        // ====================================================

        double bestScore = -1.0;

        double bestDensity = 0.0;
        double bestDirection = 0.0;
        double bestSpread = 0.0;

        double bestSubjectCoverage = 0.0;
        double bestStructureCoverage = 0.0;

        cv::Rect bestRoi;


        // ====================================================
        // 13. 多尺度 ROI 搜索
        // ====================================================

        for (double widthRatio : widthRatios)
        {
            for (double heightRatio : heightRatios)
            {
                const int roiWidth =
                    static_cast<int>(
                        subjectBox.width
                        * widthRatio
                    );

                const int roiHeight =
                    static_cast<int>(
                        subjectBox.height
                        * heightRatio
                    );


                if (roiWidth < 10 ||
                    roiHeight < 10)
                {
                    continue;
                }


                const int step =
                    std::max(
                        2,
                        std::min(
                            roiWidth,
                            roiHeight
                        ) / 8
                    );


                for (int y = subjectBox.y;
                     y + roiHeight
                         <= subjectBox.y
                         + subjectBox.height;
                     y += step)
                {
                    for (int x = subjectBox.x;
                         x + roiWidth
                             <= subjectBox.x
                             + subjectBox.width;
                         x += step)
                    {
                        const cv::Rect candidate(
                            x,
                            y,
                            roiWidth,
                            roiHeight
                        );


                        // ------------------------------------
                        // 13.1 候选矩形有多少位于主体内部
                        // ------------------------------------

                        const cv::Mat candidateSubject =
                            subjectMask(candidate);

                        const int candidateSubjectPixels =
                            cv::countNonZero(
                                candidateSubject
                            );

                        const double insideSubjectRatio =
                            static_cast<double>(
                                candidateSubjectPixels
                            )
                            / candidate.area();


                        if (insideSubjectRatio <
                            minInsideSubjectRatio)
                        {
                            continue;
                        }


                        // ------------------------------------
                        // 13.2 ROI 覆盖整个主体多少面积
                        // ------------------------------------

                        const double subjectCoverage =
                            static_cast<double>(
                                candidateSubjectPixels
                            )
                            / totalSubjectPixels;


                        if (subjectCoverage <
                            minSubjectCoverage)
                        {
                            continue;
                        }


                        // ------------------------------------
                        // 13.3 ROI 覆盖整个主体多少结构
                        // ------------------------------------

                        const cv::Mat candidateSubjectEdges =
                            subjectEdgeMap(candidate);


                        const int candidateEdgeCount =
                            cv::countNonZero(
                                candidateSubjectEdges
                            );


                        const double structureCoverage =
                            static_cast<double>(
                                candidateEdgeCount
                            )
                            / totalSubjectEdges;


                        if (structureCoverage <
                            minStructureCoverage)
                        {
                            continue;
                        }


                        // ------------------------------------
                        // 13.4 结构密度
                        // ------------------------------------

                        const double density =
                            static_cast<double>(
                                candidateEdgeCount
                            )
                            / candidate.area();


                        // ------------------------------------
                        // 13.5 方向丰富度
                        // ------------------------------------

                        const cv::Mat candidateOrientation =
                            orientation(candidate);


                        const double directionRichness =
                            calculateDirectionRichness(
                                candidateSubjectEdges,
                                candidateOrientation
                            );


                        // ------------------------------------
                        // 13.6 空间分布程度
                        // ------------------------------------

                        const double spatialSpread =
                            calculateSpatialSpread(
                                candidateSubjectEdges
                            );


                        // ------------------------------------
                        // 13.7 最终结构复杂度评分
                        // ------------------------------------

                        const double score =
                            density
                            * directionRichness
                            * spatialSpread;


                        // ------------------------------------
                        // 13.8 保存最佳 ROI
                        // ------------------------------------

                        if (score > bestScore)
                        {
                            bestScore =
                                score;

                            bestDensity =
                                density;

                            bestDirection =
                                directionRichness;

                            bestSpread =
                                spatialSpread;

                            bestSubjectCoverage =
                                subjectCoverage;

                            bestStructureCoverage =
                                structureCoverage;

                            bestRoi =
                                candidate;
                        }
                    }
                }
            }
        }


        if (bestScore < 0.0)
        {
            throw std::runtime_error(
                "No valid ROI found. "
                "Try relaxing coverage constraints."
            );
        }


        // ====================================================
        // 14. 生成 ROI mask
        // ====================================================

        cv::Mat roiMask =
            cv::Mat::zeros(
                grayImage.size(),
                CV_8UC1
            );


        cv::rectangle(
            roiMask,
            bestRoi,
            cv::Scalar(255),
            cv::FILLED
        );


        // ====================================================
        // 15. 在原图中画出 ROI
        // ====================================================

        cv::rectangle(
            colorImage,
            bestRoi,
            cv::Scalar(0, 0, 255),
            3
        );


        // ====================================================
        // 16. 保存结果
        // ====================================================

        cv::imwrite(
            roiMaskOutputPath,
            roiMask
        );

        cv::imwrite(
            roiOverlayOutputPath,
            colorImage
        );


        // ====================================================
        // 17. 输出结果
        // ====================================================

        std::cout
            << "Best ROI position: x="
            << bestRoi.x
            << ", y="
            << bestRoi.y
            << std::endl;


        std::cout
            << "Best ROI size: "
            << bestRoi.width
            << " x "
            << bestRoi.height
            << std::endl;


        std::cout
            << "Subject coverage: "
            << bestSubjectCoverage
            << std::endl;


        std::cout
            << "Structure coverage: "
            << bestStructureCoverage
            << std::endl;


        std::cout
            << "Structure density: "
            << bestDensity
            << std::endl;


        std::cout
            << "Direction richness: "
            << bestDirection
            << std::endl;


        std::cout
            << "Spatial spread: "
            << bestSpread
            << std::endl;


        std::cout
            << "Final score: "
            << bestScore
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
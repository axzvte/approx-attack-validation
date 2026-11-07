## 计算定义及来源

以下是按原论文公式独立编写的 Python 实现，数值约定完整记录，尚未与原作者二进制
逐像素比较。论文写作应披露本实验的分割、颜色转换和 Otsu 二值化方式。

### RC

依据 Cheng 等 CVPR2011 的区域对比公式5–7：

1. 用 scikit-image Felzenszwalb 对原尺寸 RGB 图分割，明确选择
   `scale=100, sigma=0.5, min_size=50`。这些是本实现的分割参数。
2. RGB 每通道均分12档。按颜色频数保留覆盖至少95%像素的颜色箱，
   每箱代表色为其中像素的平均 RGB，再转为 Lab。其余颜色箱映射到最近保留 Lab 代表色。
3. 每个区域构建归一化颜色直方图。区域颜色距离为各颜色对 Lab 欧氏距离的加权和，
   权重为两区域对应颜色的概率乘积，使用完整直方图而非单个区域均值色近似。
4. 区域对比度为其与其他区域的颜色距离加权和。区域大小权重为像素数/全图像素数，
   空间权重为 `exp(-centroid_euclidean_distance/0.4)`。
   质心使用像素中心坐标除以原图宽高。区域与自身的比较排除。
5. 区域分数映射回所有原像素，形成原尺寸连续响应图。

使用相对区域大小只使全部分数共同缩放，逐图 Otsu 分类保持同一尺度变换意义。
本实现保留原始连续响应，不添加中心偏置、不删除图像边界、不做 GrabCut。

原论文与作者页：
- https://cg.cs.tsinghua.edu.cn/people/~cmm/Saliency/ChengSaliencyCVPR2011S.pdf
- https://cg.cs.tsinghua.edu.cn/people/~cmm/Saliency/Index.htm


RC 原尺寸响应逐图使用 256 档 Otsu，严格大于阈值的点置 1，常量响应为空掩膜。与 U²-Net 掩膜逐图取并集后，以 stretch 像素中心采样统计。每个坐标的分母是所选图片总数，外接框包含全部高频点，坐标为 0 起始半开区间 [left, top, right, bottom)。不移除孤立点。


# 历史实验归档

此目录保留整理前的实验记录，不代表重新运行得到的新结果。

- `experiments/<原目录名>/`：旧实验目录，内部层级和文件内容保持不变。
- `experiments/root_outputs/`：原工程根目录的图片、CSV、日志、汇总和压缩包。
- `manifests/project_files_before_cleanup.tsv`：用户提供的整理前文件清单。

`force_active_pipeline_20261014_221356/` 内的 Stage1 CSV 仍供重评估脚本读取。可以通过脚本参数显式指定本归档中的目录。

编译产物、Python 虚拟环境和未参与当前构建的旧代码没有移入此处，仍可从完整备份分支 `backup/before-cleanup-20261023` 找回。不要将历史结果覆盖为后续实验输出。

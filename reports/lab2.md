# ECNU Tiger 2026 春季 Lab2：词法分析

## 公开远端验收结果（2026-11-07补记）

实现提交 `0674d1ef0e30d76ad41580e25f85932f85e67094` 已由 [Actions 42163866601](https://github.com/edidada/ecnu-tiger-compiler-26sp/actions/runs/42163866601) 在 Ubuntu 24.04 Release 构建和执行成功：**68/68：54个原始用例和14个边界用例**。下载结果已存入 langnote 的 courses/lab-reports/ECNU-Tiger26-Lab2-results.json。远端分支与成功 run 的 SHA 已核对。本文此前“未执行/未发布”是开发阶段历史状态，以本节及最新完成的工作流为准。

只证明公开用例及报告列出的附加检查，不宣称私有评分或完整后续实验完成。本地未编译或运行 C++。本次只补报告，自动工作流将核查文档提交后的最终 HEAD。

## 来源与实现

上游为 `https://gitee.com/east-china-normal-university_ttb_cs/tiger-compiler-26sp` 的 `lab2` 分支（`624226351fb3cde7acb677613f11d60c8f9749a7`）；实验分支为 `edidada/lab2`。该课程为 ECNU 补充项目，不等同于 SJTU 当前期实验。

补全全部关键字、标点、最长匹配运算符、标识符及整数；通过独立状态处理嵌套注释、字符串和续行。字符串支持换行、制表、引号、反斜杠、三位十进制字节及控制字符转义，以及反斜杠包围的空白续行。字符串位置保持为开始引号；续行中的 LF 按实际字节位置登记。

错误路径包括非法字符、非法转义、超过 255 的字节转义、字符串裸换行或控制字符、错误续行及注释或字符串 EOF。EOF 错误后切回初始状态，新增驱动检查再次读取 EOF 的稳定性。整数转换保留上游 std::stoi，超出 C++ int 范围尚无自定义诊断。

## 独立远端验证

工作流 `.github/workflows/lab2.yml` 在 Ubuntu 24.04 安装 flexc++ 和 bisonc++，调用 `scripts/verify_lab2.py`，按 Release 构建真实扫描器。逐字节比对 54 个上游输入的输出；只在临时副本规范化 CRLF，与上游评分脚本一致，保留原始输入及参考答案。另有 14 组边界用例，检查 token 字节、1-based 偏移、错误行列、错误数量及退出码。字节驱动保留 NUL，避免百分号 s 打印掩盖错误。总超时 15 分钟，单用例 10 秒，上传实际输出及 JSON 汇总。

## 当前证据

仅完成 Python AST、Git diff 和上游输入、参考答案、测试驱动的 Git blob 一致性检查，见 `lab2-static.json`。未本地编译，未运行扫描器，未取得远端结果。生成器兼容性及运行行为仍待 Actions 验证，不计为验收通过。OAuth 当前缺少 workflow scope，发布被拒绝时保留本地提交。

生成器接口参考 flexc++ 官方手册：https://fbb-git.gitlab.io/flexcpp/manual/flexc%2B%2B03.html 。实现未使用其他学生答案。

## 独立构建依赖修正

静态复核发现上游 Lab2 的 CMake 会生成并链接尚未实现的 Lab3 语法，其中 exp 等非终结符没有规则，词法实验也会因此受阻。现从原始 tiger.y 原样提取 token、union 和类型预包含声明，在 build/generated 下生成仅供词法实验使用的 token 头；空起始规则生成的 parser 不编译、不链接、不参与验收。两个词法驱动均链接实际 flexc++ 扫描器及 ErrorMsg，实现仍覆盖全部 54 个原始词法用例。上游 Lab3 文件未改动，后续语法实验仍须实现真正的完整语法。这项构建修正尚待远端验证。

## 远端发布恢复

2026-11-06 已使用本机现有 edidada SSH 身份发布独立实验分支，无需修改账户权限。此前 workflow scope 拒绝仅针对 HTTPS OAuth 路径。现进入实际 Actions 编译/运行修复，最终是否通过以完成的 run 与提交 SHA 为准。

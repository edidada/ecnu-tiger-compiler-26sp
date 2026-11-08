# Lab5-2：指令选择与虚拟汇编

## 已通过的公开验证（2026-11-08）

最终提交 be13680591d1e2803d5b912b88aeb773733728e2，[Actions 42432560328](https://github.com/edidada/ecnu-tiger-compiler-26sp/actions/runs/42432560328) completed/success，工件汇总 113/113：

- 官方 20 组（16 个 .tig + merge 4 组输入）生成虚拟汇编，使用**未修改**的官方 `scripts/lab5_test/main.py` 解释器执行，按原 `diff -w -B` 等价规则比较输出：20/20；
- 35 个扩展程序（`scripts/lab5_2_cases.json`）经显式适配器 `scripts/run_lab5_2_extended.py` 执行，检查 stdout、退出状态与 `ASM_RESULT`：35/35。适配器仅在扩展用例上使用，官方 20 组路径完全不变；
- 5 项适配器边界用例（正文中明确公开）：5/5；
- 对 52 份生成汇编（17 官方 + 35 扩展）的文本元数据检查：52/52；
- 复制的 Lab5-1 回归 `scripts/verify_lab5_1.py` 原样运行，77/77 内部检查全部通过（17 官方逃逸 AST、20 官方 IR 输出、30 严格 IR 执行、8 语义拒绝、帧布局、qsort 严格边界）。

结果 JSON 的机器汇总保留于 `reports/lab5-2-results.json`（本提交内附）及 Actions 工件 `tiger-lab5-2-results`。

## 明确的边界与局限（不接受超过记录范围的结论）

- 本次验收只覆盖虚拟寄存器汇编 + 官方/适配器解释器执行，**不是** Lab6 寄存器分配、原生机器代码、私有评分或完整 ISA 验收。
- 适配器扩展与官方解释器的差异已逐项公开：有符号除法 `idivq` 实现向零截断（fixture `idiv_truncates_toward_zero`/`idiv_remainder_toward_zero` 验证 -17/5 → 商 -3、余数 -2，拒绝 Python 地板除法）；字符串库（size/concat/substring/ord 等）与 `\000`、`\"`、`\\` 转义（fixture `adapter_preserves_string_escapes` 断言 7 字节原文保留）；任意位数虚拟寄存器 `t\d+`（fixture `adapter_accepts_wide_temp_registers` 使用 t1234）；未映射内存读立即报错（fixture `adapter_rejects_unmapped_memory`），而官方解释器对未映射地址返回 0xbaadbeef。这些差异都没有混入官方 20 组的对照。
- 文本元数据检查覆盖：每个函数的 `.set <fn>_framesize`、prologue `subq $N, %rsp` 与 epilogue `addq $N, %rsp`+`retq`；`cqto` 紧接 `idivq`；framesize 形如 `<fn>_framesize±d` 且基址为 `%rsp`；条件跳转紧跟 `cmpq` 且随后为 `jmp`，所有跳转目标在文件内定义；每次实参快照后的 ABI 填充窗口内只允许进一步 ABI 写或 `N(%rsp)` 栈参存入，ABI 填充为有序前缀；>6 参时必须恰好 6 个 ABI 写且栈参偏移为 0、8、… 连续字。**Caller-save/return-sink 的 use/def 元数据是 IR 级（Print 为空串），无法从生成文本核验**，故未在本报告中声明该文本检查覆盖它。
- 复制回归的 testdata 定位：36ba397 首次接线时暴露 lab5-2 分支上的 `refs-part1/{merge,qsort,queens,test_patch}.out` 与 `testcases/test_patch.tig` 停留在较旧版本，与已验收 Lab5-1 HEAD 6f6bd44 中勘误后的官方参考不一致（iffExp 与 opExp(AND,…,0) 打印差异）。be13680 已按 Lab5-1 已验收版本同步这 5 个文件；未修改任何官方解释器脚本或 starter。
- 36ba397 的 run 42432179476 失败仅由上述参考版本差异引起；其余各节在该 run 已通过，修正后统一在 be13680 复验。

## 实现与历史记录

本分支从官方 lab5-2 d2aeaa89a8efddf713d09bac4dcfb187e6bfb3d8 开始，承接本人已通过的 Lab5-1 前端/逃逸/帧/IR 实现，保留当前阶段 tree::Canon/Munch 及 Frag::OutputAssem 接口。代码生成补虚拟 FP 寻址、load/store、算术/比较/跳转、调用和 ABI 寄存器/栈参数、caller-save 定义/return sink 元数据、帧 prologue/epilogue。调用实参先全部求值并快照，避免后续实参中的调用覆盖已搬入的物理参数寄存器。使用真实 cqto/idivq 构造有符号除法；原解释器忽略 cqto 且其负除法采用 Python 地板除法，该差异已由边界用例明确。

独立 lab5-2.yml 在远端做 Release 构建并执行 `scripts/verify_lab5_2.py`：官方 20 组走未修改解释器；扩展 35 走适配器；边界用例与元数据检查如上；最后以子进程原样运行复制的 `scripts/verify_lab5_1.py` 并要求 77/77。任何段的数量少于断言值（20、35、5、52、1）都会直接失败，0/0 不可能被视作通过。工作流上传包含两份结果目录的工件。本地从未编译或运行课程代码，全部构建与执行都在 Actions。

这是 Lab5-2 虚拟寄存器汇编阶段；未完成 Lab6 寄存器分配，不把其称为原生机器代码。官方解释器缺少完整字符串库且部分算术不等价于真实 x86，不能把官方输出通过泛化为完整 ISA 验收；适配器的扩展也不声明官方解释器已具备该能力。

## 失败排查历史（保留）

3705775 的 run 42381832661 在远端编译失败，canon.h 使用 std::invalid_argument 但缺少 stdexcept 直接包含。已补头文件，尚无运行成绩，不将 0/0 视为通过。

0b3b3d8（1e3ebb1 同一改动系列）的 run 42423426441 发现 C++ if 初始化变量的作用域包含 else 块，CjumpStm::Munch 中 right 重声明。已重命名立即数变量，保留比较和跳转行为；该次仍无实验运行成绩。

36ba397 的 run 42432179476 为 73/77（官方逃逸 AST 中 merge/qsort/queens/test_patch 失败），定位为参考文件版本未同步，be13680 修正；该 run 其余各节通过。

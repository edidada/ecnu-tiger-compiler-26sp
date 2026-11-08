# Lab6：寄存器分配（图着色迁移 / 冒泡改写）

## 已通过的公开验证（2026-11-08）

最终提交 5b410c06e922993cd2bad5b4617e700d7fa73e40，[Actions 42446427911](https://github.com/edidada/ecnu-tiger-compiler-26sp/actions/runs/42446427911) completed/success，工件汇总 113/113：

- 官方 20 组（16 个 .tig + merge 4 组输入）以 Lab6 `tiger-compiler` 目标（RA 开启）编译，生成**只有机器寄存器**的汇编，用未修改官方 `main.py` 解释器执行、按原 `diff -w -B` 规则比较：20/20；
- 35 个扩展程序（同一 RA 路径 + `scripts/run_lab5_2_extended.py` 适配器）检查 stdout、退出码与 `ASM_RESULT`：35/35；
- 5 项适配器边界用例（向零截断 idiv、t1234 宽虚拟寄存器、字符串转义、未映射内存拒绝、rodata 保留）：5/5；
- 对 52 份 RA 后汇编（17 官方 + 35 扩展）的文本元数据检查：52/52。检查项：每函数 `.set <fn>_framesize`/prologue `subq $N, %rsp`/epilogue `addq $N, %rsp`+`retq`；`idivq` 只能在 `cqto` 或 spill 重载之后（区分记录）；framesize 操作数形状；跳转目标定义与条件跳转伴随 `jmp`；每个过程体内**不存在 `t<num>` 虚拟临时**（Lab6 特判）；含栈参数调用的 outgoing 偏移为 0、8、… 连续字；
- 复制的 Lab5-1 回归 `scripts/verify_lab5_1.py` 原样运行：内部 77/77。

机器汇总 `reports/lab6-results.json`（本提交内附）；Actions 工件 `tiger-lab6-results` 包含 `lab6-results/`、`lab5-1-results/` 两目录。

## 实现概要

- `src/tiger/liveness/flowgraph.{h,cc}`：控制流图工厂 + `assem::Instr` 的 `Def()/Use()`；`jmp` 不连下一条，带 `jumps_` 的 Oper 在全图扫描后补边；跳转目标不在同过程内时显式报错。
- `src/tiger/liveness/liveness.{h,cc}`：反向数据流不动点。两个实现级缺陷曾在远端 CI 中暴露并修复：`tab::Table::Set` 对缺失 binder 是静默 no-op（首轮写入失联导致死循环，已在 Set/Enter 结合后收敛）；旧 out 集合曾误从 `in_` 表读取导致振荡。均为静态可复核的历史 commit。
- `src/tiger/regalloc/color.{h,cc}`：Chaitin-Briggs 简化/选择。调色板 15 个通用寄存器（`%rsp` 除外）；机器寄存器两两互相干扰且带走固定色。**move 合并只允许双方均为虚拟临时且互不干扰的 move 对**（正式安全准则，不做预着色例外），消除 callee-save 影子临时造成的跨过程高压。选择时若无可出色则记为 spill 候选。
- `src/tiger/regalloc/regalloc.{h,cc}`：迭代驱动。每一轮重建于**原始快照**：spill 集合按 temp 记槽（`Frame::AllocSpillSlot` 下移 `offset_`，`FrameSize()` 自动随之增长， `.set`/prologue/epilogue 与 spill 寻址一致）；重写时每个定义后插入 `movq t, fs−k(%rsp)`、每个使用前插入反向装载（dst/src 列表同步替换为新鲜临时，正文文本不动）；至无 spill 或 48 轮上限（超限显式失败，不静默）。最终结果深拷贝，避免与 `RegAllocator` 生死期耦合。
- `src/tiger/frame/fram/temp`：`Frame::AllocSpillSlot()` 纯虚，`X64Frame` 实现之。

## 明确的边界与局限

- 本次验收是**虚拟机上的寄存器分配**：汇编只含机器寄存器，但执行与比较仍通过官方解释器/适配器。不声称真实 x86 执行、OS 运行、ABI 全兼容（如返回地址/栈序）或私有评分。
- caller-save/return-sink 的 use/def 元数据是 IR 级（该 Oper 打印为空串），不能从生成文本核验；本报告不把它算入文本检查覆盖。
- Lab6 元数据没有重做“ABI 实参快照后按序填寄存器”的窗口检查：合并/溢出重写在该语义下合法地改写或删除 ABI 填充指令，该属性已在 Lab5-2（RA 前）52 份汇编上以 20+35 验证并在 Lab6 经官方 20+扩展 35 的执行等价性覆盖。检查以 `checks` 字段如实记录。
- spill 流量允许出现在 `idivq` 前与调用窗口内，检查窗口内 spill 模式与连续栈参偏移；spill 是否“必要”无独立证明。
- 中间轮次的 spill 槽地址在每轮重建时逐 temp 重分配，最终输出以最终轮为准；过程级地址解释与官方解释器一致。

## 调试历史（远端 CI 保留记录）

- fix：liveness 不动点（Set 静默 no-op、out 表误读）→ 多轮全程序超时/中断；
- fix：ReturnSink Oper `Def()` 为空串导致 InterfGraph 解引用空指针 → 官方用例批量 exit 1 / 扩展 -11；
- fix：着色邻接在简化阶段清空、选择期读不到邻居颜色 → 全部值落 `%rax`；
- fix：预着色例外合并引发语义破坏（dec2bin 位数全 0、merge 未映射读、树输出坏）→ 收窄为仅虚拟对合并，全部通过；
- fix：元数据旧检查（ABI 顺序窗/重复填充）按 Lab6 语义降级为执行等价性覆盖，各检查如实记录在 `checks`。

这是补项目 ECNU26sp Lab6；未实现原生后端评测及其他仓库的后续实验，不代表 SJTU 当期 starter 或其他课程进度。

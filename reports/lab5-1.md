# Lab5-1：逃逸分析、栈帧及IR翻译

## 已通过的公开验证（2026-11-07）

实现提交54d2fecefc8647fd7fec9aae08c1e72759a4e7ca，[Actions 42380801948](https://github.com/edidada/ecnu-tiger-compiler-26sp/actions/runs/42380801948)已完成且成功，下载产物77/77：17官方逃逸AST、20官方程序/输入输出、30独立严格IR执行、8语义拒绝、1帧布局及1原qsort严格内存拒绝边界。Release构建test_lex/test_parse/test_semant/test_translate及IR/帧测试目标全部成功。没有修改官方测试或参考文件；qsort在显式官方内存模式恰有1次未映射读，其他官方用例为0，严格模式继续报错。

本次是Lab5-1逃逸/帧/IR公开验收，不是原生后端或学校私有评分。原生寄存器分配、指令选择和Lab5-2/6仍待实现。报告提交后的最终HEAD另在langnote机器证据中记录并复验。以下保留实现及失败排查历史，其中“待验证”描述当时状态。

## 实现与历史记录

分支edidada/lab5-1，来源为ECNU26sp官方lab5-1分支f4cd34c8e0c46921084a2a41c5dda0b828d7074e。承接本人已验收Lab4的词法/语法/语义实现，保留本阶段新增AST接口。它是另行注明的ECNU补充项目，不替换SJTU当期课程starter。

实现词法函数深度的逃逸遍历、变量初始化器/for上下界作用域、参数遮蔽；8字节帧访问、逃逸局部、寄存器/栈形参搬移、callee-save和outgoing空间；Ex/Nx/Cx及backpatch、短路条件、循环/溢出安全的for终止、记录/数组、类型及互递归函数、静态链。按仓库NewLevel约定，隐藏静态链是最后一个形参和实参；外部调用保持原magic marker约定。

原17份评分只比较逃逸后的AST，不能证明IR可用。新增独立IR解释器，仅执行生成的tree::Stm/Exp与ProcFrag，不评估源AST；30组有效程序覆盖输出/返回值、三层捕获、互递归、超过六参、参数及聚合初始化顺序、别名/引用比较、循环上界一次求值与INT_MAX边界、短路/嵌套控制流和字符串。另8组无效程序必须在语义阶段拒绝。逻辑结果通过Cx归一到0/1；整数条件接受任意非零为真。

独立工作流.github/workflows/lab5-1.yml远端Release构建全部前端目标，然后逐例记录日志/哈希和JSON。当前仅完成静态检查，没有本地编译或执行，尚待Actions实际验证。原AST refs未修改；不将自建解释器通过等同本机汇编/代码生成通过，Lab5-2/6尚未完成。


65982f9 的run 42380273388已成功，55/55：17原AST、30独立IR、8语义拒绝。初次164a29f仅因view shift条件表达式混用两个派生指针编译失败，65982f9已修正。进一步补20份官方运行输出（16普通程序+merge的4组标准输入）、专门帧布局检查，要求形参搬移、栈参数偏移、独立局部存储、outgoing最大值及SysV调用前16字节对齐。虚拟FP为入口RSP，FrameSize按16k+8计算。新增部分仍待新的远端最终HEAD验证；解释器结果不是原生后端验收。


f1d716c 的run 42380518499为75/76：仅官方qsort在IR严格模式触发未映射读取；帧布局通过。源码quicksort先初始化key=list[left]才判断left<right，递归末次left=16产生一次未使用的越界读。仓库原scripts/lab5_test/state_table.py::load_mem明确对未映射地址返回0xbaadbeef。新增显式--official-memory仅用于20组官方输出对照，要求qsort恰好1次、其他官方程序0次未映射读；30自建有效程序仍默认严格，必须0次未映射读；额外要求原qsort在严格模式继续报错。没有修改原程序/参考结果，也不把官方运行时的宽松内存模型当作越界安全证明。IR helper标签同步为官方运行时的init_array/alloc_record/string_equal命名；string_compare及完整字符串库由独立IR测试运行时验证，不声明原生后端库已补齐。

# ECNU Tiger 26sp Lab3：语法分析

## 来源和实现范围

上游：https://gitee.com/east-china-normal-university_ttb_cs/tiger-compiler-26sp ，lab3 起点 `7681461c2ab9a184805ba17aa6ab9dce87827c84`。工作分支 `edidada/lab3`，作为 ECNU 补充课程，不替代 SJTU 当前身份受限的实验材料。

补全 bisonc++ 语法及 AST 动作，包括变量、复合字段/下标、数组和记录构造、函数调用、算术/关系/布尔运算、赋值、条件、循环、break、let、序列以及类型/变量/函数声明。运算层级为 OR、AND、单次比较、加减、乘除、一元负号；算术左结合，连续比较在语法结构上拒绝。显式处理 if-then-else 的最近匹配关系；数组构造与下标读取共用前缀后通过 OF 区分。相邻类型及函数声明组成一个组，以保留互递归的 AST 结构；列表用右递归加 Prepend 保留源程序顺序。

复用本地 Lab2 的实际扫描器源码，不复用 Lab2 的临时 token-only 解析接口；Lab3 构建真实完整 parser。上游 AST 析构函数会删除来自全局 Symbol::UniqueSymbol 的共享指针，可能在同名标识符多次出现或多次解析时重复释放。已移除 AST 对这些驻留符号的删除，仍删除其拥有的 Var/Exp 子树。不宣称已完成上游全部容器的内存泄漏治理。

## 验证方案

独立 `.github/workflows/lab3.yml` 运行 Ubuntu 24.04，安装 flexc++/bisonc++，通过 `scripts/verify_lab3.py` 按 Release 构建。51 个原始输入中，50 个逐字节比较参考 AST，test49 必须报第 5 行第 18 列语法错误。另有 13 个手写 AST 快照、1 个同进程三次解析符号复用检查，以及 7 个非法语法/词法拒绝用例。附加测试驱动在词法报错时也返回失败，避免扫描器忽略非法字符后 AST 仍被当作成功。上传实际输出、诊断和 results.json。

原始用例和测试驱动不改动；只对临时输入副本及读取的参考答案做 CRLF 到 LF 规范化。快照用例不含字符串，比较时仅去除排版空白；官方 AST 输出仍逐字节比较。

## 当前证据及待验收项

仅进行了 Python AST、快照括号平衡、Git diff 和原始测试/驱动/符号池代码 Git blob 同一性检查，见 lab3-static.json。未本地编译、未运行 parser 或 grader，尚无远端 CI 成绩。生成器冲突、编译兼容性、所有 AST 和拒绝路径均待 Actions 实测，不能视作已验收。

AST 的位置字段沿用上游 reduction 时读取 Scanner 位置的接口，尚未建立精确到每个 AST 节点起始 token 的位置栈；当前验收设计检查语法错误位置和 AST 结构，不证明 AST 位置信息完整。语义检查、翻译和机器码属于后续实验。来源和语法接口参考 bisonc++ 官方手册：https://fbb-git.gitlab.io/bisoncpp/manual/bisonc%2B%2B04.html ，未使用其他学生答案。

静态复核补充：bisonc++ 对无终结符规则的默认优先级与 GNU Bison 不同。表达式透传及左值到表达式的规则显式使用低 CONTROL 优先级，避免截短控制体或抢先归约赋值左侧；声明分组及裸 ID 使用更低 GROUP 优先级，等待相邻 TYPE/FUNCTION 或下标/调用前缀。生成器实际冲突仍待 CI 检查，不能由静态阅读证明无冲突。

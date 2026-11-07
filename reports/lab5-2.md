# Lab5-2：指令选择与虚拟汇编

本分支从官方lab5-2 d2aeaa89a8efddf713d09bac4dcfb187e6bfb3d8开始，承接本人已通过的Lab5-1前端/逃逸/帧/IR实现，保留当前阶段tree::Canon/Munch及Frag::OutputAssem接口。代码生成补虚拟FP寻址、load/store、算术/比较/跳转、调用和ABI寄存器/栈参数、caller-save定义/return sink元数据、帧prologue/epilogue。调用实参先全部求值并快照，避免后续实参中的调用覆盖已搬入的物理参数寄存器。使用真实cqto/idivq构造有符号除法；原解释器忽略cqto且其负除法采用Python地板除法，后续需要明确边界回归。

独立lab5-2.yml远端Release构建后，对原16普通程序及merge的4组输入运行生成汇编，使用未修改的官方解释器，按原diff -w -B规则比较输出，保存汇编、源码与参考哈希及日志。当前尚待CI，不在本地编译、运行课程代码或模拟器。

这是Lab5-2虚拟寄存器汇编阶段；未完成Lab6寄存器分配，不把其称为原生机器代码。官方解释器缺少完整字符串库且部分算术不等价于真实x86，不能把官方输出通过泛化为完整ISA验收。


3705775 的run 42381832661在远端编译失败，canon.h使用std::invalid_argument但缺少stdexcept直接包含。已补头文件，尚无运行成绩，不将0/0视为通过。

0b3b3d8 的run 42423426441发现C++ if初始化变量的作用域包含else块，CjumpStm::Munch中right重声明。已重命名立即数变量，保留比较和跳转行为；该次仍无实验运行成绩。

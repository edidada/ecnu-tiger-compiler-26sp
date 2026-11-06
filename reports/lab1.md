# ECNU Tiger 26sp Lab1：直线语言解释器（未验收）

上游为CSDIY原SJTU条目补充引用的华东师大Gitee框架，25sp链接现重定向26sp。与SJTU SE3355 2026材料分开发放，不能据此把SJTU受限starter标为取得。GitHub为保留完整历史的镜像，非平台原生fork。

分支edidada/lab1基于38daecedcbabd9fcde2e66d6f464bfc91aac8ec6。补Exp/ExpList虚接口、顺序语句、赋值、print的最大参数分析及解释；使用不可变链环境，表达式副作用按从左到右传递。修复上游OpExp右操作数仍用旧环境的问题，未绑定变量在Release中明确报错。

独立lab1.yml只调用脚本：Release构建、原七个参考输出逐项比对（原grade.sh即便输出不匹配仍可能退出0，因此不单凭退出码），再验证左ESEQ对右变量可见、赋值副作用、嵌套print最大宽度及链环境不变。测试源码保留，Release回归不用会被NDEBUG移除的assert。

当前只完成源码/AST/static diff检查，尚无远端编译或执行结果，不计实验完成。本地不编译。该教学AST使用裸指针与原始分配约定，没有声称解决通用内存回收或完整编译器；lab2至lab6仍待实现。

## 上游分支核对

reports/upstream-lab-map.json固定7个上游分支SHA、源码/测试数据文件数及TODO位置；仅lab1有VERSION文件，其他分支没有，不推断为同一版本布局。TODO数只是定位提示，不是未完成函数数或实验评分。Lab1原prog.cc/test_slp.cc及7份ref文件与上游Git对象逐字节一致；新增回归位于tests目录，没有替换原始案例。

当前需要通过gh auth refresh -h github.com -s workflow的交互授权，或恢复有发布能力的已连接GitHub工具，才能创建包含独立工作流的实验分支。已向用户说明所需操作，尚未收到授权完成回复，不假定权限已变。本地不启动编译或测试。

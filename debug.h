#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_DEBUG_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_DEBUG_H

#include <iosfwd>
#include <string>
#include <string_view>

// 调试输出配置。main 解析完参数后调用一次 configure()，其余模块只读。
//
// 所有 dump 一律写 stderr：解释执行模式下 stdout 属于被编译的 Brainfuck 程序，
// 往 stdout 写调试信息会直接污染程序的输出，也会破坏回归比对。
namespace dbg {

struct Options {
    // --dump-ir=<stages>：CFG 检查点
    bool parse = false;      // 构造完基本块，优化之前
    bool final = false;      // 优化结束、交给生成器之前的最终形态
    bool stages = false;     // optimize() 内部各阶段边界
    bool passes = false;     // 每个 pass 执行之后
    // --dump-asm
    bool asmDump = false;
    // --verify
    bool verify = false;
};

void configure(const Options& options);
[[nodiscard]] const Options& options();

// 形如 "parse" / "final" / "stage:<名字>" / "pass:<名字>"
[[nodiscard]] bool wantStage(std::string_view stage);
[[nodiscard]] bool wantAsm();
[[nodiscard]] bool wantVerify();

std::ostream& out();

// 解析 --dump-ir 的参数，逗号分隔。成功返回空串，失败返回错误信息。
// 可选值：parse、final、stages、all
[[nodiscard]] std::string applyStageList(Options& options, std::string_view list);

}  // namespace dbg

#endif //YETANOTHERNEOBRAINFUCKCOMPILER_DEBUG_H

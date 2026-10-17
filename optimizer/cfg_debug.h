#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_CFG_DEBUG_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_CFG_DEBUG_H

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

struct MidBasicBlock;

// 打印整张 CFG。块按 vector 下标编号（B0、B1...），前驱/后继也用编号表示，
// 不打指针地址，方便把两次 dump 直接做文本 diff。
// withInsts 为 false 时只打印每个块的头行（前驱/后继/指针状态）。
void dumpCFG(std::string_view title,
             const std::vector<std::unique_ptr<MidBasicBlock>>& blocks,
             bool withInsts = true);

// 检查 CFG 不变量。每个问题打印一行，返回发现的问题数。
std::int32_t verifyCFG(std::string_view stage, const std::vector<std::unique_ptr<MidBasicBlock>>& blocks);

#endif //YETANOTHERNEOBRAINFUCKCOMPILER_CFG_DEBUG_H

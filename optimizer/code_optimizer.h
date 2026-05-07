#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_CODE_OPTIMIZER_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_CODE_OPTIMIZER_H

#include <vector>

#include "mid_basic_block.h"


class CodeOptimizer {
public:
    CodeOptimizer(const std::vector<Quadruple>& codes, int32_t tapeSize);

    [[nodiscard]] const std::vector<std::unique_ptr<MidBasicBlock>>& getBasicBlocks() const { return m_basicBlocks; }
    [[nodiscard]] int32_t getTapeSize() const;
    [[nodiscard]] int32_t getInitOffset() const { return -m_minNegOffset; }
    [[nodiscard]] int32_t countIRs() const;

    // 一键代码优化
    void optimize(int32_t optimizeLevel, bool isStrict, bool printInfo);

    // for debug only
    [[nodiscard]] int32_t checkInnermostBalancedLoop() const;
    void printBasicBlocks() const;
    void printBasicBlocksBrief() const;

private:
    // 合并基本块，移除不可达基本块
    // 返回 true 表示基本块被合并（有新优化空间）
    bool mergeAndRemove();

    // 最内层局部指针平衡无副作用 while (*ptr) 消除
    // 测试变量±1、±3、±5……，其他变量 += 循环不变量，或其他变量 = 循环不变量 -> 可优化，循环次数为（uint8_t n = ((-t) * inv_k) & 255;）
    // 其他 -> 摆烂，放弃
    // 局部优化时传 true，全局优化时若先前已经分析过指针，可以传 false
    // 返回 true 表示产生优化
    // 先决条件：无
    bool eliminateLocalLoop(bool preAnalyzePtr, bool isStrict, bool printInfo);

    // 提升最内层局部指针平衡 while (*ptr) 中的常数赋值
    // 返回 true 表示产生优化
    // 先决条件：基本块通用优化
    bool hoistLocalLoopConstAssign(bool printInfo);

    // 在全局优化前折叠跳转
    // 返回 true 表示产生优化
    // 先决条件：基本块通用优化
    // 注意：这只在全局优化前有效，此时这些跳转的测试变量肯定是同一个
    bool foldJumpBeforeGlobal(bool printInfo);

    // 在全局优化后折叠跳转
    // 返回 true 表示产生优化
    // 先决条件：基于全局信息的基本块通用优化
    bool foldJumpWithGlobalInfo(bool printInfo);

    // 基本块通用优化，基于值传播 + 规范化哈希 + 延迟赋值
    // 处理常量折叠/传播、无效赋值消除、公共子表达式（仅交换律）
    // 先决条件：无
    bool geOptimizeLocal(bool printInfo);

    // 基于全局信息的基本块的通用局部优化，基于值传播 + 规范化哈希 + 延迟赋值
    // 处理常量折叠/传播、无效赋值消除、公共子表达式（仅交换律）
    // 先决条件：无（函数开头自动处理先决条件）
    bool geOptimizeLocalWithGlobalInfo(bool printInfo);

    // 基于活跃变量信息消除冗余赋值
    // 先决条件：无（函数开头自动处理先决条件）
    bool eliminateAssignWithLiveVarInfo(bool printInfo);

    // 全局指针位置分析
    void analyzePtrFlow() const;

    // 前向分析变量值，可用于跨基本块（指针确定）的常数折叠/传播、无效赋值消除
    void analyzeVarValueFlow() const;

    // 活跃变量分析
    // 必须全基本块指针已知，且完成指针移动消除，否则出错
    void analyzeLiveVar() const;

    // 全局优化预备：分析指针 + 指针移动消除 + 变量值分析
    void prepareForGlobal();

    std::vector<std::unique_ptr<MidBasicBlock>> m_basicBlocks;
    int32_t m_minNegOffset;
    int32_t m_maxPosOffset;
    int32_t m_tapeSize;
};


#endif //YETANOTHERNEOBRAINFUCKCOMPILER_CODE_OPTIMIZER_H
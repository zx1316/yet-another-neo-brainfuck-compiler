#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_CODE_OPTIMIZER_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_CODE_OPTIMIZER_H

#include <bitset>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "mid_basic_block.h"
#include "optimization_level.h"

// ---------------------------------------------------------------------------
// pass 框架
//
// 目标：所有 pass 都从同一个入口执行，由入口统一维护「哪些分析还有效」和
// 「哪些基本块可能变了」。以后要加自然循环、平衡区、支配树这类分析，只需在
// AnalysisId 里加一项，并在对应 pass 的 PassEffect 里声明失效条件。
// ---------------------------------------------------------------------------

enum class AnalysisId : std::uint8_t {
    PtrFlow,     // 指针位置（ptrIn / ptrOut）
    VarValue,    // 变量值（varValueInfo）
    LiveVar,     // 活跃变量（liveVarInfo）
    Count,
};

using AnalysisMask = std::bitset<static_cast<std::size_t>(AnalysisId::Count)>;

// 一个 pass 的影响声明。
struct PassEffect {
    // 跑完之后这些分析不再有效；空表示这个 pass 不破坏任何分析
    AnalysisMask invalidates;
    // 可能增删基本块或改动 CFG 边
    bool touchesCFG = false;
    // 这个 pass 自己精确标记了它改过的块（block->dirty）。
    // 为 false 时，runPass 在 pass 返回 true 后把所有块标脏。
    bool managesDirty = false;
};


class CodeOptimizer {
public:
    CodeOptimizer(const std::vector<Quadruple>& codes, int32_t tapeSize);

    [[nodiscard]] const std::vector<std::unique_ptr<MidBasicBlock>>& getBasicBlocks() const { return m_basicBlocks; }
    [[nodiscard]] int32_t getTapeSize() const;
    [[nodiscard]] int32_t getInitOffset() const { return -m_minNegOffset; }
    [[nodiscard]] int32_t countIRs() const;

    // 一键代码优化
    void optimize(OptimizationLevel level, bool printInfo);

    // 调试检查点：--dump-ir 命中时打印 CFG，--verify 时校验 CFG 不变量。
    // 违反不变量的数量累计在 m_verifyErrors 里；其中"最终形态"的违例会阻止生成产物，
    // 中间检查点的违例只报告（有些 pass 会短暂留下不一致的 preds，之后自行修好）。
    void checkpoint(std::string_view stage);
    [[nodiscard]] int32_t getVerifyErrors() const { return m_verifyErrors; }
    [[nodiscard]] int32_t getFatalVerifyErrors() const { return m_verifyErrorsAtFinal; }

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

    // 折叠"只含一条条件跳转"的基本块，把跳转前提到前驱。
    // useGlobalInfo 只影响三件事：
    //   - 处理哪些块（全局信息阶段之前处理没有 varValueInfo 的块，之后处理已有信息的块）；
    //   - 是否要求前驱与本块测试同一个变量（全局阶段才要求）；
    //   - 前驱怎么摘除（全局阶段只摘被内联的，早期阶段整块清空）。
    // 返回 true 表示产生优化。
    // 先决条件：useGlobalInfo 为 true 时需要基本块已带全局值信息。
    bool foldJump(bool useGlobalInfo, bool printInfo);

    // 基本块通用优化，基于值传播 + 规范化哈希 + 延迟赋值。
    // 处理常量折叠/传播、无效赋值消除、公共子表达式（仅交换律）。
    // useGlobalInfo 为 false 时用局部零值判定，只处理没有 varValueInfo 的块；
    // 为 true 时先 prepareForGlobal()，用块上的全局值信息，并在合并后重算。
    // 返回 true 表示产生优化。
    bool geOptimizeLocalPass(bool useGlobalInfo, bool printInfo);

    // 基于活跃变量信息消除冗余赋值
    // 先决条件：无（函数开头自动处理先决条件）
    bool eliminateAssignWithLiveVarInfo(bool printInfo);

    // 全局指针位置分析
    void analyzePtrFlow();

    // 前向分析变量值，可用于跨基本块（指针确定）的常数折叠/传播、无效赋值消除
    void analyzeVarValueFlow();

    // 活跃变量分析
    // 必须全基本块指针已知，且完成指针移动消除，否则出错
    void analyzeLiveVar();

    // 全局优化预备：分析指针 + 指针移动消除 + 变量值分析
    void prepareForGlobal();

    // pass 的统一执行入口：维护分析有效性与脏块集合，并按需 dump / 校验。
    // fn 返回 false 时约定「什么都没改」，此时不失效任何分析。
    bool runPass(std::string_view name, const PassEffect& effect, const std::function<bool()>& fn);

    // ---- 优化流水线：三个阶段，见 optimize() 上方注释 ----
    void runLocalStages(bool isStrict, bool printInfo);
    void simplifyBlocks(bool printInfo);
    void runHoistStage(bool isStrict, bool printInfo);
    void runGlobalStages(bool isStrict, bool printInfo);
    void runGlobalFixpoint(bool isStrict, bool printInfo);
    void compactVarOffsets();
    void shiftInitOffsetForNegativeOffsets();

    [[nodiscard]] bool analysisValid(AnalysisId id) const {
        return m_validAnalyses.test(static_cast<std::size_t>(id));
    }

    void markAnalysisValid(AnalysisId id) {
        m_validAnalyses.set(static_cast<std::size_t>(id));
    }

    // 各 pass 的包装，提供统一的名字（--print-info / --dump-ir=passes / --verify 共用）
    bool passEliminateLocalLoop(bool preAnalyzePtr, bool isStrict, bool printInfo);
    bool passHoistLocalLoopConstAssign(bool printInfo);
    bool passFoldJump(bool useGlobalInfo, bool printInfo);
    bool passGeOptimizeLocal(bool useGlobalInfo, bool printInfo);
    bool passEliminateAssignWithLiveVarInfo(bool printInfo);

    std::vector<std::unique_ptr<MidBasicBlock>> m_basicBlocks;
    int32_t m_minNegOffset;
    int32_t m_maxPosOffset;
    int32_t m_tapeSize;
    int32_t m_verifyErrors = 0;
    int32_t m_verifyErrorsAtFinal = 0;
    // 分析有效性：位为 1 表示结果仍然可用。
    // 这些分析会写入块上的缓存字段，所以它们和持有者都不是 const 的。
    AnalysisMask m_validAnalyses;
    // 上一轮变量值分析用到的变量集合。集合不变时可以复用 BlockVarValueInfo，
    // 省掉整轮 unordered_map 重建。
    std::unordered_set<int32_t> m_lastVarValueVars;

    void markAllDirty() const {
        for (auto& block : m_basicBlocks) {
            block->dirty = true;
        }
    }

    // 块被改写后：它自己、以及它的后继都要重跑块内优化。
    // 后继是必须的——getZeroVarInfoLocally 要看前驱末尾的条件跳转。
    static void markDirtyWithSuccessors(MidBasicBlock* block) {
        block->dirty = true;
        if (block->succN != nullptr) {
            block->succN->dirty = true;
        }
        if (block->succY != nullptr) {
            block->succY->dirty = true;
        }
    }
};


#endif //YETANOTHERNEOBRAINFUCKCOMPILER_CODE_OPTIMIZER_H
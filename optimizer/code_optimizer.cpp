#include "code_optimizer.h"

#include <iostream>
#include <algorithm>
#include <deque>
#include <unordered_map>
#include <set>
#include <unordered_set>

#include "cfg_debug.h"
#include "../debug.h"

using OpType = Quadruple::OpType;
using ArgType = QuadrupleArg::ArgType;

CodeOptimizer::CodeOptimizer(const std::vector<Quadruple>& codes, int32_t tapeSize) : m_minNegOffset(0), m_maxPosOffset(0), m_tapeSize(tapeSize) {
    // 划分基本块
    std::set<int32_t> splits;
    for (int32_t i = 0, size = codes.size(); i < size; i++) {
        if (auto& code = codes[i]; code.type == OpType::JMP || code.type == OpType::JNZ || code.type == OpType::JZ) {
            splits.insert(i + 1);   // 跳转语句的下一条是新基本块的起始
            splits.insert(code.arg2.value); // 跳转语句的目的地是新基本块的起始
        }
    }

    // 构造基本块
    int32_t begin = 0;
    for (int32_t end : splits) {
        m_basicBlocks.push_back(std::make_unique<MidBasicBlock>(std::vector(codes.cbegin() + begin, codes.cbegin() + end)));
        begin = end;
    }
    m_basicBlocks.push_back(std::make_unique<MidBasicBlock>(std::vector(codes.cbegin() + begin, codes.cend())));

    // 跳转到哪个基本块
    std::unordered_map<int32_t, MidBasicBlock*> destToBasicBlock;
    begin = 0;
    auto it2 = m_basicBlocks.begin();
    for (auto it1 = splits.cbegin(), end = splits.cend(); it1 != end; ++it2, ++it1) {
        destToBasicBlock[begin] = it2->get();
        begin = *it1;
    }
    destToBasicBlock[begin] = it2->get();

    // 连连看
    for (int32_t j = 0, size = m_basicBlocks.size(); j < size; j++) {
        auto* block = m_basicBlocks[j].get();
        auto& lastCode = block->irs.back();   // 这时 codes 不可能为空
        if (lastCode.type == OpType::JNZ || lastCode.type == OpType::JZ) {
            // 保留 JZ 和 JNZ
            auto* jmpTo = destToBasicBlock[lastCode.arg2.value];
            block->succY = jmpTo;
            jmpTo->preds.push_back(block);
        } else if (lastCode.type == OpType::JMP) {
            // 基本块内不需要 JMP 语句
            auto* jmpTo = destToBasicBlock[lastCode.arg2.value];
            block->succN = jmpTo;
            jmpTo->preds.push_back(block);
            block->irs.pop_back();
        }
        if (lastCode.type != OpType::JMP && j < size - 1) {
            auto* next = m_basicBlocks[j + 1].get();
            block->succN = next;
            next->preds.push_back(block);
        }
    }

    // 最后一个基本块，只保留最后一个副作用前的代码（把 halt 也干了）
    auto& lastBlock = m_basicBlocks.back()->irs;
    auto codesEndIt = std::find_if(lastBlock.rbegin(), lastBlock.rend(), [](auto &code) {
        return code.type == OpType::GETCHAR || code.type == OpType::PUTCHAR;
    }).base();
    lastBlock.erase(codesEndIt, lastBlock.end());
}

int32_t CodeOptimizer::getTapeSize() const {
    // 饱和处理
    int64_t finalTapeSize = static_cast<int64_t>(m_tapeSize) + static_cast<int64_t>(m_maxPosOffset) - static_cast<int64_t>(m_minNegOffset);
    return finalTapeSize > std::numeric_limits<int32_t>::max() ? std::numeric_limits<int32_t>::max() : static_cast<int32_t>(finalTapeSize);
}

int32_t CodeOptimizer::countIRs() const {
    int32_t cnt = 0;
    for (auto& block : m_basicBlocks) {
        cnt += block->irs.size();
    }
    return cnt;
}

void CodeOptimizer::optimize(OptimizationLevel level, bool printInfo) {
    if (level == OptimizationLevel::O0) {
        return;
    }
    const bool isStrict = level == OptimizationLevel::O_STRICT;
    if (printInfo) {
        std::cout << "numIR = " << countIRs() << '\n';
    }

    // 三个阶段顺序固定，改变顺序会改变产物：
    //   一、块内化简（含单块循环消除），本地信息即可完成；
    //   二、循环内常量赋值外提，会改变 CFG；
    //   三、指针位置全局可定时启用全局值信息，并做变量紧凑化。
    runLocalStages(isStrict, printInfo);
    runHoistStage(isStrict, printInfo);
    runGlobalStages(isStrict, printInfo);
}

// 阶段一：单块循环消除 + 块内化简，一直跑到没有新循环可消除。
void CodeOptimizer::runLocalStages(bool isStrict, bool printInfo) {
    // 先无条件跑一轮，让 stage:local-loop 这个检查点至少能看到一次化简结果
    passEliminateLocalLoop(true, isStrict, printInfo);
    simplifyBlocks(printInfo);
    checkpoint("stage:local-loop");

    while (passEliminateLocalLoop(true, isStrict, printInfo)) {
        simplifyBlocks(printInfo);
    }
    checkpoint("stage:local-fixpoint");
}

// 一次块内化简：通用优化 → 折叠跳转 → （折叠成功就再优化一次）
void CodeOptimizer::simplifyBlocks(bool printInfo) {
    passGeOptimizeLocal(false, printInfo);
    if (passFoldJump(false, printInfo)) {
        passGeOptimizeLocal(false, printInfo);
    }
}

// 阶段二：把循环内的常量赋值外提。每次外提都会改 CFG，所以后面要重新化简。
void CodeOptimizer::runHoistStage(bool isStrict, bool printInfo) {
    while (passHoistLocalLoopConstAssign(printInfo)) {
        passGeOptimizeLocal(false, printInfo);
        if (passEliminateLocalLoop(true, isStrict, printInfo)) {
            passGeOptimizeLocal(false, printInfo);
        }
        if (passFoldJump(false, printInfo)) {
            passGeOptimizeLocal(false, printInfo);
        }
    }
    checkpoint("stage:hoist");
}

// 阶段三：全局信息阶段。只有在有块的指针位置可确定时才有意义，
// 全部可确定时还能再做死赋值消除和变量紧凑化。
void CodeOptimizer::runGlobalStages(bool isStrict, bool printInfo) {
    passGeOptimizeLocal(true, printInfo);
    checkpoint("stage:global-local");

    const bool anyPtrKnown = std::ranges::any_of(
        m_basicBlocks, [](auto& block) { return !block->isPtrStateUnknown(); });
    if (!anyPtrKnown) {
        checkpoint("stage:done");
        return;
    }

    runGlobalFixpoint(isStrict, printInfo);

    const bool allPtrKnown = std::ranges::all_of(
        m_basicBlocks, [](auto& block) { return !block->isPtrStateUnknown(); });
    if (allPtrKnown) {
        while (passEliminateAssignWithLiveVarInfo(printInfo)) {
            passGeOptimizeLocal(true, printInfo);
            runGlobalFixpoint(isStrict, printInfo);
        }
        compactVarOffsets();
    } else {
        shiftInitOffsetForNegativeOffsets();
    }
    checkpoint("stage:done");
}

// 三个全局 pass 的定点：任一 pass 有改动就重跑全局块内优化。
void CodeOptimizer::runGlobalFixpoint(bool isStrict, bool printInfo) {
    while (true) {
        bool changed = false;
        if (passEliminateLocalLoop(false, isStrict, printInfo)) {
            passGeOptimizeLocal(true, printInfo);
            changed = true;
        }
        if (passHoistLocalLoopConstAssign(printInfo)) {
            passGeOptimizeLocal(true, printInfo);
            changed = true;
        }
        if (passFoldJump(true, printInfo)) {
            passGeOptimizeLocal(true, printInfo);
            changed = true;
        }
        if (!changed) {
            break;
        }
    }
}

// 把所有块用到的 offset 重映射成连续下标，磁带大小随之收缩。
// 这就是「优化开启且指针位置全局可定时 -mmem-size 被忽略」的原因。
void CodeOptimizer::compactVarOffsets() {
    std::set<int32_t> vars;
    for (auto& block : m_basicBlocks) {
        auto blockVars = block->getVars();
        vars.insert(blockVars.begin(), blockVars.end());
    }
    int32_t nextId = 0;
    std::unordered_map<int32_t, int32_t> varMap;
    for (int32_t offset : vars) {
        varMap[offset] = nextId;
        nextId++;
    }
    for (auto& block : m_basicBlocks) {
        block->renameVar(varMap);
    }
    m_tapeSize = nextId;
}

// 指针位置不是全部确定时，把最小的负 offset 记下来，供生成器平移初始指针。
void CodeOptimizer::shiftInitOffsetForNegativeOffsets() {
    int32_t minNeg = 0;
    for (auto& block : m_basicBlocks) {
        if (block->isPtrStateUnknown()) {
            continue;
        }
        for (int32_t offset : block->getVars()) {
            minNeg = std::min(minNeg, offset);
        }
    }
    m_minNegOffset = std::min(m_minNegOffset, minNeg);
}

int32_t CodeOptimizer::checkInnermostBalancedLoop() const {
    int32_t cnt = 0;
    for (auto& block : m_basicBlocks) {
        if (block->calcDelta() == 0 && (block->succY == block.get() || block->succN == block.get())) {
            std::cout << *block.get() << "\n\n";
            cnt++;
        }
    }
    return cnt;
}

void CodeOptimizer::printBasicBlocks() const {
    dumpCFG("printBasicBlocks", m_basicBlocks);
}

void CodeOptimizer::printBasicBlocksBrief() const {
    dumpCFG("printBasicBlocksBrief", m_basicBlocks, /*withInsts=*/false);
}

void CodeOptimizer::checkpoint(std::string_view stage) {
    if (dbg::wantStage(stage)) {
        dumpCFG(stage, m_basicBlocks);
    }
    if (dbg::wantVerify()) {
        const int32_t errors = verifyCFG(stage, m_basicBlocks);
        m_verifyErrors += errors;
        if (stage == "final") {
            m_verifyErrorsAtFinal += errors;
        }
    }
}

// ---------------------------------------------------------------------------
// pass 框架
// ---------------------------------------------------------------------------

namespace {
    AnalysisMask allAnalyses() {
        AnalysisMask mask;
        mask.set();
        return mask;
    }
}

bool CodeOptimizer::runPass(std::string_view name, const PassEffect& effect, const std::function<bool()>& fn) {
    const bool changed = fn();

    if (changed) {
        // 约定：pass 返回 false 表示什么都没改，此时所有分析继续有效。
        m_validAnalyses &= ~effect.invalidates;
        // 精确标记的 pass 自己置 block->dirty；其余 pass 只能整体标脏。
        if (!effect.managesDirty) {
            markAllDirty();
        }
    }

    if (dbg::options().passes || dbg::wantVerify()) {
        std::string stage = "pass:";
        stage += name;
        if (dbg::options().passes) {
            dumpCFG(stage, m_basicBlocks);
        }
        if (dbg::wantVerify()) {
            // 逐 pass 的违例只累计到 m_verifyErrors，不当作致命错误；只有 final 才致命。
            m_verifyErrors += verifyCFG(stage, m_basicBlocks);
        }
    }
    return changed;
}

bool CodeOptimizer::passEliminateLocalLoop(bool preAnalyzePtr, bool isStrict, bool printInfo) {
    return runPass("eliminateLocalLoop", PassEffect{allAnalyses(), true, /*managesDirty=*/true},
                   [&] { return eliminateLocalLoop(preAnalyzePtr, isStrict, printInfo); });
}

bool CodeOptimizer::passHoistLocalLoopConstAssign(bool printInfo) {
    return runPass("hoistLocalLoopConstAssign", PassEffect{allAnalyses(), true, /*managesDirty=*/true},
                   [&] { return hoistLocalLoopConstAssign(printInfo); });
}

bool CodeOptimizer::passFoldJump(bool useGlobalInfo, bool printInfo) {
    return runPass(useGlobalInfo ? "foldJump(true)" : "foldJump(false)",
                   PassEffect{allAnalyses(), true, /*managesDirty=*/true},
                   [&] { return foldJump(useGlobalInfo, printInfo); });
}

bool CodeOptimizer::passGeOptimizeLocal(bool useGlobalInfo, bool printInfo) {
    return runPass(useGlobalInfo ? "geOptimizeLocal(true)" : "geOptimizeLocal(false)",
                   PassEffect{allAnalyses(), true, /*managesDirty=*/true},
                   [&] { return geOptimizeLocalPass(useGlobalInfo, printInfo); });
}

bool CodeOptimizer::passEliminateAssignWithLiveVarInfo(bool printInfo) {
    return runPass("eliminateAssignWithLiveVarInfo", PassEffect{allAnalyses(), true},
                   [&] { return eliminateAssignWithLiveVarInfo(printInfo); });
}

bool CodeOptimizer::mergeAndRemove() {
    // 先 bfs 移除不可达基本块
    std::unordered_set<MidBasicBlock*> vis;
    std::deque<MidBasicBlock*> queue;
    queue.push_back(m_basicBlocks.front().get());
    vis.insert(m_basicBlocks.front().get());
    while (!queue.empty()) {
        auto* block = queue.front();
        queue.pop_front();
        if (block->succY != nullptr && !vis.contains(block->succY)) {
            vis.insert(block->succY);
            queue.push_back(block->succY);
        }
        if (block->succN != nullptr && !vis.contains(block->succN)) {
            vis.insert(block->succN);
            queue.push_back(block->succN);
        }
    }
    std::erase_if(m_basicBlocks, [&vis](auto& basicBlock) { return !vis.contains(basicBlock.get()); });
    for (auto& block : m_basicBlocks) {
        std::erase_if(block->preds, [&vis](auto* pred) { return !vis.contains(pred); });
    }

    bool mergeChange = false;
    while (true) {
        bool change = false;
        // 合并基本块
        vis.clear();
        for (auto it = m_basicBlocks.begin(), end = m_basicBlocks.end(); it != end;) {
            if (auto* block = it->get(); !vis.contains(block) && block->irs.empty() && block->succN != nullptr && block->succN != block) {
                // 当前基本块为空（一定只有一个出口 succN）且不是自循环，修改指针绕过该基本块
                auto* succ = block->succN;
                std::erase(succ->preds, block);
                for (auto* pred : block->preds) {
                    if (std::ranges::find(succ->preds, pred) == succ->preds.end()) {
                        succ->preds.push_back(pred);
                    }
                    if (pred->succY == block) {
                        pred->succY = succ;
                    }
                    if (pred->succN == block) {
                        pred->succN = succ;
                    }
                }
                for (auto* pred : block->preds) {
                    if (pred->succN == block && pred->succY == block) {
                        // jt 和 jf 挤到一起了，直接把条件跳转废掉
                        if (!pred->irs.empty() && (pred->irs.back().type == OpType::JNZ || pred->irs.back().type == OpType::JZ)) {
                            pred->irs.pop_back();
                        }
                        pred->succY = nullptr;
                    }
                }
                block->preds.clear();
                if (it == m_basicBlocks.begin()) {
                    // 如果这是第一个基本块，跟 succ 交换一下位置（succ 为入口点），迭代器不往前走
                    auto it1 = std::find_if(it, end, [succ](auto& pBlock) {
                        return pBlock.get() == succ;
                    });
                    std::swap(*it, *it1);
                } else {
                    ++it;
                }
                vis.insert(block);
                change = true;
            } else if (!vis.contains(block) && block->succN != nullptr && block->succY == nullptr && block->succN->preds.size() == 1) {
                // 当前基本块只有一个后继，后继基本块只有一个前驱，可以合并
                auto* succ = block->succN;
                int32_t maxTempId = MidBasicBlock::findAvailableTempId(block->irs);
                for (auto &code : succ->irs) {
                    if (code.arg1.type == ArgType::BB_TEMP) {
                        code.arg1.value += maxTempId;
                    }
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        code.arg2.value += maxTempId;
                    }
                    if (code.result.type == ArgType::BB_TEMP) {
                        code.result.value += maxTempId;
                    }
                }
                block->irs.insert(block->irs.end(), succ->irs.begin(), succ->irs.end());
                // 被合并进来的块内容变了，需要重新跑块内优化（连同它的后继）
                markDirtyWithSuccessors(block);
                if (succ->succN != nullptr) {
                    std::erase(succ->succN->preds, succ);
                    if (std::ranges::find(succ->succN->preds, block) == succ->succN->preds.end()) {
                        succ->succN->preds.push_back(block);
                    }
                }
                if (succ->succY != nullptr) {
                    std::erase(succ->succY->preds, succ);
                    if (std::ranges::find(succ->succY->preds, block) == succ->succY->preds.end()) {
                        succ->succY->preds.push_back(block);
                    }
                }
                block->succN = succ->succN;
                block->succY = succ->succY;
                succ->preds.clear();
                vis.insert(succ);
                change = true;
            } else {
                vis.insert(block);
                ++it;
            }
        }

        // 如果没有合并，退出
        if (!change) {
            return mergeChange;
        }
        mergeChange = true;

        // bfs 移除不可达基本块
        vis.clear();
        queue.clear();
        queue.push_back(m_basicBlocks.front().get());
        vis.insert(m_basicBlocks.front().get());
        while (!queue.empty()) {
            auto* block = queue.front();
            queue.pop_front();
            if (block->succY != nullptr && !vis.contains(block->succY)) {
                queue.push_back(block->succY);
                vis.insert(block->succY);
            }
            if (block->succN != nullptr && !vis.contains(block->succN)) {
                queue.push_back(block->succN);
                vis.insert(block->succN);
            }
        }
        std::erase_if(m_basicBlocks, [&vis](auto& basicBlock) {
            return !vis.contains(basicBlock.get());
        });
        for (auto& block : m_basicBlocks) {
            const auto predsBefore = block->preds.size();
            std::erase_if(block->preds, [&vis](auto* pred) {
                return !vis.contains(pred);
            });
            if (block->preds.size() != predsBefore) {
                // 前驱集合变了，getZeroVarInfoLocally 的结果可能跟着变
                markDirtyWithSuccessors(block.get());
            }
        }
    }
}

bool CodeOptimizer::eliminateLocalLoop(bool preAnalyzePtr, bool isStrict, bool printInfo) {
    if (preAnalyzePtr) {
        analyzePtrFlow();
    }
    bool hasOptimization = false;
    for (auto& block : m_basicBlocks) {
        if (auto [isChange, minNeg, maxPos] = block->eliminateLoop(isStrict); isChange) {
            hasOptimization = true;
            // 这个块被改写了，只有它需要重新跑块内优化
            markDirtyWithSuccessors(block.get());
            m_minNegOffset = std::min(m_minNegOffset, minNeg);
            m_maxPosOffset = std::max(m_maxPosOffset, maxPos);
        }
    }
    if (hasOptimization) {
        mergeAndRemove();
        if (printInfo) {
            std::cout << "eliminateLocalLoop: success, numIR = " << countIRs() << "\n";
        }
    } else if (printInfo) {
        std::cout << "eliminateLocalLoop: no change\n";
    }
    return hasOptimization;
}

bool CodeOptimizer::hoistLocalLoopConstAssign(bool printInfo) {
    std::vector<std::unique_ptr<MidBasicBlock>> newBlocks;
    for (auto& block : m_basicBlocks) {
        if (auto newBlock = block->hoistLoopConstAssign(); newBlock != nullptr) {
            markDirtyWithSuccessors(block.get());    // 外提改写了循环体；新块默认就是脏的
            newBlocks.push_back(std::move(newBlock));
        }
    }
    bool hasOptimization = !newBlocks.empty();
    m_basicBlocks.insert(m_basicBlocks.end(), std::make_move_iterator(newBlocks.begin()), std::make_move_iterator(newBlocks.end()));
    if (hasOptimization) {
        mergeAndRemove();
        if (printInfo) {
            std::cout << "hoistLocalLoopConstAssign: success, numIR = " << countIRs() << "\n";
        }
    } else if (printInfo) {
        std::cout << "hoistLocalLoopConstAssign: no change\n";
    }
    return hasOptimization;
}

bool CodeOptimizer::foldJump(bool useGlobalInfo, bool printInfo) {
    // 两个旧版本（foldJumpBeforeGlobal / foldJumpWithGlobalInfo）的差异只有三处，
    // 全部由 useGlobalInfo 决定，剩下的是同一段代码。
    const char* const passName = useGlobalInfo ? "foldJumpAfterGlobal" : "foldJumpBeforeGlobal";
    bool hasOptimization = false;
    for (auto& block : m_basicBlocks) {
        if ((block->varValueInfo != nullptr) != useGlobalInfo) {
            continue;
        }
        if (block->irs.size() != 1) {
            continue;
        }
        const auto blockJump = block->irs.back().type;
        if (blockJump != OpType::JZ && blockJump != OpType::JNZ) {
            continue;
        }

        std::unordered_set<MidBasicBlock*> mergedBlocks;
        for (auto* pred : block->preds) {
            const bool predIsCondJmp = !pred->irs.empty()
                && (pred->irs.back().type == OpType::JZ || pred->irs.back().type == OpType::JNZ);
            if (!predIsCondJmp) {
                // 前驱不是条件跳转，直接内联
                pred->irs.push_back(block->irs.back());
                pred->succY = block->succY;
                pred->succN = block->succN;
                block->succY->preds.push_back(pred);
                block->succN->preds.push_back(pred);
                mergedBlocks.insert(pred);
                continue;
            }
            if (useGlobalInfo && !(pred->irs.back().arg1 == block->irs.back().arg1)) {
                // 进入全局信息阶段后测试变量可能已被改写，必须确认是同一个才能内联
                continue;
            }
            const bool samePolarity = (pred->irs.back().type == OpType::JZ) == (blockJump == OpType::JZ);
            if (samePolarity) {
                if (pred->succN == block.get()) {
                    pred->succN = block->succN;
                    block->succN->preds.push_back(pred);
                } else {
                    pred->succY = block->succY;
                    block->succY->preds.push_back(pred);
                }
            } else {
                if (pred->succN == block.get()) {
                    pred->succN = block->succY;
                    block->succY->preds.push_back(pred);
                } else {
                    pred->succY = block->succN;
                    block->succN->preds.push_back(pred);
                }
            }
            mergedBlocks.insert(pred);
        }

        // 被内联的前驱 IR / 出边变了，只有它们需要重新跑块内优化
        for (auto* pred : mergedBlocks) {
            pred->dirty = true;
        }

        if (useGlobalInfo) {
            // 只摘掉真正被内联的前驱；一个都没内联时视为本块没变化
            if (mergedBlocks.empty()) {
                continue;
            }
            std::erase_if(block->preds, [&mergedBlocks](auto* pred) { return mergedBlocks.contains(pred); });
            hasOptimization = true;
        } else {
            // 早期阶段所有前驱都会被内联，整块清空
            block->preds.clear();
            hasOptimization = true;
        }
    }
    if (hasOptimization) {
        mergeAndRemove();
        if (printInfo) {
            std::cout << passName << ": success, numIR = " << countIRs() << "\n";
        }
    } else if (printInfo) {
        std::cout << passName << ": no change\n";
    }
    return hasOptimization;
}

bool CodeOptimizer::geOptimizeLocalPass(bool useGlobalInfo, bool printInfo) {
    // 两个旧版本（geOptimizeLocal / geOptimizeLocalWithGlobalInfo）的差别只有：
    // 处理哪些块、值信息从哪来、合并后要不要重算全局信息。骨架完全一致。
    const char* const passName = useGlobalInfo ? "optimizeLocalWithGlobalInfo" : "optimizeLocal";
    if (useGlobalInfo) {
        // prepareForGlobal 会把指针移动折进偏移量，整张图的 IR 都可能变，只能全量处理
        prepareForGlobal();
        markAllDirty();
    }
    bool hasOptimization = false;
    while (true) {
        bool changed = false;
        std::vector<MidBasicBlock*> changedBlocks;
        for (auto& blockPtr : m_basicBlocks) {
            auto* block = blockPtr.get();
            // 只处理脏块；处理过就清掉标志，下一轮再看它是不是又被改了
            if (!block->dirty) {
                continue;
            }
            if ((block->varValueInfo != nullptr) != useGlobalInfo) {
                continue;
            }
            block->dirty = false;
            bool changedThisBlock = false;
            if (useGlobalInfo) {
                changedThisBlock = block->generalOptimize(block->varValueInfo->in);
            } else if (!block->preds.empty()) {
                // 非入口块：只能靠前驱的条件跳转判断零值
                changedThisBlock = block->generalOptimize({{0, block->getZeroVarInfoLocally()}});
            } else {
                // 入口块：所有变量初始为 0
                std::unordered_map<int32_t, VarValueInfo> info;
                for (auto offset : block->getVars()) {
                    info[offset] = VarValueInfo(VarValueInfo::exact(0));
                }
                changedThisBlock = block->generalOptimize(info);
            }
            if (changedThisBlock) {
                changed = true;
                changedBlocks.push_back(block);
            }
        }
        if (!changed) {
            if (printInfo) {
                if (hasOptimization) {
                    std::cout << passName << ": success, numIR = " << countIRs() << "\n";
                } else {
                    std::cout << passName << ": no change\n";
                }
            }
            return hasOptimization;
        }
        hasOptimization = true;
        if (mergeAndRemove()) {
            // 合块/删块后 changedBlocks 里的指针可能已经失效，只能整体标脏
            markAllDirty();
            if (useGlobalInfo) {
                prepareForGlobal();
            }
        } else {
            for (auto* block : changedBlocks) {
                markDirtyWithSuccessors(block);
            }
        }
    }
}

bool CodeOptimizer::eliminateAssignWithLiveVarInfo(bool printInfo) {
    bool hasOptimization = false;
    analyzeLiveVar();
    for (auto& block : m_basicBlocks) {
        if (block->eliminateUselessVarAssign()) {
            hasOptimization = true;
        }
    }
    if (hasOptimization) {
        mergeAndRemove();
        if (printInfo) {
            std::cout << "eliminateAssignAfterLiveVar: success, numIR = " << countIRs() << "\n";
        }
    } else if (printInfo) {
        std::cout << "eliminateAssignAfterLiveVar: no change\n";
    }
    return hasOptimization;
}

void CodeOptimizer::analyzePtrFlow() {
    if (analysisValid(AnalysisId::PtrFlow)) {
        return;
    }
    // 初始化
    for (auto& block : m_basicBlocks) {
        block->calcDelta();
        block->ptrIn = PtrState::Unreachable();
        block->ptrOut = PtrState::Unreachable();
    }
    m_basicBlocks[0]->ptrIn = PtrState::Constant(0);

    // Worklist 迭代
    std::deque<MidBasicBlock*> worklist;
    worklist.push_back(m_basicBlocks[0].get());

    while (!worklist.empty()) {
        MidBasicBlock* block = worklist.front();
        worklist.pop_front();

        // 计算 Out
        if (block->ptrIn.isUnreachable) {
            block->ptrOut = PtrState::Unreachable();
        } else if (block->ptrIn.isUnknown) {
            block->ptrOut = PtrState::Unknown();
        } else {
            block->ptrOut = PtrState::Constant(block->ptrIn.pos + block->ptrDelta);
        }

        // 传播到后继
        if (block->succN != nullptr) {
            auto oldIn = block->succN->ptrIn;
            auto newIn = oldIn.join(block->ptrOut);
            if (oldIn != newIn) {
                // 状态发生了变化
                block->succN->ptrIn = newIn;
                worklist.push_back(block->succN);
            }
        }
        if (block->succY != nullptr) {
            auto oldIn = block->succY->ptrIn;
            auto newIn = oldIn.join(block->ptrOut);
            if (oldIn != newIn) {
                // 状态发生了变化
                block->succY->ptrIn = newIn;
                worklist.push_back(block->succY);
            }
        }
    }
    markAnalysisValid(AnalysisId::PtrFlow);
}

void CodeOptimizer::analyzeVarValueFlow() {
    if (analysisValid(AnalysisId::VarValue)) {
        return;
    }
    std::unordered_set<int32_t> vars;
    std::unordered_set<MidBasicBlock*> blocks;
    for (auto &block : m_basicBlocks) {
        if (block->isPtrStateUnknown()) {
            continue;
        }
        auto blockVars = block->getVars();
        vars.insert(blockVars.begin(), blockVars.end());
        blocks.insert(block.get());
    }
    // 其实有个bug，即出指针可预测区间的跳转变量不应该出现在vars里（前面可能有个ptr_add），但是鉴于out不会传播给不可预测基本块，安全

    if (blocks.empty()) {
        markAnalysisValid(AnalysisId::VarValue);
        return;
    }

    // 初始化：变量集合没变时复用上一轮的对象，避免每轮重建所有块的 unordered_map。
    const bool reuse = (vars == m_lastVarValueVars);
    for (auto& block : blocks) {
        if (!reuse || block->varValueInfo == nullptr) {
            block->varValueInfo = std::make_unique<BlockVarValueInfo>(vars);
        } else {
            block->varValueInfo->resetValues();
        }
        block->calcVarChange();
    }
    if (!reuse) {
        m_lastVarValueVars = vars;
    }
    m_basicBlocks[0]->varValueInfo->setAllInZero();

    // Worklist 迭代
    std::deque<MidBasicBlock*> worklist;
    worklist.push_back(m_basicBlocks[0].get());

    while (!worklist.empty()) {
        MidBasicBlock* block = worklist.front();
        worklist.pop_front();

        // 计算 Out
        block->varValueInfo->calcOut();

        // 传播到后继
        if (block->succN != nullptr && blocks.contains(block->succN)) {
            if (block->succN->varValueInfo->join(*block->varValueInfo, false)) {
                // 状态发生了变化
                worklist.push_back(block->succN);
            }
        }
        if (block->succY != nullptr && blocks.contains(block->succY)) {
            if (block->succY->varValueInfo->join(*block->varValueInfo, true)) {
                // 状态发生了变化
                worklist.push_back(block->succY);
            }
        }
    }
    markAnalysisValid(AnalysisId::VarValue);
}

void CodeOptimizer::analyzeLiveVar() {
    if (analysisValid(AnalysisId::LiveVar)) {
        return;
    }
    // 默认构造所有的 in 都是空集
    for (auto& block : m_basicBlocks) {
        block->liveVarInfo = std::make_unique<BlockLiveVarInfo>();
        block->calcUseDef();
    }
    bool change;
    do {
        change = false;
        for (auto& block : std::views::reverse(m_basicBlocks)) {
            block->liveVarInfo->out.clear();
            if (block->succN != nullptr) {
                block->liveVarInfo->out.insert(block->succN->liveVarInfo->in.begin(), block->succN->liveVarInfo->in.end());
            }
            if (block->succY != nullptr) {
                block->liveVarInfo->out.insert(block->succY->liveVarInfo->in.begin(), block->succY->liveVarInfo->in.end());
            }
            std::unordered_set<int32_t> newIn;
            for (int32_t var : block->liveVarInfo->out) {
                if (!block->liveVarInfo->def.contains(var)) {
                    newIn.insert(var);
                }
            }
            newIn.insert(block->liveVarInfo->use.begin(), block->liveVarInfo->use.end());
            if (newIn != block->liveVarInfo->in) {
                change = true;
                block->liveVarInfo->in = std::move(newIn);
            }
        }
    } while (change);
    markAnalysisValid(AnalysisId::LiveVar);
}

void CodeOptimizer::prepareForGlobal() {
    analyzePtrFlow();
    std::vector<std::unique_ptr<MidBasicBlock>> newBlocks;
    for (auto& block : m_basicBlocks) {
        if (auto newBlock = block->eliminatePtrMove(); newBlock != nullptr) {
            newBlocks.push_back(std::move(newBlock));
        }
    }
    // 这里不需要尝试合并，因为新增的基本块是条件跳转某后继指针位置爆了然后挪指针的，肯定合并不了
    m_basicBlocks.insert(m_basicBlocks.end(), std::make_move_iterator(newBlocks.begin()), std::make_move_iterator(newBlocks.end()));
    // eliminatePtrMove 即使没拆出新块也会改写块内 IR（把指针移动折进基址+偏移），所以分析结果
    // 一律作废。ptrIn/ptrOut 的数据本身仍然可用（值流分析直接读它们），这里清的只是"有效"标记。
    m_validAnalyses.reset();
    markAllDirty();
    analyzeVarValueFlow();
}

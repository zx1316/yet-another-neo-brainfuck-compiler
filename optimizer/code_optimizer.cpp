#include "code_optimizer.h"

#include <iostream>
#include <algorithm>
#include <deque>
#include <unordered_map>
#include <set>
#include <unordered_set>

using OpType = Quadruple::OpType;
using ArgType = QuadrupleArg::ArgType;

CodeOptimizer::CodeOptimizer(const std::vector<Quadruple>& codes, int32_t tapeSize) : m_minNegOffset(0), m_maxPosOffset(0), m_tapeSize(tapeSize) {
    // 划分基本块
    std::set<int32_t> splits;
    for (int32_t i = 0, size = codes.size(); i < size; i++) {
        auto& code = codes[i];
        if (code.type == OpType::JMP || code.type == OpType::JNZ || code.type == OpType::JZ) {
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

void CodeOptimizer::optimize(int32_t optimizeLevel, bool isStrict, bool printInfo) {
    if (optimizeLevel < 1) {
        return;
    }
    if (printInfo) {
        std::cout << "numIR = " << countIRs() << '\n';
    }

    // 优化遍
    // 我已经不知道我在写什么了
    // 说实话写得挺烂的，性能也不行，会做很多无用功，我摆烂了
    eliminateLocalLoop(true, isStrict, printInfo);
    geOptimizeLocal(printInfo);
    if (foldJumpBeforeGlobal(printInfo)) {
        geOptimizeLocal(printInfo);
    }

    while (true) {
        if (!eliminateLocalLoop(true, isStrict, printInfo)) {
            break;
        }
        geOptimizeLocal(printInfo);
        if (foldJumpBeforeGlobal(printInfo)) {
            geOptimizeLocal(printInfo);
        }
    }

    while (true) {
        if (!hoistLocalLoopConstAssign(printInfo)) {
            break;
        }
        geOptimizeLocal(printInfo);
        if (eliminateLocalLoop(true, isStrict, printInfo)) {
            geOptimizeLocal(printInfo);
        }
        if (foldJumpBeforeGlobal(printInfo)) {
            geOptimizeLocal(printInfo);
        }
    }

    geOptimizeLocalWithGlobalInfo(printInfo);
    if (std::ranges::any_of(m_basicBlocks, [](auto &block) { return !block->isPtrStateUnknown(); })) {
        while (true) {
            bool flag = false;
            if (eliminateLocalLoop(false, isStrict, printInfo)) {
                geOptimizeLocalWithGlobalInfo(printInfo);
                flag = true;
            }
            if (hoistLocalLoopConstAssign(printInfo)) {
                geOptimizeLocalWithGlobalInfo(printInfo);
                flag = true;
            }
            if (foldJumpWithGlobalInfo(printInfo)) {
                geOptimizeLocalWithGlobalInfo(printInfo);
                flag = true;
            }
            if (!flag) break;
        }

        if (std::ranges::all_of(m_basicBlocks, [](auto &block) { return !block->isPtrStateUnknown(); })) {
            while (true) {
                if (!eliminateAssignWithLiveVarInfo(printInfo)) {
                    break;
                }
                geOptimizeLocalWithGlobalInfo(printInfo);
                while (true) {
                    bool flag = false;
                    if (eliminateLocalLoop(false, isStrict, printInfo)) {
                        geOptimizeLocalWithGlobalInfo(printInfo);
                        flag = true;
                    }
                    if (hoistLocalLoopConstAssign(printInfo)) {
                        geOptimizeLocalWithGlobalInfo(printInfo);
                        flag = true;
                    }
                    if (foldJumpWithGlobalInfo(printInfo)) {
                        geOptimizeLocalWithGlobalInfo(printInfo);
                        flag = true;
                    }
                    if (!flag) break;
                }
            }
            // 紧凑化处理 ptr offset
            std::set<int32_t> vars;
            for (auto& block : m_basicBlocks) {
                auto vars1 = block->getVars();
                vars.insert(vars1.begin(), vars1.end());
            }
            int32_t i = 0;
            std::unordered_map<int32_t, int32_t> varMap;
            for (int32_t offset : vars) {
                varMap[offset] = i;
                i++;
            }
            for (auto& block : m_basicBlocks) {
                block->renameVar(varMap);
            }
            m_tapeSize = i;
        } else {
            // 查找有没有负下标的 ptr offset，如果有，初始化的指针位置平移一下
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
    }
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
    for (auto& block : m_basicBlocks) {
        std::cout << *block << "\n\n";
    }
}

void CodeOptimizer::printBasicBlocksBrief() const {
    for (auto& block : m_basicBlocks) {
        block->printBrief(std::cout);
        std::cout << "\n\n";
    }
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
            std::erase_if(block->preds, [&vis](auto* pred) {
                return !vis.contains(pred);
            });
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
        auto newBlock = block->hoistLoopConstAssign();
        if (newBlock != nullptr) {
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

bool CodeOptimizer::foldJumpBeforeGlobal(bool printInfo) {
    bool hasOptimization = false;
    for (auto& block : m_basicBlocks) {
        if (block->varValueInfo == nullptr && block->irs.size() == 1 && (block->irs.back().type == OpType::JZ || block->irs.back().type == OpType::JNZ)) {
            for (auto* pred : block->preds) {
                if (pred->irs.empty() || pred->irs.back().type != OpType::JZ && pred->irs.back().type != OpType::JNZ) {
                    pred->irs.push_back(block->irs.back());
                    pred->succY = block->succY;
                    pred->succN = block->succN;
                    block->succY->preds.push_back(pred);
                    block->succN->preds.push_back(pred);
                } else if (pred->irs.back().type == OpType::JZ && block->irs.back().type == OpType::JZ
                    || pred->irs.back().type == OpType::JNZ && block->irs.back().type == OpType::JNZ) {
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
            }
            block->preds.clear();
            hasOptimization = true;
        }
    }
    if (hasOptimization) {
        mergeAndRemove();
        if (printInfo) {
            std::cout << "foldJumpBeforeGlobal: success, numIR = " << countIRs() << "\n";
        }
    } else if (printInfo) {
        std::cout << "foldJumpBeforeGlobal: no change\n";
    }
    return hasOptimization;
}

bool CodeOptimizer::foldJumpWithGlobalInfo(bool printInfo) {
    bool hasOptimization = false;
    for (auto& block : m_basicBlocks) {
        if (block->varValueInfo != nullptr && block->irs.size() == 1 && (block->irs.back().type == OpType::JZ || block->irs.back().type == OpType::JNZ)) {
            std::unordered_set<MidBasicBlock *> mergedBlocks;
            for (auto *pred : block->preds) {
                if (pred->irs.empty() || pred->irs.back().type != OpType::JZ && pred->irs.back().type != OpType::JNZ) {
                    // 前驱不是条件跳转，直接内联
                    pred->irs.push_back(block->irs.back());
                    pred->succY = block->succY;
                    pred->succN = block->succN;
                    block->succY->preds.push_back(pred);
                    block->succN->preds.push_back(pred);
                    mergedBlocks.insert(pred);
                } else if (pred->irs.back().arg1 == block->irs.back().arg1) {
                    // 必须是同一个测试变量才能内联
                    if (pred->irs.back().type == OpType::JZ && block->irs.back().type == OpType::JZ
                        || pred->irs.back().type == OpType::JNZ && block->irs.back().type == OpType::JNZ) {
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
            }
            if (!mergedBlocks.empty()) {
                std::erase_if(block->preds, [&mergedBlocks](auto *pred) { return mergedBlocks.contains(pred); });
                hasOptimization = true;
            }
        }
    }
    if (hasOptimization) {
        mergeAndRemove();
        if (printInfo) {
            std::cout << "foldJumpAfterGlobal: success, numIR = " << countIRs() << "\n";
        }
    } else if (printInfo) {
        std::cout << "foldJumpAfterGlobal: no change\n";
    }
    return hasOptimization;
}

bool CodeOptimizer::geOptimizeLocal(bool printInfo) {
    bool hasOptimization = false;
    // 第一个基本块时扫描所有变量，设成0
    // 其他基本块的走局部零判定
    while (true) {
        bool hasOptimization1 = false;
        for (auto& block : m_basicBlocks) {
            // 如果这个块已经有全局信息了，再在这个分支里优化就是错误
            if (block->varValueInfo != nullptr) {
                continue;
            }
            if (!block->preds.empty()) {
                if (block->generalOptimize({{0, block->getZeroVarInfoLocally()}})) {
                    hasOptimization1 = true;
                }
            } else {
                std::unordered_map<int32_t, VarValueInfo> info;
                for (auto offset : block->getVars()) {
                    info[offset] = VarValueInfo(VarValueInfo::exact(0));
                }
                if (block->generalOptimize(info)) {
                    hasOptimization1 = true;
                }
            }
        }
        if (!hasOptimization1) {
            if (printInfo) {
                if (hasOptimization) {
                    std::cout << "optimizeLocal: success, numIR = " << countIRs() << "\n";
                } else {
                    std::cout << "optimizeLocal: no change\n";
                }
            }
            return hasOptimization;
        }
        hasOptimization = true;
        mergeAndRemove();
    }
}

bool CodeOptimizer::geOptimizeLocalWithGlobalInfo(bool printInfo) {
    prepareForGlobal();
    bool hasOptimization = false;
    while (true) {
        bool hasOptimization1 = false;
        for (auto& block : m_basicBlocks) {
            if (block->varValueInfo != nullptr && block->generalOptimize(block->varValueInfo->in)) {
                hasOptimization1 = true;
            }
        }
        if (!hasOptimization1) {
            if (printInfo) {
                if (hasOptimization) {
                    std::cout << "optimizeLocalWithGlobalInfo: success, numIR = " << countIRs() << "\n";
                } else {
                    std::cout << "optimizeLocalWithGlobalInfo: no change\n";
                }
            }
            return hasOptimization;
        }
        hasOptimization = true;
        if (mergeAndRemove()) {
            prepareForGlobal();
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

void CodeOptimizer::analyzePtrFlow() const {
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
}

void CodeOptimizer::analyzeVarValueFlow() const {
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
        return;
    }

    // 初始化
    for (auto& block : blocks) {
        block->varValueInfo = std::make_unique<BlockVarValueInfo>(vars);
        block->calcVarChange();
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
}

void CodeOptimizer::analyzeLiveVar() const {
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
}

void CodeOptimizer::prepareForGlobal() {
    analyzePtrFlow();
    std::vector<std::unique_ptr<MidBasicBlock>> newBlocks;
    for (auto& block : m_basicBlocks) {
        auto newBlock = block->eliminatePtrMove();
        if (newBlock != nullptr) {
            newBlocks.push_back(std::move(newBlock));
        }
    }
    // 这里不需要尝试合并，因为新增的基本块是条件跳转某后继指针位置爆了然后挪指针的，肯定合并不了
    m_basicBlocks.insert(m_basicBlocks.end(), std::make_move_iterator(newBlocks.begin()), std::make_move_iterator(newBlocks.end()));
    analyzeVarValueFlow();
}

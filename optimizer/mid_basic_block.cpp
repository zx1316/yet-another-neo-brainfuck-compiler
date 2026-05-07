#include "mid_basic_block.h"

#include <algorithm>
#include <map>
#include <ranges>
#include <unordered_map>
#include <set>
#include <unordered_set>

using OpType = Quadruple::OpType;
using ArgType = QuadrupleArg::ArgType;

GettersForInterpret MidBasicBlock::getGettersForInterpreter() {
    return {
        [](const void* block) -> const std::vector<Quadruple>& {
            return static_cast<const MidBasicBlock*>(block)->irs;
        },
        [](const void* block) {
            return static_cast<const void*>(static_cast<const MidBasicBlock*>(block)->succY);
        },
        [](const void* block) {
            return static_cast<const void*>(static_cast<const MidBasicBlock*>(block)->succN);
        }
    };
}

int32_t MidBasicBlock::calcDelta() {
    int32_t delta = 0;
    for (auto& code : irs) {
        if (code.type == OpType::PTR_ADD) {
            delta += code.arg1.value;
        }
    }
    ptrDelta = delta;
    return delta;
}

VarValueInfo MidBasicBlock::getZeroVarInfoLocally() const {
    if (preds.empty()) {
        return VarValueInfo::unknown();
    }
    VarValueInfo info = VarValueInfo::unreachable();
    for (auto* pred : preds) {
        if (pred->succY == nullptr) {
            return VarValueInfo::unknown();
        }
        if (pred->irs.back().type == OpType::JZ && pred->succY == this || pred->irs.back().type == OpType::JNZ && pred->succN == this) {
            info = info.join({VarValueInfo::InfoType::EXACT, 0});
        } else {
            info = info.join({VarValueInfo::InfoType::NOT_ZERO});
        }
    }
    return info;
}

std::unordered_set<int32_t> MidBasicBlock::getVars() const {
    std::unordered_set<int32_t> vars;
    for (auto &code : irs) {
        if (code.result.type == ArgType::PTR_OFFSET) {
            vars.insert(code.result.value);
        }
        if (code.arg1.type == ArgType::PTR_OFFSET) {
            vars.insert(code.arg1.value);
        }
        if (code.arg2.type == ArgType::PTR_OFFSET) {
            vars.insert(code.arg2.value);
        }
    }
    return vars;
}

std::tuple<bool, int32_t, int32_t> MidBasicBlock::eliminateLoop(bool isStrict) {
    // 需要是指针平衡的无副作用的最内层 while (*ptr) 循环
    // 进循环非 0 的测试放到后面做
    if (irs.empty() || irs.back().type != OpType::JNZ || succY != this || ptrDelta != 0) {
        return {false, 0, 0};
    }
    for (auto code : irs) {
        if (code.type == OpType::GETCHAR || code.type == OpType::PUTCHAR) {
            return {false, 0, 0};
        }
    }

    // 先消除指针移动，全部转换为基址 + 偏移量
    auto seq1 = getNoPtrMoveForm().first;

    // 循环不变量
    std::unordered_set<QuadrupleArg> constVars;
    for (auto& code : seq1) {
        if (code.arg1.type == ArgType::PTR_OFFSET) {
            constVars.insert(code.arg1);
        }
        if (code.arg2.type == ArgType::PTR_OFFSET) {
            constVars.insert(code.arg2);
        }
    }
    for (auto &code : seq1) {
        if (code.result.type == ArgType::PTR_OFFSET) {
            constVars.erase(code.result);
        }
    }
    for (auto& code : seq1) {
        if (code.result.type == ArgType::BB_TEMP && (code.arg1.type == ArgType::CONSTANT || constVars.contains(code.arg1)) && (
            (code.type == OpType::ADD || code.type == OpType::MUL || code.type == OpType::SUB) && (code.arg2.type == ArgType::CONSTANT || constVars.contains(code.arg2))
            || code.type == OpType::NEG || code.type == OpType::ASSIGN)) {
            constVars.insert(code.result);
        }
    }

    struct VarState {
        enum StateType { CONSTANT, DELTA };
        struct Component {
            enum Sign { POSITIVE, NEGATIVE };
            Sign sign;
            QuadrupleArg arg;
        };
        StateType type = DELTA;
        std::vector<Component> components;
    };

    // 优化 v0 = v0 ± c 和 v0 = c
    std::unordered_map<int32_t, int32_t> aliasMap;  // 临时变量 id 到变量 id 的映射
    std::unordered_map<int32_t, VarState> stateMap; // 变量一次循环后的状态
    for (auto& code : seq1) {
        if (code.type == OpType::ASSIGN) {
            if (constVars.contains(code.result)) {
                // 如果赋值结果是循环不变量，先放着
                continue;
            }
            if (code.result.type == ArgType::BB_TEMP) {
                if (code.arg1.type != ArgType::PTR_OFFSET) {
                    return {false, 0, 0};
                }
                // t = v 型
                for (auto it = aliasMap.begin(), end = aliasMap.end(); it != end; ++it) {
                    if (it->second == code.arg1.value) {
                        aliasMap.erase(it);
                        break;
                    }
                }
                aliasMap[code.result.value] = code.arg1.value;
            } else if (code.arg1.type == ArgType::CONSTANT || constVars.contains(code.arg1)) {
                // v = c 型
                // 从增量转为常量赋值
                stateMap[code.result.value] = {VarState::CONSTANT, std::vector<VarState::Component>{{VarState::Component::POSITIVE, code.arg1}}};
            } else if (code.arg1.type == ArgType::BB_TEMP) {
                // v = t 型，不串台就可以
                if (auto it = aliasMap.find(code.arg1.value); it == aliasMap.end() || it->second != code.result.value) {
                    // 变量串台了，无法优化
                    return {false, 0, 0};
                }
            } else {
                // v = v 型
                // 变量串台了，无法优化
                return {false, 0, 0};
            }
        } else if (code.type == OpType::MUL || code.type == OpType::NEG) {
            if (!constVars.contains(code.result)) {
                // 如果乘法/负结果不是循环不变量，无法优化
                return {false, 0, 0};
            }
            // 如果乘法/负结果（临时变量）是循环不变量，先放着
        } else if (code.type == OpType::ADD) {
            if (constVars.contains(code.result)) {
                // 如果加法结果（临时变量）是循环不变量，先放着
                continue;
            }
            // 如果加法结果不是循环不变量
            if (code.result.type == ArgType::PTR_OFFSET) {
                // v = v + c，自增型（考虑交换律），且增量为循环不变量
                if (code.arg1 == code.result && (code.arg2.type == ArgType::CONSTANT || constVars.contains(code.arg2))) {
                    stateMap[code.result.value].components.emplace_back(VarState::Component::POSITIVE, code.arg2);
                } else if (code.arg2 == code.result && (code.arg1.type == ArgType::CONSTANT || constVars.contains(code.arg1))) {
                    stateMap[code.result.value].components.emplace_back(VarState::Component::POSITIVE, code.arg1);
                } else if (code.arg1.type == ArgType::BB_TEMP && (code.arg2.type == ArgType::CONSTANT || constVars.contains(code.arg2))) {
                    if (auto it = aliasMap.find(code.arg1.value); it == aliasMap.end() || it->second != code.result.value) {
                        return {false, 0, 0};
                    }
                    stateMap[code.result.value].components.emplace_back(VarState::Component::POSITIVE, code.arg2);
                    aliasMap.erase(code.arg1.value);
                } else if (code.arg2.type == ArgType::BB_TEMP && (code.arg1.type == ArgType::CONSTANT || constVars.contains(code.arg1))) {
                    if (auto it = aliasMap.find(code.arg2.value); it == aliasMap.end() || it->second != code.result.value) {
                        return {false, 0, 0};
                    }
                    stateMap[code.result.value].components.emplace_back(VarState::Component::POSITIVE, code.arg1);
                    aliasMap.erase(code.arg2.value);
                } else {
                    // 变量串台了，无法优化
                    return {false, 0, 0};
                }
            } else {
                // code.result.type == ArgType::BB_TEMP
                // t = v + c，一个为待赋值变量，另一个为循环不变量（考虑交换律）
                if (code.arg1.type == ArgType::PTR_OFFSET && (code.arg2.type == ArgType::CONSTANT || constVars.contains(code.arg2))) {
                    stateMap[code.arg1.value].components.emplace_back(VarState::Component::POSITIVE, code.arg2);
                    aliasMap[code.result.value] = code.arg1.value;
                } else if (code.arg2.type == ArgType::PTR_OFFSET && (code.arg1.type == ArgType::CONSTANT || constVars.contains(code.arg1))) {
                    stateMap[code.arg2.value].components.emplace_back(VarState::Component::POSITIVE, code.arg1);
                    aliasMap[code.result.value] = code.arg2.value;
                } else if (code.arg1.type == ArgType::BB_TEMP && (code.arg2.type == ArgType::CONSTANT || constVars.contains(code.arg2))) {
                    if (auto it = aliasMap.find(code.arg1.value); it != aliasMap.end()) {
                        int32_t currentVar = it->second;
                        stateMap[currentVar].components.emplace_back(VarState::Component::POSITIVE, code.arg2);
                        aliasMap.erase(it);
                        aliasMap[code.result.value] = currentVar;
                    } else {
                        return {false, 0, 0};
                    }
                } else if (code.arg2.type == ArgType::BB_TEMP && (code.arg1.type == ArgType::CONSTANT || constVars.contains(code.arg1))) {
                    if (auto it = aliasMap.find(code.arg2.value); it != aliasMap.end()) {
                        int32_t currentVar = it->second;
                        stateMap[currentVar].components.emplace_back(VarState::Component::POSITIVE, code.arg1);
                        aliasMap.erase(it);
                        aliasMap[code.result.value] = currentVar;
                    } else {
                        return {false, 0, 0};
                    }
                } else {
                    // 变量串台了，无法优化
                    return {false, 0, 0};
                }
            }
        } else if (code.type == OpType::SUB) {
            if (constVars.contains(code.result)) {
                // 如果减法结果（临时变量）是循环不变量，先放着
                continue;
            }
            // 如果减法结果不是循环不变量
            if (code.result.type == ArgType::PTR_OFFSET) {
                // v = v - c，自减型，且增量为循环不变量
                if (code.arg1 == code.result && (code.arg2.type == ArgType::CONSTANT || constVars.contains(code.arg2))) {
                    stateMap[code.result.value].components.emplace_back(VarState::Component::NEGATIVE, code.arg2);
                } else if (code.arg1.type == ArgType::BB_TEMP && (code.arg2.type == ArgType::CONSTANT || constVars.contains(code.arg2))) {
                    if (auto it = aliasMap.find(code.arg1.value); it == aliasMap.end() || it->second != code.result.value) {
                        return {false, 0, 0};
                    }
                    stateMap[code.result.value].components.emplace_back(VarState::Component::NEGATIVE, code.arg2);
                    aliasMap.erase(code.arg1.value);
                } else {
                    // 变量串台了，无法优化
                    return {false, 0, 0};
                }
            } else {
                // code.result.type == ArgType::BB_TEMP
                // t = v - c，左侧为待赋值变量，右侧为循环不变量
                if (code.arg1.type == ArgType::PTR_OFFSET && (code.arg2.type == ArgType::CONSTANT || constVars.contains(code.arg2))) {
                    stateMap[code.arg1.value].components.emplace_back(VarState::Component::NEGATIVE, code.arg2);
                    aliasMap[code.result.value] = code.arg1.value;
                } else if (code.arg1.type == ArgType::BB_TEMP && (code.arg2.type == ArgType::CONSTANT || constVars.contains(code.arg2))) {
                    if (auto it = aliasMap.find(code.arg1.value); it != aliasMap.end()) {
                        int32_t currentVar = it->second;
                        stateMap[currentVar].components.emplace_back(VarState::Component::NEGATIVE, code.arg2);
                        aliasMap.erase(it);
                        aliasMap[code.result.value] = currentVar;
                    } else {
                        return {false, 0, 0};
                    }
                } else {
                    // 变量串台了，无法优化
                    return {false, 0, 0};
                }
            }
        }
    }

    // 界定测试变量
    int32_t testOffset = seq1.back().arg1.type == ArgType::BB_TEMP ? aliasMap[seq1.back().arg1.value] : seq1.back().arg1.value;
    // 需要测试变量进来一定是非0
    if (varValueInfo == nullptr && getZeroVarInfoLocally().type != VarValueInfo::InfoType::NOT_ZERO ||
        varValueInfo != nullptr && varValueInfo->in.find(testOffset)->second.type != VarValueInfo::InfoType::NOT_ZERO) {
        return {false, 0, 0};
    }

    // 合并常量并排序
    for (auto& state : stateMap | std::views::values) {
        int32_t constant = 0;
        for (auto &component : state.components) {
            if (component.arg.type == ArgType::CONSTANT) {
                constant += (component.sign == VarState::Component::POSITIVE ? component.arg.value : -component.arg.value);
            }
        }
        std::erase_if(state.components, [](auto &component) { return component.arg.type == ArgType::CONSTANT; });
        constant = static_cast<int8_t>(constant);
        if (constant == 0) {
            if (state.type == VarState::CONSTANT && state.components.empty()) {
                state.components.emplace_back(VarState::Component::POSITIVE, QuadrupleArg{ArgType::CONSTANT, constant});
            }
        } else {
            state.components.emplace_back(VarState::Component::POSITIVE, QuadrupleArg{ArgType::CONSTANT, constant});
        }
        std::ranges::sort(state.components, [](auto &a, auto &b) { return a.arg < b.arg; });
    }
    std::erase_if(stateMap, [](auto &pair) { return pair.second.components.empty(); });

    // 若没更新测试变量，或者测试变量最终是常量，或者测试变量的步长不是奇数常数，放弃优化，交给通用局部优化器
    if (auto it = stateMap.find(testOffset); it == stateMap.end() || it->second.type == VarState::CONSTANT || it->second.components.size() != 1
        || it->second.components.back().arg.type != ArgType::CONSTANT || (it->second.components.back().arg.value & 1) == 0) {
        return {false, 0, 0};
    }

    // 要留下的临时变量
    std::unordered_set<int32_t> temps;
    for (auto& state : stateMap | std::views::values) {
        for (auto& component : state.components) {
            if (component.arg.type == ArgType::BB_TEMP) {
                temps.insert(component.arg.value);
            }
        }
    }

    // 留下要用到的一系列语句
    std::unordered_set<int32_t> codeIndexToCopy;
    for (int32_t i = seq1.size() - 1; i >= 0; i--) {
        if (seq1[i].result.type == ArgType::BB_TEMP && temps.contains(seq1[i].result.value)) {
            codeIndexToCopy.insert(i);
            if (seq1[i].arg1.type == ArgType::BB_TEMP) {
                temps.insert(seq1[i].arg1.value);
            }
            if (seq1[i].arg2.type == ArgType::BB_TEMP) {
                temps.insert(seq1[i].arg2.value);
            }
        }
    }

    irs.clear();  // 这里已经把最后一条 JNZ 截掉了
    for (int32_t i = 0, size = seq1.size(); i < size; i++) {
        if (codeIndexToCopy.contains(i)) {
            irs.push_back(seq1[i]);
        }
    }

    // 查找已有的最大临时变量编号
    int32_t maxTempId = findAvailableTempId(irs);

    // 摆烂了，交给通用优化器来化简吧
    // 应该能化简吧，配合上预定义规则
    // 循环次数 n = ((-t) * inv_k) & MASK;
    // 特别地，k = -1 时循环 t 次，k = 1 时循环 -t 次
    int32_t invK = static_cast<int8_t>(-inverse256(stateMap[testOffset].components.back().arg.value & 0xff));
    irs.emplace_back(
        OpType::MUL,
        QuadrupleArg{ArgType::BB_TEMP, maxTempId},
        QuadrupleArg{ArgType::PTR_OFFSET, testOffset},
        QuadrupleArg{ArgType::CONSTANT, invK}
    );
    int32_t loopTimesTempId = maxTempId;
    maxTempId++;
    bool hasConstant = false;
    for (auto& [offset, state] : stateMap) {
        if (offset == testOffset) {
            continue;
        }
        if (state.type == VarState::CONSTANT) {
            hasConstant = true;
            if (state.components.size() == 1) {
                irs.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::PTR_OFFSET, offset}, state.components.back().arg);
            } else {
                auto &first = state.components[0];
                auto &second = state.components[1];
                if (first.sign == VarState::Component::NEGATIVE) {
                    irs.emplace_back(OpType::NEG, QuadrupleArg{ArgType::BB_TEMP, maxTempId}, first.arg);
                    maxTempId++;
                    irs.emplace_back(
                        second.sign == VarState::Component::POSITIVE ? OpType::ADD : OpType::SUB,
                        QuadrupleArg{ArgType::BB_TEMP, maxTempId},
                        QuadrupleArg{ArgType::BB_TEMP, maxTempId - 1},
                        second.arg
                    );
                    maxTempId++;
                } else {
                    irs.emplace_back(
                        second.sign == VarState::Component::POSITIVE ? OpType::ADD : OpType::SUB,
                        QuadrupleArg{ArgType::BB_TEMP, maxTempId},
                        first.arg,
                        second.arg
                    );
                    maxTempId++;
                }
                for (int32_t i = 2, size = state.components.size(); i < size; i++) {
                    irs.emplace_back(
                        state.components[i].sign == VarState::Component::POSITIVE ? OpType::ADD : OpType::SUB,
                        QuadrupleArg{ArgType::BB_TEMP, maxTempId},
                        QuadrupleArg{ArgType::BB_TEMP, maxTempId - 1},
                        state.components[i].arg
                    );
                    maxTempId++;
                }
                irs.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::PTR_OFFSET, offset}, QuadrupleArg{ArgType::BB_TEMP, maxTempId - 1});
            }
        } else {
            // delta
            if (state.components.size() == 1) {
                irs.emplace_back(
                    OpType::MUL,
                    QuadrupleArg{ArgType::BB_TEMP, maxTempId},
                    state.components.back().arg,
                    QuadrupleArg{ArgType::BB_TEMP, loopTimesTempId}
                );
                maxTempId++;
                irs.emplace_back(
                    state.components.back().sign == VarState::Component::POSITIVE ? OpType::ADD : OpType::SUB,
                    QuadrupleArg{ArgType::BB_TEMP, maxTempId},
                    QuadrupleArg{ArgType::PTR_OFFSET, offset},
                    QuadrupleArg{ArgType::BB_TEMP, maxTempId - 1}
                );
                maxTempId++;
                irs.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::PTR_OFFSET, offset}, QuadrupleArg{ArgType::BB_TEMP, maxTempId - 1});
            } else {
                auto& first = state.components[0];
                auto& second = state.components[1];
                if (first.sign == VarState::Component::NEGATIVE) {
                    irs.emplace_back(OpType::NEG, QuadrupleArg{ArgType::BB_TEMP, maxTempId}, first.arg);
                    maxTempId++;
                    irs.emplace_back(
                        second.sign == VarState::Component::POSITIVE ? OpType::ADD : OpType::SUB,
                        QuadrupleArg{ArgType::BB_TEMP, maxTempId},
                        QuadrupleArg{ArgType::BB_TEMP, maxTempId - 1},
                        second.arg
                    );
                    maxTempId++;
                } else {
                    irs.emplace_back(
                        second.sign == VarState::Component::POSITIVE ? OpType::ADD : OpType::SUB,
                        QuadrupleArg{ArgType::BB_TEMP, maxTempId},
                        first.arg,
                        second.arg
                    );
                    maxTempId++;
                }
                for (int32_t i = 2, size = state.components.size(); i < size; i++) {
                    irs.emplace_back(
                        state.components[i].sign == VarState::Component::POSITIVE ? OpType::ADD : OpType::SUB,
                        QuadrupleArg{ArgType::BB_TEMP, maxTempId},
                        QuadrupleArg{ArgType::BB_TEMP, maxTempId - 1},
                        state.components[i].arg
                    );
                    maxTempId++;
                }
                irs.emplace_back(
                    OpType::MUL,
                    QuadrupleArg{ArgType::BB_TEMP, maxTempId},
                    QuadrupleArg{ArgType::BB_TEMP, maxTempId - 1},
                    QuadrupleArg{ArgType::BB_TEMP, loopTimesTempId}
                );
                maxTempId++;
                irs.emplace_back(
                    OpType::ADD,
                    QuadrupleArg{ArgType::BB_TEMP, maxTempId},
                    QuadrupleArg{ArgType::PTR_OFFSET, offset},
                    QuadrupleArg{ArgType::BB_TEMP, maxTempId - 1}
                );
                maxTempId++;
                irs.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::PTR_OFFSET, offset}, QuadrupleArg{ArgType::BB_TEMP, maxTempId - 1});
            }
        }
    }
    // 测试变量清零
    irs.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::PTR_OFFSET, testOffset}, QuadrupleArg{ArgType::CONSTANT, 0});
    // 循环消除，JNZ 在前面已经扬了
    succY = nullptr;
    std::erase(preds, this);
    // 若循环内未发生纯常量赋值且除了自己外只有一个前驱，且这个前驱的某个后继是自己的后继，消除 if
    // 也能处理 do-while，因为外面根本就没有 if
    // 更正：有个陷阱，原则上不能消除除纯清零外的 if：测试变量为0，就不应该执行计算，因为计算可能导致访问越界
    // stateMap 的大小为 1 表示纯清零
    // 严格模式：若指针信息已知则能消除 if，最后映射内存的时候如果有负偏移量可以调；若未知则保守
    // 激进模式：若指针位置未知，分析指针的最大偏移量，然后在常规纸带的两端留出安全空间
    if (isStrict) {
        if (preds.size() == 1 && (stateMap.size() == 1 || (!(ptrIn.isUnknown || ptrIn.isUnreachable) && !hasConstant))) {
            auto pred = preds.back();
            // pred->succY != nullptr 说明是条件跳转
            if (pred->succY != nullptr && (pred->succY == succN || pred->succN == succN)) {
                std::erase(succN->preds, pred);
                pred->succY = nullptr;
                pred->succN = this;
                pred->irs.pop_back();
            }
        }
    } else if (preds.size() == 1 && !hasConstant) {
        auto pred = preds.back();
        // pred->succY != nullptr 说明是条件跳转
        if (pred->succY != nullptr && (pred->succY == succN || pred->succN == succN)) {
            std::erase(succN->preds, pred);
            pred->succY = nullptr;
            pred->succN = this;
            pred->irs.pop_back();
            if (stateMap.size() > 1 && (ptrIn.isUnknown || ptrIn.isUnreachable)) {
                // 计算安全空间
                int32_t negOffset = 0, posOffset = 0;
                for (int32_t offset : stateMap | std::views::keys) {
                    negOffset = std::min(negOffset, offset);
                    posOffset = std::max(posOffset, offset);
                }
                return {true, negOffset, posOffset};
            }
        }
    }

    return {true, 0, 0};
}

bool MidBasicBlock::generalOptimize(const std::unordered_map<int32_t, VarValueInfo> &varInfos) {
    // 先消除指针移动，全部转换为基址 + 偏移量
    auto [seq1, currentPtr] = getNoPtrMoveForm();
    // 查找已有的最大临时变量编号
    int32_t maxTempId = findAvailableTempId(seq1);

    // 展开四元式为“单赋值”形式
    std::vector<Quadruple> seq2;
    seq2.reserve(seq1.size());
    for (auto &code : seq1) {
        if ((code.type == OpType::ADD || code.type == OpType::SUB || code.type == OpType::MUL) && code.result.type == ArgType::PTR_OFFSET) {
            // 等号左侧为常规变量的二元运算
            seq2.emplace_back(code.type, QuadrupleArg{ArgType::BB_TEMP, maxTempId}, code.arg1, code.arg2);
            seq2.emplace_back(OpType::ASSIGN, code.result, QuadrupleArg{ArgType::BB_TEMP, maxTempId});
            maxTempId++;
        } else if (code.type == OpType::NEG && code.result.type == ArgType::PTR_OFFSET) {
            // 等号左侧为常规变量的一元运算
            seq2.emplace_back(code.type, QuadrupleArg{ArgType::BB_TEMP, maxTempId}, code.arg1);
            seq2.emplace_back(OpType::ASSIGN, code.result, QuadrupleArg{ArgType::BB_TEMP, maxTempId});
            maxTempId++;
        } else if (code.type == OpType::ASSIGN && code.result.type == ArgType::PTR_OFFSET && code.arg1.type == ArgType::PTR_OFFSET) {
            // 等号左右两侧都是常规变量的赋值
            seq2.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::BB_TEMP, maxTempId}, code.arg1);
            seq2.emplace_back(OpType::ASSIGN, code.result, QuadrupleArg{ArgType::BB_TEMP, maxTempId});
            maxTempId++;
        } else if (code.type == OpType::GETCHAR && code.result.type == ArgType::PTR_OFFSET) {
            // 输入
            seq2.emplace_back(OpType::GETCHAR, QuadrupleArg{ArgType::BB_TEMP, maxTempId});
            seq2.emplace_back(OpType::ASSIGN, code.result, QuadrupleArg{ArgType::BB_TEMP, maxTempId});
            maxTempId++;
        } else {
            seq2.push_back(code);
        }
    }

    std::unordered_set<QuadrupleArg> notZeroSet;    // 用于处理条件跳转消除
    while (true) {
        notZeroSet.clear();
        for (auto &[offset, info] : varInfos) {
            if (info.type == VarValueInfo::InfoType::NOT_ZERO) {
                notZeroSet.emplace(ArgType::PTR_OFFSET, offset);
            }
        }

        // 第一遍
        std::unordered_map<QuadrupleArg, QuadrupleArg> valMap;  // 变量或临时变量的已知值
        for (auto& [offset, info] : varInfos) {
            if (info.type == VarValueInfo::InfoType::EXACT) {
                valMap[{ArgType::PTR_OFFSET, offset}] = {ArgType::CONSTANT, info.exactValue};
            }
        }
        std::unordered_map<Quadruple::CSECanonicalKey, int32_t> cseMap;    // 规范化表达式到产生该值的临时变量
        std::map<int32_t, QuadrupleArg> pendingAssign;    // 变量的待定赋值，需要排序

        seq1.clear();
        bool isFinalAssigned = false;
        for (auto &code : seq2) {
            // 解析最新值
            Quadruple simplified{code.type, code.result};
            if (auto it = valMap.find(code.arg1); it == valMap.end()) {
                simplified.arg1 = code.arg1;
            } else {
                simplified.arg1 = it->second;
            }
            if (auto it = valMap.find(code.arg2); it == valMap.end()) {
                simplified.arg2 = code.arg2;
            } else {
                simplified.arg2 = it->second;
            }
            // 预化简
            simplified.preSimplifyMod256();

            if (simplified.type == OpType::ADD || simplified.type == OpType::SUB || simplified.type == OpType::MUL || simplified.type == OpType::NEG) {
                // 这些运算左侧都只可能为临时变量
                auto key = simplified.toCanonicalKey();
                if (auto it = cseMap.find(key); it == cseMap.end()) {
                    // 未命中公共子表达式，发射
                    seq1.push_back(simplified);
                    cseMap[key] = simplified.result.value;
                } else {
                    // 命中公共子表达式
                    valMap[simplified.result] = {ArgType::BB_TEMP, it->second};
                }
                // 对非零取负加入非零集
                if (simplified.type == OpType::NEG && notZeroSet.contains(simplified.arg1)) {
                    notZeroSet.insert(simplified.result);
                }
            } else if (simplified.type == OpType::ASSIGN) {
                if (simplified.result.type == ArgType::PTR_OFFSET) {
                    // 这里可以识别无效自赋值
                    // 只有 valMap[result] != arg1 才是有效赋值
                    if (auto it = valMap.find(simplified.result); it == valMap.end() || it->second != simplified.arg1) {
                        valMap[simplified.result] = simplified.arg1;
                        pendingAssign[simplified.result.value] = simplified.arg1;
                    }
                } else {
                    // result 为临时变量
                    if (simplified.arg1.type == ArgType::PTR_OFFSET) {
                        // 拉到 valMap 里，后续所有对变量的引用指向临时变量
                        valMap[simplified.arg1] = simplified.result;
                        // 需要发射吗？通常来说一定发射，但是万一后面只有自赋值，这里的 t = v 就是无效赋值（t 未被使用），只能在第二遍删除
                        seq1.push_back(simplified);
                    } else {
                        valMap[simplified.result] = simplified.arg1;
                    }
                }
                // 非零量只能通过赋值传播，运算后是未知
                if (notZeroSet.contains(simplified.arg1)) {
                    notZeroSet.insert(simplified.result);
                }
            } else if (simplified.type == OpType::GETCHAR || simplified.type == OpType::PUTCHAR) {
                seq1.push_back(simplified);
            } else {
                // JZ、JNZ
                // 提前发射赋值
                for (auto& [offset, arg1] : pendingAssign) {
                    if (arg1.type == ArgType::CONSTANT) {
                        // 赋相同常量就不发射了
                        if (auto it = varInfos.find(offset); it == varInfos.end() || !(it->second.type == VarValueInfo::InfoType::EXACT && it->second.exactValue == arg1.value)) {
                            seq1.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::PTR_OFFSET, offset}, arg1); // 发射赋值
                        }
                    } else {
                        seq1.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::PTR_OFFSET, offset}, arg1); // 发射赋值
                    }
                }
                isFinalAssigned = true;
                seq1.push_back(simplified);
            }
        }
        if (!isFinalAssigned && succN != nullptr) {
            // 若没有提前发射且不是程序结束则发射赋值
            for (auto& [offset, arg1] : pendingAssign) {
                if (arg1.type == ArgType::CONSTANT) {
                    // 赋相同常量就不发射了
                    if (auto it = varInfos.find(offset); it == varInfos.end() || !(it->second.type == VarValueInfo::InfoType::EXACT && it->second.exactValue == arg1.value)) {
                        seq1.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::PTR_OFFSET, offset}, arg1); // 发射赋值
                    }
                } else {
                    seq1.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::PTR_OFFSET, offset}, arg1); // 发射赋值
                }
            }
        }

        // 第二遍：尝试根据预定义压缩规则合并只被引用一次的临时变量（2 条四元式 -> 1条）
        // 临时变量不可能自引用，不可能出现 t1 = t1 + 1
        // 其实这里有个 bug，t2 = t1 + t1，会被算作引用两次，但是不影响，因为暂时没有这种合并规则
        shrinkOptimize(seq1, seq2, &Quadruple::mergeMod256);
        if (seq2.size() == seq1.size()) {
            break;  // 第二遍没有进一步优化，退出
        }
    }
    // seq2 是优化好的代码
    // 检查负数常数乘法，如果该临时变量只被用来加减，可以变换为正数常数乘法
    std::unordered_map<int32_t, std::vector<int32_t>> mulTempIdToPos;   // value 的第一个元素是产生处
    for (int32_t i = 0, size = seq2.size(); i < size; i++) {
        if (seq2[i].type == OpType::MUL && seq2[i].arg2.type == ArgType::CONSTANT && seq2[i].arg2.value < 0) {
            mulTempIdToPos[seq2[i].result.value] = {i};
            continue;
        }
        auto it1 = mulTempIdToPos.end();
        auto it2 = mulTempIdToPos.end();
        if (seq2[i].arg1.type == ArgType::BB_TEMP) {
            it1 = mulTempIdToPos.find(seq2[i].arg1.value);
        }
        if (seq2[i].arg2.type == ArgType::BB_TEMP) {
            it2 = mulTempIdToPos.find(seq2[i].arg2.value);
        }
        if (it1 != mulTempIdToPos.end() && it2 != mulTempIdToPos.end()) {
            mulTempIdToPos.erase(it1);
            if (it1 != it2) {
                mulTempIdToPos.erase(it2);
            }
        } else if (it1 != mulTempIdToPos.end()) {
            if (seq2[i].type == OpType::ADD) {
                it1->second.push_back(i);
            } else {
                mulTempIdToPos.erase(it1);
            }
        } else if (it2 != mulTempIdToPos.end()) {
            if (seq2[i].type == OpType::ADD || seq2[i].type == OpType::SUB) {
                it2->second.push_back(i);
            } else {
                mulTempIdToPos.erase(it2);
            }
        }
    }
    for (auto& [tempId, positions] : mulTempIdToPos) {
        seq2[positions.front()].arg2.value = static_cast<int8_t>(-seq2[positions.front()].arg2.value);   // 乘负数变成乘正数
        // 从第二个开始是使用这个变量的地方
        for (auto it = positions.begin() + 1, end = positions.end(); it != end; ++it) {
            if (seq2[*it].arg1.value == tempId) {
                // arg1 是这个变量，那肯定是加法，调转顺序然后转减法
                std::swap(seq2[*it].arg1, seq2[*it].arg2);
                seq2[*it].type = OpType::SUB;
            } else if (seq2[*it].type == OpType::SUB) {
                seq2[*it].type = OpType::ADD;   // arg2 是这个变量且为减法，转加法
            } else {
                seq2[*it].type = OpType::SUB;   // arg2 是这个变量且为加法，转减法
            }
        }
    }

    // 尝试内联跳转变量
    if (!seq2.empty()) {
        auto& lastCode = seq2.back();
        if ((lastCode.type == OpType::JZ || lastCode.type == OpType::JNZ) && lastCode.arg1.type == ArgType::BB_TEMP) {
            int32_t i;
            // 找到给这个临时变量赋值的地方
            for (i = seq2.size() - 2; i >= 0; --i) {
                if (seq2[i].type == OpType::ASSIGN && seq2[i].result == lastCode.arg1) {
                    break;
                }
            }
            // 如果不是赋值（可能是运算），且等号右侧不是变量，不管
            if (i >= 0 && seq2[i].arg1.type == ArgType::PTR_OFFSET) {
                // 要求给临时变量赋值后没有再对这个变量再进行定值，且没有再引用这个临时变量
                bool canInline = true;
                for (int32_t j = i + 1, size = seq2.size() - 1; j < size; ++j) {
                    if (seq2[j].result == seq2[i].arg1 || seq2[j].arg1 == seq2[i].result || seq2[j].arg2 == seq2[i].result) {
                        canInline = false;
                        break;
                    }
                }
                if (canInline) {
                    // 内联并删除原始赋值
                    lastCode.arg1 = seq2[i].arg1;
                    seq2.erase(seq2.begin() + i);
                }
            }
        }
    }

    // 先进行条件跳转消除
    if (!seq2.empty()) {
        bool isJmpKnown = false;
        bool isJmpToTrue = false;
        auto &lastCode = seq2.back();
        if (lastCode.type == OpType::JZ) {
            if (lastCode.arg1.type == ArgType::CONSTANT) {
                isJmpKnown = true;
                isJmpToTrue = lastCode.arg1.value == 0;
            } else if (notZeroSet.contains(lastCode.arg1)) {
                // 其实这里很简单，只需要考虑 not zero 的情况，exact 的情况都会常数折叠成上面的情况
                isJmpKnown = true;
                isJmpToTrue = false;
            }
        } else if (lastCode.type == OpType::JNZ) {
            if (lastCode.arg1.type == ArgType::CONSTANT) {
                isJmpKnown = true;
                isJmpToTrue = lastCode.arg1.value != 0;
            } else if (notZeroSet.contains(lastCode.arg1)) {
                // 其实这里很简单，只需要考虑 not zero 的情况，exact 的情况都会常数折叠成上面的情况
                isJmpKnown = true;
                isJmpToTrue = true;
            }
        }
        if (isJmpKnown) {
            if (isJmpToTrue) {
                // 一定走 true 分支
                std::erase(succN->preds, this);
                succN = succY;
                succY = nullptr;
            } else {
                // 一定走 false 分支
                std::erase(succY->preds, this);
                succY = nullptr;
            }
            seq2.pop_back();
        }
    }

    // 指针偏移恢复
    if (currentPtr != 0 && (succN != nullptr || succY != nullptr)) {
        if (!seq2.empty()) {
            auto &lastCode = seq2.back();
            if (lastCode.type == OpType::JZ || lastCode.type == OpType::JNZ) {
                // 如果指针不平衡，且跳转测试变量是 ptr_offset，在移动完指针后，跳转测试变量恢复到0偏移
                if (lastCode.arg1.type == ArgType::PTR_OFFSET) {
                    lastCode.arg1.value = 0;
                }
                // 在跳转前插入指针偏移
                seq2.emplace(seq2.end() - 1, OpType::PTR_ADD, QuadrupleArg{}, QuadrupleArg{ArgType::CONSTANT, currentPtr});
            } else {
                seq2.emplace_back(OpType::PTR_ADD, QuadrupleArg{}, QuadrupleArg{ArgType::CONSTANT, currentPtr});
            }
        } else {
            seq2.emplace_back(OpType::PTR_ADD, QuadrupleArg{}, QuadrupleArg{ArgType::CONSTANT, currentPtr});
        }
    }

    // 直接交换到代码块里
    std::swap(irs, seq2);
    return seq2 != irs;
}

std::unique_ptr<MidBasicBlock> MidBasicBlock::hoistLoopConstAssign() {
    // 需要是指针平衡的最内层 while (*ptr) 循环
    if (irs.empty() || irs.back().type != OpType::JNZ || succY != this || calcDelta() != 0) {
        return {};
    }
    // 先消除指针移动，全部转换为基址 + 偏移量
    auto seq1 = getNoPtrMoveForm().first;
    // 找到所有被赋常量的变量
    std::unordered_map<int32_t, int32_t> varOffsetToConstant;
    for (auto& code : seq1) {
        if (code.type == OpType::ASSIGN && code.result.type == ArgType::PTR_OFFSET && code.arg1.type == ArgType::CONSTANT) {
            varOffsetToConstant[code.result.value] = code.arg1.value;
        }
    }
    if (varOffsetToConstant.empty()) {
        return {};
    }
    // 找到所有被使用的变量
    std::unordered_set<int32_t> usedVars;
    for (auto& code : seq1) {
        if (code.arg1.type == ArgType::PTR_OFFSET) {
            usedVars.insert(code.arg1.value);
        }
        if (code.arg2.type == ArgType::PTR_OFFSET) {
            usedVars.insert(code.arg2.value);
        }
    }
    // 判断常量定值有没有被使用
    bool isUsed = false;
    for (const auto& offset : varOffsetToConstant | std::views::keys) {
        if (usedVars.contains(offset)) {
            isUsed = true;
            break;
        }
    }

    auto newBlock = std::make_unique<MidBasicBlock>();
    irs.clear();
    if (!isUsed) {
        // 常量定值未被使用，直接外提
        for (auto &code : seq1) {
            if (code.type == OpType::ASSIGN && code.result.type == ArgType::PTR_OFFSET && code.arg1.type == ArgType::CONSTANT && varOffsetToConstant.contains(code.result.value)) {
                continue;
            }
            irs.push_back(code);
            // 循环内不会使用外提变量
        }
        // 外提出的赋值代码
        for (auto& [offset, constant] : varOffsetToConstant) {
            newBlock->irs.emplace_back(OpType::ASSIGN, QuadrupleArg{ArgType::PTR_OFFSET, offset}, QuadrupleArg{ArgType::CONSTANT, constant});
        }
    } else {
        // 否则需要展开一次再替换成常量
        for (auto &code : seq1) {
            if (code.type == OpType::ASSIGN && code.result.type == ArgType::PTR_OFFSET && code.arg1.type == ArgType::CONSTANT && varOffsetToConstant.contains(code.result.value)) {
                continue;
            }
            irs.push_back(code);
            // 把所有对常量变量的使用换成常量，剩下的交给通用优化器
            auto& lastCode = irs.back();
            if (lastCode.arg1.type == ArgType::PTR_OFFSET) {
                if (auto it = varOffsetToConstant.find(lastCode.arg1.value); it != varOffsetToConstant.end()) {
                    lastCode.arg1 = {ArgType::CONSTANT, it->second};
                }
            }
            if (lastCode.arg2.type == ArgType::PTR_OFFSET) {
                if (auto it = varOffsetToConstant.find(lastCode.arg2.value); it != varOffsetToConstant.end()) {
                    lastCode.arg2 = {ArgType::CONSTANT, it->second};
                }
            }
        }
        newBlock->irs = seq1; // 原封不动抄过来，把最后的跳转改一下
        newBlock->irs.back().type = OpType::JZ;
    }
    // 先拷贝一份前驱到外提节点，然后外提节点的前驱删掉那个回边
    newBlock->preds = preds;
    std::erase_if(newBlock->preds, [this](auto *block) { return block == this; });
    // 所有原本连到 this 的边全部连到外提节点
    for (auto *block : newBlock->preds) {
        if (block->succN == this) {
            block->succN = newBlock.get();
        }
        if (block->succY == this) {
            block->succY = newBlock.get();
        }
    }
    // this 的前驱只剩下自己和外提节点
    std::erase_if(preds, [this](auto *block) { return block != this; });
    preds.push_back(newBlock.get());

    newBlock->succN = this;
    if (isUsed) {
        newBlock->succY = succN;    // 如果展开了一次
    }
    return newBlock;
}

std::unique_ptr<MidBasicBlock> MidBasicBlock::eliminatePtrMove() {
    // 如果指针未知则跳过
    if (isPtrStateUnknown()) {
        return nullptr;
    }

    auto seq1 = getNoPtrMoveForm().first;
    for (auto& code : seq1) {
        if (code.result.type == ArgType::PTR_OFFSET) {
            code.result.value += ptrIn.pos;
        }
        if (code.arg1.type == ArgType::PTR_OFFSET) {
            code.arg1.value += ptrIn.pos;
        }
        if (code.arg2.type == ArgType::PTR_OFFSET) {
            code.arg2.value += ptrIn.pos;
        }
    }
    if (succN == nullptr) {
        std::swap(seq1, irs);
        return nullptr;
    }

    std::unique_ptr<MidBasicBlock> addBlock = nullptr;
    // auto &succNInfo = ptrInfos.find(succN)->second;

    // 检查后继的情况
    if (succY == nullptr) {
        // 只有一个后继
        if (succN != nullptr && succN->ptrIn.isUnknown) {
            // 若后继指针位置未知，恢复指针移动，一次性移够
            if (ptrOut.pos != 0) {
                seq1.emplace_back(OpType::PTR_ADD, QuadrupleArg{}, QuadrupleArg{ArgType::CONSTANT, ptrOut.pos});
            }
        }
    } else {
        // auto &succYInfo = ptrInfos.find(succY)->second;
        if (succY->ptrIn.isUnknown) {
            if (succN->ptrIn.isUnknown) {
                // 若两个后继的指针位置均未知，恢复指针移动，一次性移够
                if (ptrOut.pos != 0) {
                    seq1.emplace(seq1.end() - 1, OpType::PTR_ADD, QuadrupleArg{}, QuadrupleArg{ArgType::CONSTANT, ptrOut.pos});
                }
                if (seq1.back().arg1.type == ArgType::PTR_OFFSET) {
                    seq1.back().arg1.value = 0;
                }
            } else {
                // succY 处插入一个移动指针的基本块
                if (ptrOut.pos != 0) {
                    addBlock = std::make_unique<MidBasicBlock>();
                    addBlock->irs.emplace_back(OpType::PTR_ADD, QuadrupleArg{}, QuadrupleArg{ArgType::CONSTANT, ptrOut.pos});
                    addBlock->preds.push_back(this);
                    addBlock->succN = succY;
                    std::erase_if(succY->preds, [this](auto *block) { return block == this; });
                    succY->preds.push_back(addBlock.get());
                    succY = addBlock.get();
                }
            }
        } else if (succN->ptrIn.isUnknown) {
            // succN 处插入一个移动指针的基本块
            if (ptrOut.pos != 0) {
                addBlock = std::make_unique<MidBasicBlock>();
                addBlock->irs.emplace_back(Quadruple::OpType::PTR_ADD, QuadrupleArg{}, QuadrupleArg{ArgType::CONSTANT, ptrOut.pos});
                addBlock->preds.push_back(this);
                addBlock->succN = succN;
                std::erase_if(succN->preds, [this](auto *block) { return block == this; });
                succN->preds.push_back(addBlock.get());
                succN = addBlock.get();
            }
        }
    }

    // 其他情况不需要恢复指针移动
    std::swap(seq1, irs);
    return addBlock;
}

bool MidBasicBlock::eliminateUselessVarAssign() {
    std::vector<Quadruple> seq1;
    seq1.reserve(irs.size());
    for (auto &code : irs) {
        if (code.type == OpType::ASSIGN && code.result.type == ArgType::PTR_OFFSET && !liveVarInfo->out.contains(code.result.value)) {
            continue;
        }
        seq1.push_back(code);
    }
    std::swap(seq1, irs);
    return seq1 != irs;
}

void MidBasicBlock::calcVarChange() const {
    std::unordered_set<QuadrupleArg> notZeroSet;
    for (auto& [offset, info] : varValueInfo->in) {
        if (info.type == VarValueInfo::InfoType::NOT_ZERO) {
            notZeroSet.emplace(ArgType::PTR_OFFSET, offset);
        }
    }

    for (auto& code : irs) {
        if (code.type == OpType::NEG) {
            if (notZeroSet.contains(code.arg1)) {
                notZeroSet.insert(code.result);
            }
        } else if (code.type == OpType::ASSIGN) {
            if (notZeroSet.contains(code.arg1)) {
                notZeroSet.insert(code.result);
            }
            if (code.result.type == ArgType::PTR_OFFSET) {
                if (code.arg1.type == ArgType::CONSTANT) {
                    varValueInfo->change[code.result.value] = VarValueInfo::exact(code.arg1.value);
                } else if (notZeroSet.contains(code.arg1)) {
                    varValueInfo->change[code.result.value] = VarValueInfo::notZero();
                } else {
                    varValueInfo->change[code.result.value] = VarValueInfo::unknown();
                }
            }
        }
    }

    // 如果为空无额外信息
    if (irs.empty()) {
        varValueInfo->jmpType = OpType::JMP;
        return;
    }
    auto &lastCode = irs.back();
    // 如果最后一条语句不是条件跳转，没有额外信息
    if (!(lastCode.type == OpType::JNZ || lastCode.type == OpType::JZ)) {
        varValueInfo->jmpType = OpType::JMP;
        return;
    }
    // 如果最后一条语句是条件跳转且使用变量，附加额外信息
    if (lastCode.arg1.type == ArgType::PTR_OFFSET) {
        varValueInfo->jmpType = lastCode.type;
        varValueInfo->jmpTestOffset = lastCode.arg1.value;
        return;
    }
    int32_t tempId = lastCode.arg1.value;
    // 如果跳转语句使用临时变量，基本块至少有两条语句，否则崩溃
    for (int32_t i = irs.size() - 2; i >= 0; i--) {
        if (irs[i].type == OpType::ASSIGN && irs[i].result.type == ArgType::PTR_OFFSET && irs[i].arg1.type == ArgType::BB_TEMP && irs[i].arg1.value == tempId) {
            // 测试的临时变量关联到了变量写入，附加额外信息
            varValueInfo->jmpType = lastCode.type;
            varValueInfo->jmpTestOffset = irs[i].result.value;
            return;
        }
    }
    // 剩下的没有额外信息
    varValueInfo->jmpType = OpType::JMP;
}

std::pair<std::vector<Quadruple>, int32_t> MidBasicBlock::getNoPtrMoveForm() const {
    int32_t currentPtr = 0;
    std::vector<Quadruple> seq1;
    seq1.reserve(irs.size());
    for (auto& code : irs) {
        if (code.type == OpType::PTR_ADD) {
            currentPtr += code.arg1.value;
        } else {
            seq1.push_back(code);
            auto &lastCode = seq1.back();
            if (lastCode.result.type == ArgType::PTR_OFFSET) {
                lastCode.result.value += currentPtr;
            }
            if (lastCode.arg1.type == ArgType::PTR_OFFSET) {
                lastCode.arg1.value += currentPtr;
            }
            if (lastCode.arg2.type == ArgType::PTR_OFFSET) {
                lastCode.arg2.value += currentPtr;
            }
        }
    }
    return {std::move(seq1), currentPtr};
}

void MidBasicBlock::calcUseDef() const {
    // 考虑到基本块优化后，变量定值只在最后一次性完成，同一个变量不会被定值两次
    // 最后的条件跳转：bb_temp 类型，前面的计算已经引用关联的变量；ptr_offset 类型，说明前面根本没有引用/定值这个变量，在这里被首次引用
    // 只要被引用的就放在 use 里
    // 如果被定值且定值前没被引用，放在 def 里
    liveVarInfo->use.clear();
    for (auto &code : irs) {
        if (code.arg1.type == ArgType::PTR_OFFSET) {
            liveVarInfo->use.insert(code.arg1.value);
        }
        if (code.arg2.type == ArgType::PTR_OFFSET) {
            liveVarInfo->use.insert(code.arg2.value);
        }
    }
    liveVarInfo->def.clear();
    for (auto& code : irs) {
        if (code.result.type == ArgType::PTR_OFFSET && !liveVarInfo->use.contains(code.result.value)) {
            liveVarInfo->def.insert(code.result.value);
        }
    }
}

void MidBasicBlock::renameVar(const std::unordered_map<int32_t, int32_t>& offsetMap) {
    for (auto& code : irs) {
        if (code.result.type == ArgType::PTR_OFFSET) {
            code.result.value = offsetMap.find(code.result.value)->second;
        }
        if (code.arg1.type == ArgType::PTR_OFFSET) {
            code.arg1.value = offsetMap.find(code.arg1.value)->second;
        }
        if (code.arg2.type == ArgType::PTR_OFFSET) {
            code.arg2.value = offsetMap.find(code.arg2.value)->second;
        }
    }
}

std::ostream & operator<<(std::ostream &os, const MidBasicBlock &obj) {
    os << "BasicBlock@" << &obj << "{codes=[\n";
    auto it1 = obj.irs.begin();
    auto end1 = obj.irs.end();
    for (; it1 != end1; ++it1) {
        os << "    " << *it1 << "\n";
    }
    os << "], preds=[";
    auto it2 = obj.preds.begin();
    auto end2 = obj.preds.end();
    if (it2 != end2) {
        os << *it2;
        for (++it2; it2 != end2; ++it2) {
            os << ", " << *it2;
        }
    }
    os << "], succN=" << obj.succN
       << ", succY=" << obj.succY
       << ", ptrDelta=" << obj.ptrDelta
       << ", ptrIn=" << obj.ptrIn
       << ", ptrOut=" << obj.ptrOut << '}';
    return os;
}

void MidBasicBlock::printBrief(std::ostream &os) const {
    os << "BasicBlock@" << this << "{preds=[";
    auto it2 = preds.begin();
    auto end2 = preds.end();
    if (it2 != end2) {
        os << *it2;
        for (++it2; it2 != end2; ++it2) {
            os << ", " << *it2;
        }
    }
    os << "], succN=" << succN
       << ", succY=" << succY
       << ", ptrDelta=" << ptrDelta
       << ", ptrIn=" << ptrIn
       << ", ptrOut=" << ptrOut << '}';
}

void MidBasicBlock::shrinkOptimize(std::vector<Quadruple> &src, std::vector<Quadruple> &dst, bool (Quadruple::*mergeFunc)(const Quadruple &)) {
    // 查找临时变量使用情况
    std::unordered_map<int32_t, std::vector<int32_t>> useMap;
    for (int32_t i = 0, size = src.size(); i < size; ++i) {
        if (src[i].arg1.type == ArgType::BB_TEMP) {
            useMap[src[i].arg1.value].push_back(i);
        }
        if (src[i].arg2.type == ArgType::BB_TEMP) {
            useMap[src[i].arg2.value].push_back(i);
        }
    }
    // 合并四元式
    dst.clear();
    for (auto& code : src) {
        if (code.result.type == ArgType::BB_TEMP) {
            if (auto mapIt = useMap.find(code.result.value); mapIt == useMap.end() || mapIt->second.size() == 1 && (src[mapIt->second.back()].*mergeFunc)(code)) {
                // 如果临时变量没被使用，或发生了合并，就不往 seq2 添加
                continue;
            }
        }
        dst.push_back(code);
    }
}

int32_t MidBasicBlock::findAvailableTempId(const std::vector<Quadruple>& codes) {
    int32_t maxTempId = 0;
    for (auto& code : codes) {
        if (code.result.type == ArgType::BB_TEMP && code.result.value > maxTempId) {
            maxTempId = code.result.value;
        }
        if (code.arg1.type == ArgType::BB_TEMP && code.arg1.value > maxTempId) {
            maxTempId = code.arg1.value;
        }
        if (code.arg2.type == ArgType::BB_TEMP && code.arg2.value > maxTempId) {
            maxTempId = code.arg2.value;
        }
    }
    maxTempId++;
    return maxTempId;
}

void MidBasicBlock::exGCD(int32_t a, int32_t b, int32_t *x, int32_t *y) {
    if (b == 0) {
        *x = 1;
        *y = 0;
    } else {
        exGCD(b, a % b, y, x);
        *y = *y - a / b * (*x);
    }
}

int32_t MidBasicBlock::inverse256(int32_t a) {
    __assume(a & 1);
    int32_t x, y;
    exGCD(a, 256, &x, &y);
    return (x % 256 + 256) % 256;
}

#include "x64_basic_block.h"

#include "windows_pe_plus_generator.h"
#include "x64_encoding_ctx.h"
#include "../optimizer/mid_basic_block.h"

using AsmOp = X64Asm::Operator;
using Size = X64RMOperand::Size;
using Reg = X64RMOperand::Reg;
using OpType = Quadruple::OpType;
using ArgType = QuadrupleArg::ArgType;

std::ostream& operator<<(std::ostream& os, const X64BasicBlock& obj) {
    os << "X64BasicBlock{\n    size: " << obj.m_codeSize
       // << ",\n    isAlign: " << obj.m_isAlign
       << ",\n    IRs: [\n";
    for (auto& ir : obj.m_irs) {
        os << "        " << ir << '\n';
    }
    os << "    ],\n    ASMs: [\n";
    for (auto& asm1 : obj.m_asms) {
        os << "        " << asm1 << '\n';
    }
    os << "    ]\n}";
    return os;
}

GettersForInterpret X64BasicBlock::getGettersForInterpreter() {
    return {
        [](const void* block) -> const std::vector<Quadruple>& {
            return static_cast<const X64BasicBlock*>(block)->irs();
        },
        [](const void* block) {
            return static_cast<const void*>(static_cast<const X64BasicBlock*>(block)->succY());
        },
        [](const void* block) {
            return static_cast<const void*>(static_cast<const X64BasicBlock*>(block)->succN());
        }
    };
}

void X64BasicBlock::setIRs(const std::vector<Quadruple>& irs) {
    m_isRearranged = false;
    m_lastJmpIsShort = false;
    m_secondLastJmpIsShort = false;
    m_hasPutchar = false;
    m_hasGetchar = false;
    m_canUseGetcharReg = false;

    for (auto& ir : irs) {
        if (ir.type == OpType::PUTCHAR) {
            m_hasPutchar = true;
        } else if (ir.type == OpType::GETCHAR) {
            m_hasGetchar = true;
        }
    }
    m_irs = irs;
}

void X64BasicBlock::setIRs(std::vector<Quadruple>&& irs) noexcept {
    m_isRearranged = false;
    m_lastJmpIsShort = false;
    m_secondLastJmpIsShort = false;
    m_hasPutchar = false;
    m_hasGetchar = false;
    m_canUseGetcharReg = false;

    for (auto& ir : irs) {
        if (ir.type == OpType::PUTCHAR) {
            m_hasPutchar = true;
        } else if (ir.type == OpType::GETCHAR) {
            m_hasGetchar = true;
        }
    }
    m_irs = std::move(irs);
}

void X64BasicBlock::equivFlipCondJmp() {
    auto it1 = std::find_if(m_asms.rbegin(), m_asms.rend(), [](const auto& code) {
        return code.op == AsmOp::JZ || code.op == AsmOp::JNZ;
    });
    if (it1 != m_asms.rend()) {
        auto it2 = std::find_if(m_irs.rbegin(), m_irs.rend(), [](const auto& code) {
            return code.type == OpType::JZ || code.type == OpType::JNZ;
        });
        if (it2 != m_irs.rend()) {
            it1->op = it1->op == AsmOp::JZ ? AsmOp::JNZ : AsmOp::JZ;
            it2->type = it2->type == OpType::JZ ? OpType::JNZ : OpType::JZ;
            std::swap(m_succN, m_succY);
        }
    }
}

void X64BasicBlock::placeJmp() {
    m_asms.emplace_back(AsmOp::JMP, 0);
}

void X64BasicBlock::removeJmpIfJmpTo(int32_t target) {
    if (m_asms.empty()) {
        return;
    }
    if (auto& lastAsm = m_asms.back(); lastAsm.op == AsmOp::JMP && lastAsm.imm == target) {
        m_asms.pop_back();
    }
}

void X64BasicBlock::setJmpTarget(const std::unordered_map<X64BasicBlock*, int32_t>& idMap) {
    if (m_asms.empty()) {
        return;
    }
    if (auto& lastAsm = m_asms.back(); lastAsm.op == AsmOp::JMP) {
        lastAsm.imm = idMap.find(m_succN)->second;
        if (m_asms.size() >= 2) {
            if (auto &secondLastAsm = m_asms[m_asms.size() - 2]; secondLastAsm.op == AsmOp::JZ || secondLastAsm.op == AsmOp::JNZ) {
                secondLastAsm.imm = idMap.find(m_succY)->second;
            }
        }
    } else if (lastAsm.op == AsmOp::JZ || lastAsm.op == AsmOp::JNZ) {
        lastAsm.imm = idMap.find(m_succY)->second;
    }
}

void X64BasicBlock::firstCalcCodeSize() {
    X64EncodingCtx ctx;
    for (auto& code : m_asms) {
        ctx.emit(code, true);
    }
    m_codeSize = ctx.size();
}

bool X64BasicBlock::tryShortJmp(int32_t thisId, const int32_t* codePos) {
    if (m_asms.empty()) {
        return false;
    }
    auto& lastAsm = m_asms.back();
    if (lastAsm.op != AsmOp::JMP && lastAsm.op != AsmOp::JNZ && lastAsm.op != AsmOp::JZ) {
        return false;
    }
    bool changed = false;
    if (!m_lastJmpIsShort) {
        int32_t ip = codePos[thisId] + m_codeSize;
        int32_t dest = codePos[lastAsm.imm];
        if (dest - ip >= INT8_MIN && dest - ip <= INT8_MAX) {
            m_codeSize -= lastAsm.op == AsmOp::JMP ? 3 : 4;
            m_lastJmpIsShort = true;
            changed = true;
        }
    }
    if (m_asms.size() >= 2) {
        if (auto& secondLastAsm = m_asms[m_asms.size() - 2]; (secondLastAsm.op == AsmOp::JZ || secondLastAsm.op == AsmOp::JNZ) && !m_secondLastJmpIsShort) {
            int32_t ip = codePos[thisId] + m_codeSize - (m_lastJmpIsShort ? 2 : lastAsm.op == AsmOp::JMP ? 5 : 6);
            int32_t dest = codePos[secondLastAsm.imm];
            if (dest - ip >= INT8_MIN && dest - ip <= INT8_MAX) {
                m_codeSize -= 4;
                m_secondLastJmpIsShort = true;
                changed = true;
            }
        }
    }
    return changed;
}

void X64BasicBlock::finalSetJmpRel(int32_t thisId, const int32_t* codePos) {
    if (m_asms.empty()) {
        return;
    }
    auto &lastAsm = m_asms.back();
    if (lastAsm.op != AsmOp::JMP && lastAsm.op != AsmOp::JNZ && lastAsm.op != AsmOp::JZ) {
        return;
    }
    int32_t ip = codePos[thisId] + m_codeSize;
    int32_t dest = codePos[lastAsm.imm];
    lastAsm.imm = dest - ip;
    if (m_asms.size() >= 2) {
        if (auto& secondLastAsm = m_asms[m_asms.size() - 2]; secondLastAsm.op == AsmOp::JZ || secondLastAsm.op == AsmOp::JNZ) {
            ip = codePos[thisId] + m_codeSize - (m_lastJmpIsShort ? 2 : lastAsm.op == AsmOp::JMP ? 5 : 6);
            dest = codePos[secondLastAsm.imm];
            secondLastAsm.imm = dest - ip;
        }
    }
}

void X64BasicBlock::finalEncode(X64EncodingCtx& ctx) const {
    for (auto& code : m_asms) {
        ctx.emit(code, false);
    }
}

int32_t X64BasicBlock::rearrange() {
    if (m_isRearranged || m_irs.empty()) {
        return m_irs.size();
    }
    // 如果最后的跳转不是 [ptr]，把可能的 ptr_add（跳转语句前一条）移到开头
    if ((m_irs.back().type == OpType::JZ || m_irs.back().type == OpType::JNZ) && m_irs.back().arg1.type == ArgType::BB_TEMP && m_irs.size() >= 3 && m_irs[m_irs.size() - 2].type == OpType::PTR_ADD) {
        int32_t ptrMove = m_irs[m_irs.size() - 2].arg1.value;
        m_irs.erase(m_irs.end() - 2);
        for (auto &code : m_irs) {
            if (code.result.type == ArgType::PTR_OFFSET) {
                code.result.value -= ptrMove;
            }
            if (code.arg1.type == ArgType::PTR_OFFSET) {
                code.arg1.value -= ptrMove;
            }
            if (code.arg2.type == ArgType::PTR_OFFSET) {
                code.arg2.value -= ptrMove;
            }
        }
        m_irs.emplace(m_irs.begin(), OpType::PTR_ADD, QuadrupleArg{}, QuadrupleArg{ArgType::CONSTANT, ptrMove});
    }

    // 查找被多次使用的变量，拉到临时变量里
    // 不想管时间复杂度了。。。
    int32_t maxTempId = MidBasicBlock::findAvailableTempId(m_irs);
    std::unordered_map<int32_t, int32_t> varRefCnts;
    std::unordered_set<int32_t> varHasTempAssign;
    for (auto& code : m_irs) {
        if (code.type == OpType::JZ || code.type == OpType::JNZ) {
            continue;
        }
        if (code.arg1.type == ArgType::PTR_OFFSET) {
            varRefCnts[code.arg1.value]++;
        }
        if (code.arg2.type == ArgType::PTR_OFFSET) {
            varRefCnts[code.arg2.value]++;
        }
        if (code.type == OpType::ASSIGN && code.arg1.type == ArgType::PTR_OFFSET && code.result.type == ArgType::BB_TEMP) {
            varHasTempAssign.insert(code.arg1.value);
        }
    }
    for (auto [offset, cnt] : varRefCnts) {
        if (cnt < 2) {
            continue;
        }
        if (varHasTempAssign.contains(offset)) {
            // 一定有类似的赋给临时变量的语句，借用一下
            for (auto it = m_irs.begin(); it != m_irs.end(); ++it) {
                if (it->type == OpType::ASSIGN && it->arg1.type == ArgType::PTR_OFFSET && it->arg1.value == offset && it->result.type == ArgType::BB_TEMP) {
                    auto tempAssign = *it;
                    m_irs.erase(it);
                    bool isInserted = false;
                    for (auto it1 = m_irs.begin(); it1 != m_irs.end(); ++it1) {
                        if (it1->type == OpType::JNZ || it1->type == OpType::JZ) {
                            continue;
                        }
                        if (it1->arg1.type == ArgType::PTR_OFFSET && it1->arg1.value == offset || it1->arg2.type == ArgType::PTR_OFFSET && it1->arg2.value == offset) {
                            if (it1->arg1.type == ArgType::PTR_OFFSET) {
                                it1->arg1 = {ArgType::BB_TEMP, tempAssign.result.value};
                            } else {
                                it1->arg2 = {ArgType::BB_TEMP, tempAssign.result.value};
                            }
                            if (!isInserted) {
                                it1 = m_irs.insert(it1, tempAssign);
                                isInserted = true;
                            }
                        }
                    }
                    break;
                }
            }
        } else {
            // 插入新的赋值语句并替换
            bool isInserted = false;
            for (auto it1 = m_irs.begin(); it1 != m_irs.end(); ++it1) {
                if (it1->type == OpType::JNZ || it1->type == OpType::JZ) {
                    continue;
                }
                if (it1->arg1.type == ArgType::PTR_OFFSET && it1->arg1.value == offset || it1->arg2.type == ArgType::PTR_OFFSET && it1->arg2.value == offset) {
                    if (it1->arg1.type == ArgType::PTR_OFFSET) {
                        it1->arg1 = {ArgType::BB_TEMP, maxTempId};
                    } else {
                        it1->arg2 = {ArgType::BB_TEMP, maxTempId};
                    }
                    if (!isInserted) {
                        it1 = m_irs.emplace(it1, OpType::ASSIGN, QuadrupleArg{ArgType::BB_TEMP, maxTempId}, QuadrupleArg{ArgType::PTR_OFFSET, offset});
                        isInserted = true;
                    }
                }
            }
            maxTempId++;
        }
    }

    // 下移测试用临时变量的产生到最终赋值前
    if ((m_irs.back().type == OpType::JZ || m_irs.back().type == OpType::JNZ) && m_irs.back().arg1.type == ArgType::BB_TEMP) {
        int32_t testTemp =  m_irs.back().arg1.value;
        Quadruple targetCode;
        bool isFind = false;
        bool isInserted = false;
        for (auto it = m_irs.begin(); it != m_irs.end() - 1;) {
            if (!isFind) {
                if (it->result.type == ArgType::BB_TEMP && it->result.value == testTemp) {
                    targetCode = *it;
                    it = m_irs.erase(it);
                    isFind = true;
                } else {
                    ++it;
                }
            } else {
                if (it->arg1.type == ArgType::BB_TEMP && it->arg1.value == testTemp || it->arg2.type == ArgType::BB_TEMP && it->arg2.value == testTemp ||
                    it->type == OpType::ASSIGN && it->result.type == ArgType::PTR_OFFSET) {
                    m_irs.insert(it, targetCode);
                    isInserted = true;
                    break;
                }
                ++it;
            }
        }
        if (isFind && !isInserted) {
            m_irs.insert(m_irs.end() - 1, targetCode);
        }
    }

    // 让临时变量尽可能早落盘，上移变量写入，减少寄存器占用时间，临时变量写入变量上移到最后一次使用该变量和临时变量产出较靠后的那个后面
    // 要相信现代 cpu 的乱序执行，load-use 不那么会卡流水线了
    // 我已经不想管时间复杂度了。。。
    std::vector<Quadruple> assigns;
    for (auto it = m_irs.begin(); it != m_irs.end();) {
        if (auto& code = *it; code.type == OpType::ASSIGN && code.result.type == ArgType::PTR_OFFSET && code.arg1.type == ArgType::BB_TEMP) {
            assigns.push_back(code);
            it = m_irs.erase(it);
        } else {
            ++it;
        }
    }
    for (auto &assignCode : assigns) {
        int32_t maxI = 0;
        for (int32_t i = 0, size = m_irs.size(); i < size; ++i) {
            if (m_irs[i].arg1 == assignCode.result && m_irs[i].type != OpType::JNZ && m_irs[i].type != OpType::JZ || m_irs[i].arg2 == assignCode.result || m_irs[i].result == assignCode.arg1) {
                maxI = i;
            }
        }
        m_irs.insert(m_irs.begin() + maxI + 1, assignCode);
    }

    // t1 = v1 ± c1; v1 = t1; -> v1 = v1 ± c1;
    // t2 = v1 ± t1; v1 = t2; -> v1 = v1 ± t1;
    // 折叠 [ptr + offset] 的加减负，需要计算结果只被引用一次
    for (int32_t i = 0, size = m_irs.size() - 1; i < size;) {
        auto& code1 = m_irs[i];
        auto& code2 = m_irs[i + 1];
        if (code2.type != OpType::ASSIGN || code1.result != code2.arg1) {
            i++;
            continue;
        }
        if (((code1.type == OpType::ADD || code1.type == OpType::SUB) &&
            (code1.arg2.type == ArgType::CONSTANT || code1.arg2.type == ArgType::BB_TEMP) || code1.type == OpType::NEG) && code1.arg1 == code2.result) {
            bool canInline = true;
            for (int32_t j = i + 2, size1 = m_irs.size(); j < size1; j++) {
                if (m_irs[j].arg1 == code1.result || m_irs[j].arg2 == code1.result) {
                    canInline = false;
                    break;
                }
            }
            if (canInline) {
                code1.result = code1.arg1;
                m_irs.erase(m_irs.begin() + i + 1);
                i++;
                size--;
            } else {
                i += 2;
            }
        } else if (code1.type == OpType::ADD && code1.arg1.type == ArgType::BB_TEMP && code1.arg2 == code2.result) {
            bool canInline = true;
            for (int32_t j = i + 2, size1 = m_irs.size(); j < size1; j++) {
                if (m_irs[j].arg1 == code1.result || m_irs[j].arg2 == code1.result) {
                    canInline = false;
                    break;
                }
            }
            if (canInline) {
                code1.result = code1.arg2;
                m_irs.erase(m_irs.begin() + i + 1);
                i++;
                size--;
            } else {
                i += 2;
            }
        } else {
            i++;
        }
    }

    // 合并临时变量
    for (int32_t i = 0, size = m_irs.size(); i < size; i++) {
        if (auto& code = m_irs[i]; code.result.type == ArgType::BB_TEMP && (code.arg1.type == ArgType::BB_TEMP || code.arg2.type == ArgType::BB_TEMP)) {
            bool canReplace = true;
            int32_t searchId = code.arg1.type == ArgType::BB_TEMP ? code.arg1.value : code.arg2.value;
            for (int32_t j = i + 1; j < size; j++) {
                if (m_irs[j].arg1.type == ArgType::BB_TEMP && m_irs[j].arg1.value == searchId || m_irs[j].arg2.type == ArgType::BB_TEMP && m_irs[j].arg2.value == searchId) {
                    canReplace = false;
                    break;
                }
            }
            if (canReplace) {
                int32_t replaceId = code.result.value;
                code.result.value = searchId;
                for (int32_t j = i + 1; j < size; j++) {
                    if (m_irs[j].arg1.type == ArgType::BB_TEMP && m_irs[j].arg1.value == replaceId) {
                        m_irs[j].arg1.value = searchId;
                    }
                    if (m_irs[j].arg2.type == ArgType::BB_TEMP && m_irs[j].arg2.value == replaceId) {
                        m_irs[j].arg2.value = searchId;
                    }
                }
            }
        }
    }

    for (auto it = m_irs.begin(); it != m_irs.end(); ++it) {
        auto& code = *it;
        // 再确保常数模 256
        if (code.type != OpType::PTR_ADD) {
            if (code.arg1.type == ArgType::CONSTANT) {
                code.arg1.value = static_cast<int8_t>(code.arg1.value);
            }
            if (code.arg2.type == ArgType::CONSTANT) {
                code.arg2.value = static_cast<int8_t>(code.arg2.value);
            }
        }
        // 如果是乘 ±2，转成加法处理（为后面减负）
        if (code.type == OpType::MUL && code.arg2.type == ArgType::CONSTANT && (code.arg2.value == 2 || code.arg2.value == -2)) {
            code.type = OpType::ADD;
            code.arg2 = code.arg1;
            if (code.arg2.value == -2) {
                it = m_irs.emplace(it + 1, OpType::NEG, code.result, code.result);
            }
        }
        else if ((code.type == OpType::ADD || code.type == OpType::MUL) && code.arg1.type == ArgType::BB_TEMP && code.arg2.type == ArgType::PTR_OFFSET) {
            // 规范化加法和乘法，如果两个操作数是 v 和 t，v 在 t 左边
            std::swap(code.arg1, code.arg2);
        }
    }

    m_isRearranged = true;
    return m_irs.size();
}

void X64BasicBlock::generateWindows(OptimizationLevel level) {
    m_asms.clear();
    std::unordered_map<int32_t, TempAsmInfo> tempInfos;
    if (level != OptimizationLevel::O0) {
        collectTempInfo(tempInfos);
    }

    // 更新 zf 的最新临时变量
    int32_t zfTemp = -1;
    // 寄存器当前存的变量
    int32_t regUse[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    // 寄存器是 32 位更新还是 8 位更新
    bool eStates[16];
    // 溢出到栈的临时变量
    std::vector<int32_t> spills;
    // 勾使来了
    for (int32_t i = 0, size = m_irs.size(); i < size; i++) {
        auto &code = m_irs[i];
        switch (code.type) {
            case OpType::PTR_ADD:
                // m_asms.emplace_back(
                    // AsmOp::LEA,
                    // X64AsmOperand{false, Size::QWORD, Reg::BX},
                    // X64AsmOperand{true, Size::QWORD, Reg::BX, code.arg1.value}
                // );
                m_asms.emplace_back(
                    AsmOp::ADD,
                    X64RMOperand{false, Size::QWORD, Reg::BX},
                    code.arg1.value
                );
                zfTemp = -1;
                break;
            case OpType::ADD:
                if (code.result.type == ArgType::BB_TEMP) {
                    if (code.arg1.type == ArgType::PTR_OFFSET) {
                        if (code.arg2.type == ArgType::PTR_OFFSET) {
                            // t = v + v
                            auto reg = findReg(i, code.result.value, tempInfos, regUse, spills);
                            m_asms.emplace_back(
                                AsmOp::MOVZX,
                                X64RMOperand{false, Size::DWORD, reg},
                                X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                            );
                            m_asms.emplace_back(
                                AsmOp::ADD,
                                X64RMOperand{false, Size::BYTE, reg},
                                X64RMOperand{true, Size::BYTE, Reg::BX, code.arg2.value}
                            );
                            tempInfos[code.result.value].isSpill = false;
                            tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(reg);
                            regUse[static_cast<int32_t>(reg)] = code.result.value;
                            eStates[static_cast<int32_t>(reg)] = false;
                            zfTemp = code.result.value;
                        } else if (code.arg2.type == ArgType::BB_TEMP) {
                            // t = v + t
                            if (code.result.value == code.arg2.value) {
                                // 两个临时变量用同一个寄存器
                                auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                                m_asms.emplace_back(
                                    AsmOp::ADD,
                                    X64RMOperand{false, Size::BYTE, reg},
                                    X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                                );
                                eStates[static_cast<int32_t>(reg)] = false;
                                zfTemp = code.result.value;
                            } else {
                                // 产出新的临时变量
                                auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                                m_asms.emplace_back(
                                    AsmOp::MOVZX,
                                    X64RMOperand{false, Size::DWORD, resultReg},
                                    X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                                );
                                if (tempInfos[code.arg2.value].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, Size::BYTE, resultReg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg2.value].currentRegOrMem}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = false;
                                    zfTemp = code.result.value;
                                } else if (eStates[tempInfos[code.arg2.value].currentRegOrMem] && !isNextToCondJmp(i, code.result.value)) {
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, Size::DWORD, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = true;
                                    zfTemp = -1;
                                } else {
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, Size::BYTE, resultReg},
                                        X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = false;
                                    zfTemp = code.result.value;
                                }
                                tempInfos[code.result.value].isSpill = false;
                                tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                                regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            }
                        } else {
                            // t = v + c
                            auto reg = findReg(i, code.result.value, tempInfos, regUse, spills);
                            m_asms.emplace_back(
                                AsmOp::MOVZX,
                                X64RMOperand{false, Size::DWORD, reg},
                                X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                            );
                            bool useByte = isNextToCondJmp(i, code.result.value);
                            m_asms.emplace_back(
                                AsmOp::ADD,
                                X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, reg},
                                code.arg2.value
                            );
                            eStates[static_cast<int32_t>(reg)] = !useByte;
                            tempInfos[code.result.value].isSpill = false;
                            tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(reg);
                            regUse[static_cast<int32_t>(reg)] = code.result.value;
                            zfTemp = useByte ? code.result.value : -1;
                        }
                    } else {
                        if (code.arg2.type == ArgType::BB_TEMP) {
                            // t = t + t
                            if (code.result.value == code.arg1.value || code.result.value == code.arg2.value) {
                                auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                                int32_t anotherId = code.result.value == code.arg1.value ? code.arg2.value : code.arg1.value;
                                if (tempInfos[anotherId].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, Size::BYTE, reg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[anotherId].currentRegOrMem}
                                    );
                                    eStates[static_cast<int32_t>(reg)] = false;
                                    zfTemp = code.result.value;
                                } else if (eStates[static_cast<int32_t>(reg)] && eStates[tempInfos[anotherId].currentRegOrMem] && !isNextToCondJmp(i, code.result.value)) {
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, Size::DWORD, reg},
                                        X64RMOperand{false, Size::DWORD, static_cast<Reg>(tempInfos[anotherId].currentRegOrMem)}
                                    );
                                    eStates[static_cast<int32_t>(reg)] = true;
                                    zfTemp = -1;
                                } else {
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, Size::BYTE, reg},
                                        X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[anotherId].currentRegOrMem)}
                                    );
                                    eStates[static_cast<int32_t>(reg)] = false;
                                    zfTemp = code.result.value;
                                }
                            } else {
                                auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                                if (tempInfos[code.arg1.value].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg1.value].currentRegOrMem}
                                    );
                                    if (tempInfos[code.arg2.value].isSpill) {
                                        m_asms.emplace_back(
                                            AsmOp::ADD,
                                            X64RMOperand{false, Size::BYTE, resultReg},
                                            X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg2.value].currentRegOrMem}
                                        );
                                        eStates[static_cast<int32_t>(resultReg)] = false;
                                        zfTemp = code.result.value;
                                    } else if (eStates[tempInfos[code.arg2.value].currentRegOrMem] && !isNextToCondJmp(i, code.result.value)) {
                                        m_asms.emplace_back(
                                            AsmOp::ADD,
                                            X64RMOperand{false, Size::DWORD, resultReg},
                                            X64RMOperand{false, Size::DWORD, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                        );
                                        eStates[static_cast<int32_t>(resultReg)] = true;
                                        zfTemp = -1;
                                    } else {
                                        m_asms.emplace_back(
                                            AsmOp::ADD,
                                            X64RMOperand{false, Size::BYTE, resultReg},
                                            X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                        );
                                        eStates[static_cast<int32_t>(resultReg)] = false;
                                        zfTemp = code.result.value;
                                    }
                                } else if (tempInfos[code.arg2.value].isSpill) {
                                    bool arg1EStates = eStates[tempInfos[code.arg1.value].currentRegOrMem];
                                    m_asms.emplace_back(
                                        arg1EStates ? AsmOp::MOV : AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, arg1EStates ? Size::DWORD : Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                    );
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, Size::BYTE, resultReg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg2.value].currentRegOrMem}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = false;
                                    zfTemp = code.result.value;
                                } else if (!isNextToCondJmp(i, code.result.value) && eStates[tempInfos[code.arg1.value].currentRegOrMem] && eStates[tempInfos[code.arg2.value].currentRegOrMem]) {
                                    m_asms.emplace_back(
                                        AsmOp::LEA,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{true, Size::DWORD, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem), 0, 1, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = true;
                                    // 这里无须清除 zf 标记，因为若结果直接用于跳转则不会走这个分支
                                } else {
                                    bool arg1EStates = eStates[tempInfos[code.arg1.value].currentRegOrMem];
                                    m_asms.emplace_back(
                                        arg1EStates ? AsmOp::MOV : AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, arg1EStates ? Size::DWORD : Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                    );
                                    bool useByte = isNextToCondJmp(i, code.result.value) || !eStates[tempInfos[code.arg2.value].currentRegOrMem];
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, resultReg},
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                    );
                                    zfTemp = useByte ? code.result.value : -1;
                                    eStates[static_cast<int32_t>(resultReg)] = !useByte;
                                }
                                tempInfos[code.result.value].isSpill = false;
                                tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                                regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            }
                        } else {
                            // t = t + c
                            if (code.result.value == code.arg1.value) {
                                auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                                bool useByte = isNextToCondJmp(i, code.result.value) || !eStates[static_cast<int32_t>(reg)];
                                m_asms.emplace_back(
                                    AsmOp::ADD,
                                    X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, reg},
                                    code.arg2.value
                                );
                                eStates[static_cast<int32_t>(reg)] = !useByte;
                                zfTemp = useByte ? code.result.value : -1;
                            } else {
                                auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                                if (tempInfos[code.arg1.value].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg1.value].currentRegOrMem}
                                    );
                                    bool useByte = isNextToCondJmp(i, code.result.value);
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, resultReg},
                                        code.arg2.value
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = !useByte;
                                    zfTemp = useByte ? code.result.value : -1;
                                } else if (!isNextToCondJmp(i, code.result.value) && eStates[tempInfos[code.arg1.value].currentRegOrMem]) {
                                    m_asms.emplace_back(
                                        AsmOp::LEA,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{true, Size::DWORD, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem), code.arg2.value}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = true;
                                    // 这里无须清除 zf 标记，因为若结果直接用于跳转则不会走这个分支
                                } else {
                                    bool arg1EStates = eStates[tempInfos[code.arg1.value].currentRegOrMem];
                                    m_asms.emplace_back(
                                        arg1EStates ? AsmOp::MOV : AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, arg1EStates ? Size::DWORD : Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                    );
                                    bool useByte = isNextToCondJmp(i, code.result.value);
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, resultReg},
                                        code.arg2.value
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = !useByte;
                                    zfTemp = useByte ? code.result.value : -1;
                                }
                                tempInfos[code.result.value].isSpill = false;
                                tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                                regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            }
                        }
                    }
                } else {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        // v = v + t
                        m_asms.emplace_back(
                            AsmOp::ADD,
                            X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value},
                            X64RMOperand{false, Size::BYTE, ensureOldTempInReg(i, code.arg2.value, tempInfos, regUse, eStates, spills)}
                        );
                        zfTemp = -1;
                    } else {
                        // v = v + c
                        m_asms.emplace_back(
                            AsmOp::ADD,
                            X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value},
                            code.arg2.value
                        );
                        zfTemp = -1;
                    }
                }
                break;
            case OpType::SUB:
                if (code.result.type == ArgType::BB_TEMP) {
                    if (code.arg1.type == ArgType::PTR_OFFSET) {
                        if (code.arg2.type == ArgType::PTR_OFFSET) {
                            // t = v - v
                            auto reg = findReg(i, code.result.value, tempInfos, regUse, spills);
                            m_asms.emplace_back(
                                AsmOp::MOVZX,
                                X64RMOperand{false, Size::DWORD, reg},
                                X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                            );
                            m_asms.emplace_back(
                                AsmOp::SUB,
                                X64RMOperand{false, Size::BYTE, reg},
                                X64RMOperand{true, Size::BYTE, Reg::BX, code.arg2.value}
                            );
                            tempInfos[code.result.value].isSpill = false;
                            tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(reg);
                            regUse[static_cast<int32_t>(reg)] = code.result.value;
                            eStates[static_cast<int32_t>(reg)] = false;
                            zfTemp = code.result.value;
                        } else if (code.arg2.type == ArgType::BB_TEMP) {
                            // t = v - t
                            if (code.result.value == code.arg2.value) {
                                // 两个临时变量用同一个寄存器
                                auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                                m_asms.emplace_back(
                                    AsmOp::NEG,
                                    X64RMOperand{false, Size::BYTE, reg}
                                );
                                m_asms.emplace_back(
                                    AsmOp::ADD,
                                    X64RMOperand{false, Size::BYTE, reg},
                                    X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                                );
                                eStates[static_cast<int32_t>(reg)] = false;
                                zfTemp = code.result.value;
                            } else {
                                // 产出新的临时变量
                                auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                                m_asms.emplace_back(
                                    AsmOp::MOVZX,
                                    X64RMOperand{false, Size::DWORD, resultReg},
                                    X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                                );
                                if (tempInfos[code.arg2.value].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, Size::BYTE, resultReg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg2.value].currentRegOrMem}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = false;
                                    zfTemp = code.result.value;
                                } else if (eStates[tempInfos[code.arg2.value].currentRegOrMem] && !isNextToCondJmp(i, code.result.value)) {
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, Size::DWORD, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = true;
                                    zfTemp = -1;
                                } else {
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, Size::BYTE, resultReg},
                                        X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = false;
                                    zfTemp = code.result.value;
                                }
                                tempInfos[code.result.value].isSpill = false;
                                tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                                regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            }
                        } else {
                            // t = v - c
                            auto reg = findReg(i, code.result.value, tempInfos, regUse, spills);
                            m_asms.emplace_back(
                                AsmOp::MOVZX,
                                X64RMOperand{false, Size::DWORD, reg},
                                X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                            );
                            bool useByte = isNextToCondJmp(i, code.result.value);
                            m_asms.emplace_back(
                                AsmOp::SUB,
                                X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, reg},
                                code.arg2.value
                            );
                            eStates[static_cast<int32_t>(reg)] = !useByte;
                            tempInfos[code.result.value].isSpill = false;
                            tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(reg);
                            regUse[static_cast<int32_t>(reg)] = code.result.value;
                            zfTemp = useByte ? code.result.value : -1;
                        }
                    } else if (code.arg1.type == ArgType::BB_TEMP) {
                        if (code.arg2.type == ArgType::PTR_OFFSET) {
                            // t = t - v
                            if (code.result.value == code.arg1.value) {
                                auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                                m_asms.emplace_back(
                                    AsmOp::SUB,
                                    X64RMOperand{false, Size::BYTE, reg},
                                    X64RMOperand{true, Size::BYTE, Reg::BX, code.arg2.value}
                                );
                                eStates[static_cast<int32_t>(reg)] = false;
                            } else {
                                auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                                if (tempInfos[code.arg1.value].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg1.value].currentRegOrMem}
                                    );
                                } else {
                                    bool arg1EStates = eStates[tempInfos[code.arg1.value].currentRegOrMem];
                                    m_asms.emplace_back(
                                        arg1EStates ? AsmOp::MOV : AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, arg1EStates ? Size::DWORD : Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                    );
                                }
                                m_asms.emplace_back(
                                    AsmOp::SUB,
                                    X64RMOperand{false, Size::BYTE, resultReg},
                                    X64RMOperand{true, Size::BYTE, Reg::BX, code.arg2.value}
                                );
                                eStates[static_cast<int32_t>(resultReg)] = false;
                                tempInfos[code.result.value].isSpill = false;
                                tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                                regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            }
                            zfTemp = code.result.value;
                        } else if (code.arg2.type == ArgType::BB_TEMP) {
                            // t = t - t
                            if (code.result.value == code.arg1.value) {
                                auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                                if (tempInfos[code.arg2.value].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, Size::BYTE, reg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg2.value].currentRegOrMem}
                                    );
                                    zfTemp = code.result.value;
                                    eStates[static_cast<int32_t>(reg)] = false;
                                } else if (eStates[static_cast<int32_t>(reg)] && eStates[tempInfos[code.arg2.value].currentRegOrMem] && !isNextToCondJmp(i, code.result.value)) {
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, Size::DWORD, reg},
                                        X64RMOperand{false, Size::DWORD, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                    );
                                    zfTemp = -1;
                                    eStates[static_cast<int32_t>(reg)] = true;
                                } else {
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, Size::BYTE, reg},
                                        X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                    );
                                    zfTemp = code.result.value;
                                    eStates[static_cast<int32_t>(reg)] = false;
                                }
                            } else if (code.result.value == code.arg2.value) {
                                auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                                m_asms.emplace_back(
                                    AsmOp::NEG,
                                    X64RMOperand{false, eStates[static_cast<int32_t>(reg)] ? Size::DWORD : Size::BYTE, reg}
                                );
                                if (tempInfos[code.arg1.value].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, Size::BYTE, reg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg1.value].currentRegOrMem}
                                    );
                                    zfTemp = code.result.value;
                                    eStates[static_cast<int32_t>(reg)] = false;
                                } else if (eStates[static_cast<int32_t>(reg)] && eStates[tempInfos[code.arg1.value].currentRegOrMem] && !isNextToCondJmp(i, code.result.value)) {
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, Size::DWORD, reg},
                                        X64RMOperand{false, Size::DWORD, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                    );
                                    zfTemp = -1;
                                    eStates[static_cast<int32_t>(reg)] = true;
                                } else {
                                    m_asms.emplace_back(
                                        AsmOp::ADD,
                                        X64RMOperand{false, Size::BYTE, reg},
                                        X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                    );
                                    zfTemp = code.result.value;
                                    eStates[static_cast<int32_t>(reg)] = false;
                                }
                            } else {
                                auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                                if (tempInfos[code.arg1.value].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg1.value].currentRegOrMem}
                                    );
                                    if (tempInfos[code.arg2.value].isSpill) {
                                        m_asms.emplace_back(
                                            AsmOp::SUB,
                                            X64RMOperand{false, Size::BYTE, resultReg},
                                            X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg2.value].currentRegOrMem}
                                        );
                                        eStates[static_cast<int32_t>(resultReg)] = false;
                                        zfTemp = code.result.value;
                                    } else if (eStates[tempInfos[code.arg2.value].currentRegOrMem] && !isNextToCondJmp(i, code.result.value)) {
                                        m_asms.emplace_back(
                                            AsmOp::SUB,
                                            X64RMOperand{false, Size::DWORD, resultReg},
                                            X64RMOperand{false, Size::DWORD, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                        );
                                        eStates[static_cast<int32_t>(resultReg)] = true;
                                        zfTemp = -1;
                                    } else {
                                        m_asms.emplace_back(
                                            AsmOp::SUB,
                                            X64RMOperand{false, Size::BYTE, resultReg},
                                            X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                        );
                                        eStates[static_cast<int32_t>(resultReg)] = false;
                                        zfTemp = code.result.value;
                                    }
                                } else if (tempInfos[code.arg2.value].isSpill) {
                                    bool arg1EStates = eStates[tempInfos[code.arg1.value].currentRegOrMem];
                                    m_asms.emplace_back(
                                        arg1EStates ? AsmOp::MOV : AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, arg1EStates ? Size::DWORD : Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                    );
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, Size::BYTE, resultReg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg2.value].currentRegOrMem}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = false;
                                    zfTemp = code.result.value;
                                } else {
                                    bool arg1EStates = eStates[tempInfos[code.arg1.value].currentRegOrMem];
                                    m_asms.emplace_back(
                                        arg1EStates ? AsmOp::MOV : AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, arg1EStates ? Size::DWORD : Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                    );
                                    bool useByte = isNextToCondJmp(i, code.result.value) || !eStates[tempInfos[code.arg2.value].currentRegOrMem];
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, resultReg},
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                    );
                                    zfTemp = useByte ? code.result.value : -1;
                                    eStates[static_cast<int32_t>(resultReg)] = !useByte;
                                }
                                tempInfos[code.result.value].isSpill = false;
                                tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                                regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            }
                        } else {
                            // t = t - c
                            if (code.result.value == code.arg1.value) {
                                auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                                bool useByte = isNextToCondJmp(i, code.result.value) || !eStates[static_cast<int32_t>(reg)];
                                m_asms.emplace_back(
                                    AsmOp::SUB,
                                    X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, reg},
                                    code.arg2.value
                                );
                                eStates[static_cast<int32_t>(reg)] = !useByte;
                                zfTemp = useByte ? code.result.value : -1;
                            } else {
                                auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                                if (tempInfos[code.arg1.value].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg1.value].currentRegOrMem}
                                    );
                                    bool useByte = isNextToCondJmp(i, code.result.value);
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, resultReg},
                                        code.arg2.value
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = !useByte;
                                    zfTemp = useByte ? code.result.value : -1;
                                } else if (!isNextToCondJmp(i, code.result.value) && eStates[tempInfos[code.arg1.value].currentRegOrMem]) {
                                    m_asms.emplace_back(
                                        AsmOp::LEA,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{true, Size::DWORD, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem), static_cast<int8_t>(-code.arg2.value)}
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = true;
                                    // 这里无须清除 zf 标记，因为若结果直接用于跳转则不会走这个分支
                                } else {
                                    bool arg1EStates = eStates[tempInfos[code.arg1.value].currentRegOrMem];
                                    m_asms.emplace_back(
                                        arg1EStates ? AsmOp::MOV : AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, arg1EStates ? Size::DWORD : Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                    );
                                    bool useByte = isNextToCondJmp(i, code.result.value);
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, resultReg},
                                        code.arg2.value
                                    );
                                    eStates[static_cast<int32_t>(resultReg)] = !useByte;
                                    zfTemp = useByte ? code.result.value : -1;
                                }
                                tempInfos[code.result.value].isSpill = false;
                                tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                                regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            }
                        }
                    } else {
                        if (code.arg2.type == ArgType::PTR_OFFSET) {
                            // t = c - v
                            auto reg = findReg(i, code.result.value, tempInfos, regUse, spills);
                            m_asms.emplace_back(
                                AsmOp::MOV,
                                X64RMOperand{false, Size::BYTE, reg},
                                code.arg1.value
                            );
                            m_asms.emplace_back(
                                AsmOp::SUB,
                                X64RMOperand{false, Size::BYTE, reg},
                                X64RMOperand{true, Size::BYTE, Reg::BX, code.arg2.value}
                            );
                            zfTemp = code.result.value;
                            tempInfos[code.result.value].isSpill = false;
                            tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(reg);
                            regUse[static_cast<int32_t>(reg)] = code.result.value;
                            eStates[static_cast<int32_t>(reg)] = false;
                        } else {
                            // t = c - t
                            if (code.result.value == code.arg2.value) {
                                auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                                bool useByte = isNextToCondJmp(i, code.result.value) || !eStates[static_cast<int32_t>(reg)];
                                m_asms.emplace_back(
                                    AsmOp::NEG,
                                    X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, reg}
                                );
                                m_asms.emplace_back(
                                    AsmOp::ADD,
                                    X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, reg},
                                    code.arg1.value
                                );
                                zfTemp = useByte ? code.result.value : -1;
                                eStates[static_cast<int32_t>(reg)] = !useByte;
                            } else {
                                auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                                if (tempInfos[code.arg2.value].isSpill) {
                                    m_asms.emplace_back(
                                        AsmOp::MOV,
                                        X64RMOperand{false, Size::BYTE, resultReg},
                                        code.arg1.value
                                    );
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, Size::BYTE, resultReg},
                                        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg2.value].currentRegOrMem}
                                    );
                                    zfTemp = code.result.value;
                                    eStates[static_cast<int32_t>(resultReg)] = false;
                                } else {
                                    bool useByte = isNextToCondJmp(i, code.result.value) || !eStates[tempInfos[code.arg2.value].currentRegOrMem];
                                    m_asms.emplace_back(
                                        AsmOp::MOV,
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, resultReg},
                                        code.arg1.value
                                    );
                                    m_asms.emplace_back(
                                        AsmOp::SUB,
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, resultReg},
                                        X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem)}
                                    );
                                    zfTemp = useByte ? code.result.value : -1;
                                    eStates[static_cast<int32_t>(resultReg)] = !useByte;
                                }
                                tempInfos[code.result.value].isSpill = false;
                                tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                                regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            }
                        }
                    }
                } else {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        // v = v - t
                        m_asms.emplace_back(
                            AsmOp::SUB,
                            X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value},
                            X64RMOperand{false, Size::BYTE, ensureOldTempInReg(i, code.arg2.value, tempInfos, regUse, eStates, spills)}
                        );
                        zfTemp = -1;
                    } else {
                        // v = v - c
                        m_asms.emplace_back(
                            AsmOp::SUB,
                            X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value},
                            code.arg2.value
                        );
                        zfTemp = -1;
                    }
                }
                break;
            case OpType::MUL:
                if (code.arg1.type == ArgType::PTR_OFFSET) {
                    if (code.arg2.type == ArgType::PTR_OFFSET) {
                        // t = v * v
                        auto reg = findReg(i, code.result.value, tempInfos, regUse, spills);
                        auto reg1 = findAnotherRegForMul(i, reg, tempInfos, regUse, spills);
                        m_asms.emplace_back(
                            AsmOp::MOVZX,
                            X64RMOperand{false, Size::DWORD, reg},
                            X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                        );
                        m_asms.emplace_back(
                            AsmOp::MOVZX,
                            X64RMOperand{false, Size::DWORD, reg1},
                            X64RMOperand{true, Size::BYTE, Reg::BX, code.arg2.value}
                        );
                        m_asms.emplace_back(
                            AsmOp::IMUL,
                            X64RMOperand{false, Size::DWORD, reg},
                            X64RMOperand{false, Size::DWORD, reg1}
                        );
                        tempInfos[code.result.value].isSpill = false;
                        tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(reg);
                        regUse[static_cast<int32_t>(reg)] = code.result.value;
                        eStates[static_cast<int32_t>(reg)] = true;
                        zfTemp = -1;
                    } else if (code.arg2.type == ArgType::BB_TEMP) {
                        // t = v * t
                        auto regTemp = ensureOldTempInReg(i, code.arg2.value, tempInfos, regUse, eStates, spills);
                        if (code.result.value == code.arg2.value) {
                            auto reg1 = findAnotherRegForMul(i, std::nullopt, tempInfos, regUse, spills);
                            m_asms.emplace_back(
                                AsmOp::MOVZX,
                                X64RMOperand{false, Size::DWORD, reg1},
                                X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                            );
                            if (!eStates[static_cast<int32_t>(regTemp)]) {
                                m_asms.emplace_back(
                                    AsmOp::MOVZX,
                                    X64RMOperand{false, Size::DWORD, regTemp},
                                    X64RMOperand{false, Size::BYTE, regTemp}
                                );
                            }
                            m_asms.emplace_back(
                                AsmOp::IMUL,
                                X64RMOperand{false, Size::DWORD, regTemp},
                                X64RMOperand{false, Size::DWORD, reg1}
                            );
                            eStates[static_cast<int32_t>(regTemp)] = true;
                        } else {
                            auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                            m_asms.emplace_back(
                                AsmOp::MOVZX,
                                X64RMOperand{false, Size::DWORD, resultReg},
                                X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                            );
                            if (!eStates[static_cast<int32_t>(regTemp)]) {
                                m_asms.emplace_back(
                                    AsmOp::MOVZX,
                                    X64RMOperand{false, Size::DWORD, regTemp},
                                    X64RMOperand{false, Size::BYTE, regTemp}
                                );
                                eStates[static_cast<int32_t>(regTemp)] = true;
                            }
                            m_asms.emplace_back(
                                AsmOp::IMUL,
                                X64RMOperand{false, Size::DWORD, resultReg},
                                X64RMOperand{false, Size::DWORD, regTemp}
                            );
                            tempInfos[code.result.value].isSpill = false;
                            tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                            regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            eStates[static_cast<int32_t>(resultReg)] = true;
                        }
                        zfTemp = -1;
                    } else {
                        // t = v * c
                        auto reg = findReg(i, code.result.value, tempInfos, regUse, spills);
                        m_asms.emplace_back(
                            AsmOp::MOVZX,
                            X64RMOperand{false, Size::DWORD, reg},
                            X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                        );
                        eStates[static_cast<int32_t>(reg)] = true;
                        zfTemp = imulConstant(i, code.result.value, reg, reg, code.arg2.value, zfTemp, eStates);
                        tempInfos[code.result.value].isSpill = false;
                        tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(reg);
                        regUse[static_cast<int32_t>(reg)] = code.result.value;
                    }
                } else {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        // t = t * t
                        if (code.result.value == code.arg1.value || code.result.value == code.arg2.value) {
                            auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                            int32_t anotherId = code.result.value == code.arg1.value ? code.arg2.value : code.arg1.value;
                            auto reg1 = ensureOldTempInReg(i, anotherId, tempInfos, regUse, eStates, spills);
                            if (!eStates[static_cast<int32_t>(reg)]) {
                                m_asms.emplace_back(
                                    AsmOp::MOVZX,
                                    X64RMOperand{false, Size::DWORD, reg},
                                    X64RMOperand{false, Size::BYTE, reg}
                                );
                            }
                            if (!eStates[static_cast<int32_t>(reg1)]) {
                                m_asms.emplace_back(
                                    AsmOp::MOVZX,
                                    X64RMOperand{false, Size::DWORD, reg1},
                                    X64RMOperand{false, Size::BYTE, reg1}
                                );
                                eStates[static_cast<int32_t>(reg1)] = true;
                            }
                            m_asms.emplace_back(
                                AsmOp::IMUL,
                                X64RMOperand{false, Size::DWORD, reg},
                                X64RMOperand{false, Size::DWORD, reg1}
                            );
                            eStates[static_cast<int32_t>(reg)] = true;
                        } else {
                            auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                            if (tempInfos[code.arg1.value].isSpill || tempInfos[code.arg2.value].isSpill) {
                                int32_t toResultId = tempInfos[code.arg1.value].isSpill ? code.arg1.value : code.arg2.value;
                                int32_t checkId = tempInfos[code.arg1.value].isSpill ? code.arg2.value : code.arg1.value;
                                m_asms.emplace_back(
                                    AsmOp::MOVZX,
                                    X64RMOperand{false, Size::DWORD, resultReg},
                                    X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[toResultId].currentRegOrMem}
                                );
                                auto reg1 = ensureOldTempInReg(i, checkId, tempInfos, regUse, eStates, spills);
                                if (!eStates[static_cast<int32_t>(reg1)]) {
                                    m_asms.emplace_back(
                                        AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, reg1},
                                        X64RMOperand{true, Size::BYTE, reg1}
                                    );
                                    eStates[static_cast<int32_t>(reg1)] = true;
                                }
                                m_asms.emplace_back(
                                    AsmOp::IMUL,
                                    X64RMOperand{false, Size::DWORD, resultReg},
                                    X64RMOperand{false, Size::DWORD, reg1}
                                );
                            } else {
                                auto reg1 = static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem);
                                auto reg2 = static_cast<Reg>(tempInfos[code.arg2.value].currentRegOrMem);
                                if (eStates[static_cast<int32_t>(reg1)] || eStates[static_cast<int32_t>(reg2)]) {
                                    auto toResultReg = eStates[static_cast<int32_t>(reg1)] ? reg1 : reg2;
                                    auto checkReg = eStates[static_cast<int32_t>(reg1)] ? reg2 : reg1;
                                    m_asms.emplace_back(
                                        AsmOp::MOV,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, Size::DWORD, toResultReg}
                                    );
                                    if (!eStates[static_cast<int32_t>(checkReg)]) {
                                        m_asms.emplace_back(
                                            AsmOp::MOVZX,
                                            X64RMOperand{false, Size::DWORD, checkReg},
                                            X64RMOperand{false, Size::BYTE, checkReg}
                                        );
                                        eStates[static_cast<int32_t>(checkReg)] = true;
                                    }
                                    m_asms.emplace_back(
                                        AsmOp::IMUL,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, Size::DWORD, checkReg}
                                    );
                                } else {
                                    m_asms.emplace_back(
                                        AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, Size::BYTE, reg1}
                                    );
                                    m_asms.emplace_back(
                                        AsmOp::MOVZX,
                                        X64RMOperand{false, Size::DWORD, reg2},
                                        X64RMOperand{false, Size::BYTE, reg2}
                                    );
                                    eStates[static_cast<int32_t>(reg2)] = true;
                                    m_asms.emplace_back(
                                        AsmOp::IMUL,
                                        X64RMOperand{false, Size::DWORD, resultReg},
                                        X64RMOperand{false, Size::DWORD, reg2}
                                    );
                                }
                            }
                            tempInfos[code.result.value].isSpill = false;
                            tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                            regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            eStates[static_cast<int32_t>(resultReg)] = true;
                        }
                        zfTemp = -1;
                    } else {
                        // t = t * c
                        auto regTemp = ensureOldTempInReg(i, code.arg1.value, tempInfos, regUse, eStates, spills);
                        if (code.result.value == code.arg1.value) {
                            zfTemp = imulConstant(i, code.result.value, regTemp, regTemp, code.arg2.value, zfTemp, eStates);
                        } else {
                            auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                            zfTemp = imulConstant(i, code.result.value, regTemp, resultReg, code.arg2.value, zfTemp, eStates);
                            tempInfos[code.result.value].isSpill = false;
                            tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                            regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                        }
                    }
                }
                break;
            case OpType::NEG:
                if (code.result.type == ArgType::BB_TEMP) {
                    if (code.arg1.type == ArgType::PTR_OFFSET) {
                        // t = -v
                        auto reg = findReg(i, code.result.value, tempInfos, regUse, spills);
                        m_asms.emplace_back(
                            AsmOp::MOVZX,
                            X64RMOperand{false, Size::DWORD, reg},
                            X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                        );
                        // 由于被零拓展并被立刻使用，不需要考虑是否被用作跳转条件而使用窄运算
                        m_asms.emplace_back(
                            AsmOp::NEG,
                            X64RMOperand{false, Size::DWORD, reg}
                        );
                        tempInfos[code.result.value].isSpill = false;
                        tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(reg);
                        regUse[static_cast<int32_t>(reg)] = code.result.value;
                        eStates[static_cast<int32_t>(reg)] = true;
                        zfTemp = code.result.value;
                    } else {
                        // t = -t
                        if (code.result.value == code.arg1.value) {
                            auto reg = ensureOldTempInReg(i, code.result.value, tempInfos, regUse, eStates, spills);
                            bool useByte = isNextToCondJmp(i, code.result.value) || !eStates[static_cast<int32_t>(reg)];
                            m_asms.emplace_back(
                                AsmOp::NEG,
                                X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, reg}
                            );
                            eStates[static_cast<int32_t>(reg)] = !useByte;
                            zfTemp = useByte ? code.result.value : -1;
                        } else {
                            auto resultReg = findReg(i, code.result.value, tempInfos, regUse, spills);
                            if (tempInfos[code.arg1.value].isSpill) {
                                m_asms.emplace_back(
                                    AsmOp::MOVZX,
                                    X64RMOperand{false, Size::DWORD, resultReg},
                                    X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg1.value].currentRegOrMem}
                                );
                            } else if (eStates[tempInfos[code.arg1.value].currentRegOrMem]) {
                                m_asms.emplace_back(
                                    AsmOp::MOV,
                                    X64RMOperand{false, Size::DWORD, resultReg},
                                    X64RMOperand{false, Size::DWORD, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                );
                            } else {
                                m_asms.emplace_back(
                                    AsmOp::MOVZX,
                                    X64RMOperand{false, Size::DWORD, resultReg},
                                    X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                                );
                            }
                            bool useByte = isNextToCondJmp(i, code.result.value);
                            m_asms.emplace_back(
                                AsmOp::NEG,
                                X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, resultReg}
                            );
                            tempInfos[code.result.value].isSpill = false;
                            tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(resultReg);
                            regUse[static_cast<int32_t>(resultReg)] = code.result.value;
                            eStates[static_cast<int32_t>(resultReg)] = !useByte;
                            zfTemp = useByte ? code.result.value : -1;
                        }
                    }
                } else {
                    // v = -v
                    m_asms.emplace_back(
                        AsmOp::NEG,
                        X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                    );
                    zfTemp = -1;
                }
                break;
            case OpType::ASSIGN:
                if (code.result.type == ArgType::PTR_OFFSET) {
                    if (code.arg1.type == ArgType::CONSTANT) {
                        // v = c
                        m_asms.emplace_back(
                            AsmOp::MOV,
                            X64RMOperand{true, Size::BYTE, Reg::BX, code.result.value},
                            code.arg1.value
                        );
                    } else {
                        // v = t
                        m_asms.emplace_back(
                            AsmOp::MOV,
                            X64RMOperand{true, Size::BYTE, Reg::BX, code.result.value},
                            X64RMOperand{false, Size::BYTE, ensureOldTempInReg(i, code.arg1.value, tempInfos, regUse, eStates, spills)}
                        );
                    }
                } else {
                    // t = v
                    auto reg = findReg(i, code.result.value, tempInfos, regUse, spills);
                    m_asms.emplace_back(
                        AsmOp::MOVZX,
                        X64RMOperand{false, Size::DWORD, reg},
                        X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                    );
                    tempInfos[code.result.value].isSpill = false;
                    tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(reg);
                    regUse[static_cast<int32_t>(reg)] = code.result.value;
                    eStates[static_cast<int32_t>(reg)] = true;
                }
                break;
            case OpType::JNZ:
                if (code.arg1.type == ArgType::PTR_OFFSET) {
                    m_asms.emplace_back(
                        AsmOp::CMP,
                        X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value},
                        0
                    );
                } else if (zfTemp != code.arg1.value) {
                    if (tempInfos[code.arg1.value].isSpill) {
                        m_asms.emplace_back(
                            AsmOp::CMP,
                            X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg1.value].currentRegOrMem},
                            0
                        );
                    } else {
                        m_asms.emplace_back(
                            AsmOp::TEST,
                            X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)},
                            X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                        );
                    }
                }
                m_asms.emplace_back(AsmOp::JNZ, 0);
                break;
            case OpType::JZ:
                if (code.arg1.type == ArgType::PTR_OFFSET) {
                    m_asms.emplace_back(
                        AsmOp::CMP,
                        X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value},
                        0
                    );
                } else if (zfTemp != code.arg1.value) {
                    if (tempInfos[code.arg1.value].isSpill) {
                        m_asms.emplace_back(
                            AsmOp::CMP,
                            X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg1.value].currentRegOrMem},
                            0
                        );
                    } else {
                        m_asms.emplace_back(
                            AsmOp::TEST,
                            X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)},
                            X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                        );
                    }
                }
                m_asms.emplace_back(AsmOp::JZ, 0);
                break;
            case OpType::PUTCHAR:
                if (code.arg1.type == ArgType::PTR_OFFSET) {
                    m_asms.emplace_back(
                        AsmOp::MOVZX,
                        X64RMOperand{false, Size::DWORD, Reg::CX},
                        X64RMOperand{true, Size::BYTE, Reg::BX, code.arg1.value}
                    );
                } else if (code.arg1.type == ArgType::BB_TEMP) {
                    if (tempInfos[code.arg1.value].isSpill) {
                        m_asms.emplace_back(
                            AsmOp::MOVZX,
                            X64RMOperand{false, Size::DWORD, Reg::CX},
                            X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[code.arg1.value].currentRegOrMem}
                        );
                    } else if (static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem) != Reg::CX) {
                        if (eStates[tempInfos[code.arg1.value].currentRegOrMem]) {
                            m_asms.emplace_back(
                                AsmOp::MOV,
                                X64RMOperand{false, Size::DWORD, Reg::CX},
                                X64RMOperand{false, Size::DWORD, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                            );
                        } else {
                            m_asms.emplace_back(
                                AsmOp::MOVZX,
                                X64RMOperand{false, Size::DWORD, Reg::CX},
                                X64RMOperand{false, Size::BYTE, static_cast<Reg>(tempInfos[code.arg1.value].currentRegOrMem)}
                            );
                        }
                    } else if (!eStates[static_cast<int32_t>(Reg::CX)]) {
                        m_asms.emplace_back(
                            AsmOp::MOVZX,
                            X64RMOperand{false, Size::DWORD, Reg::CX},
                            X64RMOperand{false, Size::BYTE, Reg::CX}
                        );
                    }
                } else {
                    m_asms.emplace_back(
                        AsmOp::MOV,
                        X64RMOperand{false, Size::DWORD, Reg::CX},
                        code.arg1.value
                    );
                }
                m_asms.emplace_back(AsmOp::CALL, X64RMOperand{false, Size::QWORD, Reg::SI});
                zfTemp = -1;
                break;
            case OpType::GETCHAR:
                m_asms.emplace_back(AsmOp::CALL, X64RMOperand{false, Size::QWORD, Reg::DI});
                if (code.result.type == ArgType::BB_TEMP) {
                    // 首先判断这个临时变量有没有跨函数，如果没有那就先放在 ax，否则 mov
                    if (tempInfos[code.result.value].isCrossFuncs) {
                        auto reg = findReg(i, code.result.value, tempInfos, regUse, spills);
                        m_asms.emplace_back(
                            AsmOp::MOV,
                            X64RMOperand{false, Size::DWORD, reg},
                            X64RMOperand{false, Size::DWORD, Reg::AX}
                        );
                        tempInfos[code.result.value].isSpill = false;
                        tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(reg);
                        regUse[static_cast<int32_t>(reg)] = code.result.value;
                        eStates[static_cast<int32_t>(reg)] = true;
                    } else {
                        tempInfos[code.result.value].isSpill = false;
                        tempInfos[code.result.value].currentRegOrMem = static_cast<int32_t>(Reg::AX);
                        regUse[static_cast<int32_t>(Reg::AX)] = code.result.value;
                        eStates[static_cast<int32_t>(Reg::AX)] = true;
                    }
                } else {
                    m_asms.emplace_back(
                        AsmOp::MOV,
                        X64RMOperand{true, Size::BYTE, Reg::BX, code.result.value},
                        X64RMOperand{false, Size::BYTE, Reg::AX}
                    );
                }
                zfTemp = -1;
                break;
            default:
                break;
        }
    }

    m_spillCount = spills.size();
    postOptimize(level); // 过一遍后优化

    // 程序结束代码块
    if (m_succN == nullptr && m_succY == nullptr) {
        m_asms.emplace_back(
            AsmOp::MOV,
            X64RMOperand{false, Size::QWORD, Reg::AX},
            X64RMOperand{true, Size::QWORD, Reg::SP, 32}
        );
        m_asms.emplace_back(
            AsmOp::XOR,
            X64RMOperand{false, Size::DWORD, Reg::CX},
            X64RMOperand{false, Size::DWORD, Reg::CX}
        );
        m_asms.emplace_back(AsmOp::CALL, X64RMOperand{false, Size::QWORD, Reg::AX});
    }
}

void X64BasicBlock::collectTempInfo(std::unordered_map<int32_t, TempAsmInfo>& tempInfos) const {
    // 临时变量信息收集
    for (int32_t i = m_irs.size() - 1; i >= 0; --i) {
        auto& code = m_irs[i];
        if (code.arg1.type == ArgType::BB_TEMP) {
            if (!tempInfos.contains(code.arg1.value)) {
                tempInfos[code.arg1.value] = {-1, i, {i}};
            } else {
                tempInfos[code.arg1.value].usePos.insert(i);
            }
            if (code.type == OpType::PUTCHAR) {
                tempInfos[code.arg1.value].isUsedByPutchar = true;
            }
        }
        if (code.arg2.type == ArgType::BB_TEMP) {
            if (!tempInfos.contains(code.arg2.value)) {
                tempInfos[code.arg2.value] = {-1, i, {i}};
            } else {
                tempInfos[code.arg2.value].usePos.insert(i);
            }
        }
    }
    for (int32_t i = 0, size = m_irs.size(); i < size; i++) {
        if (auto& code = m_irs[i]; code.result.type == ArgType::BB_TEMP && tempInfos[code.result.value].liveBegin < 0) {
            tempInfos[code.result.value].liveBegin = i;
        }
    }
    for (int32_t i = 0, size = m_irs.size(); i < size; i++) {
        if (auto& code = m_irs[i]; code.type == OpType::PUTCHAR || code.type == OpType::GETCHAR) {
            for (auto& info : tempInfos | std::views::values) {
                if (i > info.liveBegin && i < info.liveEnd) {
                    info.isCrossFuncs = true;
                }
            }
        }
    }
}

void X64BasicBlock::postOptimize(OptimizationLevel level) {
    if (level != OptimizationLevel::O0) {
        // 连续内存常量赋值优化
        for (auto it = m_asms.begin(); it != m_asms.end();) {
            if (it->op == AsmOp::MOV && it->rm1.isMem && it->rm1.reg == Reg::BX && it->rm1.size == Size::BYTE && it->hasImm) {
                int32_t offset = it->rm1.disp;
                int8_t data[4] = {static_cast<int8_t>(it->imm)};
                auto size = Size::BYTE;
                if (auto next1 = it + 1; next1 != m_asms.end() &&
                    next1->op == AsmOp::MOV && next1->rm1.isMem && next1->rm1.reg == Reg::BX &&
                    next1->rm1.disp == offset + 1 && next1->rm1.size == Size::BYTE && next1->hasImm) {
                    size = Size::WORD;
                    data[1] = static_cast<int8_t>(next1->imm);
                    if (auto next2 = next1 + 1; next2 != m_asms.end()) {
                        if (auto next3 = next2 + 1; next3 != m_asms.end()
                            && next2->op == AsmOp::MOV && next2->rm1.isMem && next2->rm1.reg == Reg::BX
                            && next2->rm1.disp == offset + 2 && next2->rm1.size == Size::BYTE && next2->hasImm
                            && next3->op == AsmOp::MOV && next3->rm1.isMem && next3->rm1.reg == Reg::BX
                            && next3->rm1.disp == offset + 3 && next3->rm1.size == Size::BYTE && next3->hasImm) {
                            size = Size::DWORD;
                            data[2] = static_cast<int8_t>(next2->imm);
                            data[3] = static_cast<int8_t>(next3->imm);
                        }
                    }
                }
                if (size == Size::WORD) {
                    it->rm1.size = Size::WORD;
                    it->imm = static_cast<int32_t>(*reinterpret_cast<int16_t*>(data));
                    it = m_asms.erase(it + 1, it + 2);
                } else if (size == Size::DWORD) {
                    it->rm1.size = Size::DWORD;
                    it->imm = *reinterpret_cast<int32_t*>(data);
                    it = m_asms.erase(it + 1, it + 4);
                } else {
                    ++it;
                }
            } else {
                ++it;
            }
        }

        // todo：为什么会出现这种屎代码？有更好的写法吗？
        // 如果最后一条语句是条件跳转
        if (!m_asms.empty() && (m_asms.back().op == AsmOp::JNZ || m_asms.back().op == AsmOp::JZ)) {
            // 跳转运算优化部分
            // 查找最后一条设置 flags 的运算，只需要为加减且第一个操作数是寄存器
            auto setFlagsIt = m_asms.rbegin() + 1;
            bool canOptimize1 = false;
            for (auto end = m_asms.rend(); setFlagsIt != end; ++setFlagsIt) {
                if (setFlagsIt->op == AsmOp::ADD || setFlagsIt->op == AsmOp::SUB || setFlagsIt->op == AsmOp::CMP || setFlagsIt->op == AsmOp::SHL || setFlagsIt->op == AsmOp::NEG
                    || setFlagsIt->op == AsmOp::IMUL || setFlagsIt->op == AsmOp::TEST || setFlagsIt->op == AsmOp::XOR || setFlagsIt->op == AsmOp::INC || setFlagsIt->op == AsmOp::DEC) {
                    if ((setFlagsIt->op == AsmOp::ADD || setFlagsIt->op == AsmOp::SUB) && !setFlagsIt->rm1.isMem && setFlagsIt->rm1.size == Size::BYTE) {
                        canOptimize1 = true;
                    }
                    break;
                }
            }
            if (canOptimize1) {
                // 如果是加，查找上一条是不是 neg
                bool isNegThenAdd = false;
                if (setFlagsIt->op == AsmOp::ADD && setFlagsIt + 1 != m_asms.rend() && (setFlagsIt + 1)->op == AsmOp::NEG && (setFlagsIt + 1)->rm1 == setFlagsIt->rm1) {
                    isNegThenAdd = true;
                }
                // 然后看有没有写入变量，要么没写入变量，要么只写入一个变量
                auto destIt = m_asms.rbegin() + 1;
                int32_t writeCnt = 0;
                for (auto it = m_asms.rbegin() + 1; it != setFlagsIt; ++it) {
                    if (it->op == AsmOp::MOV && it->rm1.isMem && it->rm1.size == Size::BYTE && it->rm1.reg == Reg::BX
                        && it->hasRm2 && it->rm2 == setFlagsIt->rm1) {
                        if (writeCnt == 0) {
                            destIt = it;
                            writeCnt = 1;
                        } else {
                            writeCnt = 2;
                            break;
                        }
                    }
                }

                if (writeCnt == 0 && (setFlagsIt->op == AsmOp::SUB || isNegThenAdd || setFlagsIt->hasImm)) {
                    // 转为 cmp 的条件：减法，如果是加法，要么前面 neg 了，要么第二操作数为立即数
                    bool isMemSrc = false;
                    auto srcIt = setFlagsIt + 1;
                    for (auto end = m_asms.rend(); srcIt != end; ++srcIt) {
                        // 若中途出现写入目标变量，终止回溯
                        // call getchar 指令终止回溯 ax
                        if (srcIt->hasRm1 && !srcIt->rm1.isMem && srcIt->rm1.reg == setFlagsIt->rm1.reg
                            || srcIt->op == AsmOp::CALL && srcIt->rm1.reg == Reg::DI && setFlagsIt->rm1.reg == Reg::AX) {
                            if (srcIt->op == AsmOp::MOVZX && srcIt->rm2.isMem && srcIt->rm2.reg == Reg::BX) {
                                isMemSrc = true;
                                // 往下查找，如果出现写入该目标变量，则不设置 isMemSrc
                                for (auto it = srcIt.base(), end1 = setFlagsIt.base() - 1; it != end1; ++it) {
                                    if (it->op == AsmOp::MOV && it->rm1 == srcIt->rm2) {
                                        isMemSrc = false;
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                    }
                    int32_t srcItOffset = srcIt.base() - 1 - m_asms.begin();
                    if (isMemSrc) {
                        setFlagsIt->rm1 = srcIt->rm2;
                    }
                    if (setFlagsIt->op == AsmOp::SUB) {
                        // 减法
                        setFlagsIt->op = AsmOp::CMP;
                    } else if (isNegThenAdd) {
                        // 加法但寄存器先前 neg
                        setFlagsIt->op = AsmOp::CMP;
                        m_asms.erase(setFlagsIt.base() - 2);
                    } else {
                        // 加法立即数
                        setFlagsIt->op = AsmOp::CMP;
                        setFlagsIt->imm = static_cast<int8_t>(-setFlagsIt->imm);
                    }
                    if (isMemSrc) {
                        m_asms.erase(m_asms.begin() + srcItOffset);
                    }
                } else if (writeCnt == 1) {
                    // 如果这条运算为加法，第二操作数为变量，最后写到同一个变量，根据是否 neg 可以优化成 内存 + 寄存器或内存 - 寄存器
                    if (setFlagsIt->op == AsmOp::ADD && setFlagsIt->hasRm2 && setFlagsIt->rm2 == destIt->rm1) {
                        if (isNegThenAdd) {
                            destIt->op = AsmOp::SUB;
                            m_asms.erase(setFlagsIt.base() - 2, setFlagsIt.base());
                        } else {
                            destIt->op = AsmOp::ADD;
                            m_asms.erase(setFlagsIt.base() - 1);
                        }
                    } else if (!setFlagsIt->hasRm2 || !setFlagsIt->rm2.isMem) {
                        // 否则为减法，需要第二个操作数为寄存器或立即数，需要查找这个寄存器是否直接来源于相同变量（movzx）
                        for (auto srcIt = setFlagsIt + 1, end = m_asms.rend(); srcIt != end; ++srcIt) {
                            // call getchar 指令终止回溯 ax
                            if (srcIt->hasRm1 && !srcIt->rm1.isMem && srcIt->rm1.reg == setFlagsIt->rm1.reg ||
                                srcIt->op == AsmOp::CALL && srcIt->rm1.reg == Reg::DI && setFlagsIt->rm1.reg == Reg::AX) {
                                if (srcIt->op == AsmOp::MOVZX && srcIt->rm2 == destIt->rm1) {
                                    // source known
                                    destIt->op = setFlagsIt->op;
                                    destIt->hasRm2 = setFlagsIt->hasRm2;
                                    destIt->rm2 = setFlagsIt->rm2;
                                    destIt->hasImm = setFlagsIt->hasImm;
                                    destIt->imm = setFlagsIt->imm;
                                    int32_t srcItOffset = srcIt.base() - 1 - m_asms.begin();
                                    m_asms.erase(setFlagsIt.base() - 1);
                                    m_asms.erase(m_asms.begin() + srcItOffset);
                                }
                                break;
                            }
                        }
                    }
                }
            }
            // lea cmp jcc -> cmp lea jcc 优化部分（打破依赖链）（废弃，若开启，重排列时无须前提指针移动，指针移动需用 lea 而非 add）
            /*
            if (m_asms.size() >= 3 && m_asms[m_asms.size() - 2].op == AsmOp::CMP && m_asms[m_asms.size() - 2].operand1->isMem && m_asms[m_asms.size() - 2].operand1->reg == Reg::BX
                && m_asms[m_asms.size() - 3].op == AsmOp::LEA && m_asms[m_asms.size() - 3].operand1->reg == Reg::BX && m_asms[m_asms.size() - 3].operand1->size == Size::QWORD) {
                std::swap(m_asms[m_asms.size() - 2], m_asms[m_asms.size() - 3]);
                // 现在 -3 是 cmp，-2 是 lea
                m_asms[m_asms.size() - 3].operand1->disp += m_asms[m_asms.size() - 2].operand2->disp;
            }*/
        }
    }

    // 经典的 ±1 替换为 inc/dec，寄存器清零 xor
    for (auto& asm1 : m_asms) {
        if ((asm1.op == AsmOp::ADD || asm1.op == AsmOp::SUB) && asm1.hasImm) {
            if (asm1.op == AsmOp::ADD && asm1.imm == 1 || asm1.op == AsmOp::SUB && asm1.imm == -1) {
                asm1.op = AsmOp::INC;
                asm1.hasImm = false;
                asm1.imm = 0;
            } else if (asm1.op == AsmOp::ADD && asm1.imm == -1 || asm1.op == AsmOp::SUB && asm1.imm == 1) {
                asm1.op = AsmOp::DEC;
                asm1.hasImm = false;
                asm1.imm = 0;
            }
        } else if (asm1.op == AsmOp::MOV && !asm1.rm1.isMem && asm1.hasImm && asm1.imm == 0) {
            asm1.op = AsmOp::XOR;
            asm1.rm1.size = Size::DWORD;
            asm1.hasRm2 = true;
            asm1.rm2 = asm1.rm1;
            asm1.hasImm = false;
            asm1.imm = 0;
        }
    }
}

int32_t X64BasicBlock::imulConstant(
    int32_t index,
    int32_t targetId,
    X64RMOperand::Reg sourceReg,
    X64RMOperand::Reg targetReg,
    int32_t constant,
    int32_t oldZfTemp,
    bool* eStates
) {
    int32_t zfTemp1 = oldZfTemp;
    bool isSpecial = false;
    bool isCondJmp = isNextToCondJmp(index, targetId);

    // 公共开头
    switch (std::abs(constant)) {
        case 4: case 8: case 16: case 32: case 64: case 128:
            // 纯移位
            if (sourceReg != targetReg) {
                bool useByte = !eStates[static_cast<int32_t>(sourceReg)];
                m_asms.emplace_back(
                    useByte ? AsmOp::MOVZX : AsmOp::MOV,
                    X64RMOperand{false, Size::DWORD, targetReg},
                    X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, sourceReg}
                );
                eStates[static_cast<int32_t>(targetReg)] = true;
            }
            break;
        default:
            // 带 lea 或纯 imul
            if (!eStates[static_cast<int32_t>(sourceReg)]) {
                m_asms.emplace_back(
                    AsmOp::MOVZX,
                    X64RMOperand{false, Size::DWORD, sourceReg},
                    X64RMOperand{false, Size::BYTE, sourceReg}
                );
                eStates[static_cast<int32_t>(sourceReg)] = true;
            }
    }

    // 生成代码
    switch (std::abs(constant)) {
        case 3:
            // t * 3 = t + 2 * t
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 2, sourceReg}
            );
            isSpecial = true;
            break;
        case 4: {
            // t * 4 = t << 2
            bool useByte = isCondJmp || !eStates[static_cast<int32_t>(targetReg)];
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, targetReg},
                2
            );
            eStates[static_cast<int32_t>(targetReg)] = !useByte;
            zfTemp1 = useByte ? targetId : -1;
            isSpecial = true;
            break;
        }
        case 5:
            // t * 5 = t + 4 * t
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 4, sourceReg}
            );
            isSpecial = true;
            break;
        case 6:
            // t * 6 = (t + 2 * t) + (t + 2 * t)
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 2, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::ADD,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg}
            );
            isSpecial = true;
            break;
        case 8: {
            // t * 8 = t << 3
            bool useByte = isCondJmp || !eStates[static_cast<int32_t>(targetReg)];
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, targetReg},
                3
            );
            eStates[static_cast<int32_t>(targetReg)] = !useByte;
            zfTemp1 = useByte ? targetId : -1;
            isSpecial = true;
            break;
        }
        case 9:
            // t * 9 = t + 8 * t
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 8, sourceReg}
            );
            isSpecial = true;
            break;
        case 10:
            // t * 10 = (t + 4 * t) + (t + 4 * t)
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 4, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::ADD,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg}
            );
            isSpecial = true;
            break;
        case 12:
            // t * 12 = (t + 2 * t) << 2
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 2, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                2
            );
            isSpecial = true;
            break;
        case 15:
            // t * 15 = (t + 2 * t) + 4 * (t + 2 * t)
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 2, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, targetReg, 0, 4, targetReg}
            );
            isSpecial = true;
            break;
        case 16: {
            // t * 16 = t << 4
            bool useByte = isCondJmp || !eStates[static_cast<int32_t>(targetReg)];
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, targetReg},
                4
            );
            eStates[static_cast<int32_t>(targetReg)] = !useByte;
            zfTemp1 = useByte ? targetId : -1;
            isSpecial = true;
            break;
        }
        case 18:
            // t * 18 = (t + 8 * t) + (t + 8 * t)
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 8, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::ADD,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg}
            );
            isSpecial = true;
            break;
        case 20:
            // t * 20 = (t + 4 * t) << 2
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 4, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                2
            );
            isSpecial = true;
            break;
        case 24:
            // t * 24 = (t + 2 * t) << 3
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 2, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                3
            );
            isSpecial = true;
            break;
        case 25:
            // t * 25 = (t + 4 * t) + 4 * (t + 4 * t)
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 4, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, targetReg, 0, 4, targetReg}
            );
            isSpecial = true;
            break;
        case 27:
            // t * 27 = (t + 2 * t) + 8 * (t + 2 * t)
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 2, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, targetReg, 0, 8, targetReg}
            );
            isSpecial = true;
            break;
        case 32: {
            // t * 32 = t << 5
            bool useByte = isCondJmp || !eStates[static_cast<int32_t>(targetReg)];
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, targetReg},
                5
            );
            eStates[static_cast<int32_t>(targetReg)] = !useByte;
            zfTemp1 = useByte ? targetId : -1;
            isSpecial = true;
            break;
        }
        case 36:
            // t * 36 = (t + 8 * t) << 2
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 8, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                2
            );
            isSpecial = true;
            break;
        case 40:
            // t * 40 = (t + 4 * t) << 3
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 4, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                3
            );
            isSpecial = true;
            break;
        case 45:
            // t * 45 = (t + 4 * t) + 8 * (t + 4 * t)
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 4, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, targetReg, 0, 8, targetReg}
            );
            isSpecial = true;
            break;
        case 48:
            // t * 48 = (t + 2 * t) << 4
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 2, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                4
            );
            isSpecial = true;
            break;
        case 64: {
            bool useByte = isCondJmp || !eStates[static_cast<int32_t>(targetReg)];
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, targetReg},
                6
            );
            eStates[static_cast<int32_t>(targetReg)] = !useByte;
            zfTemp1 = useByte ? targetId : -1;
            isSpecial = true;
            break;
        }
        case 72:
            // t * 72 = (t + 8 * t) << 3
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 8, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                3
            );
            isSpecial = true;
            break;
        case 80:
            // t * 80 = (t + 4 * t) << 4
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 4, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                4
            );
            isSpecial = true;
            break;
        case 81:
            // t * 81 = (t + 8 * t) + 8 * (t + 8 * t)
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 8, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, targetReg, 0, 8, targetReg}
            );
            isSpecial = true;
            break;
        case 96:
            // t * 96 = (t + 2 * t) << 5
            m_asms.emplace_back(
                AsmOp::LEA,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{true, Size::DWORD, sourceReg, 0, 2, sourceReg}
            );
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg},
                5
            );
            isSpecial = true;
            break;
        case 128: {
            bool useByte = isCondJmp || !eStates[static_cast<int32_t>(targetReg)];
            m_asms.emplace_back(
                AsmOp::SHL,
                X64RMOperand{false, useByte ? Size::BYTE : Size::DWORD, targetReg},
                7
            );
            eStates[static_cast<int32_t>(targetReg)] = !useByte;
            zfTemp1 = useByte ? targetId : -1;
            isSpecial = true;
            break;
        }
        default:
            m_asms.emplace_back(
                AsmOp::IMUL,
                X64RMOperand{false, Size::DWORD, targetReg},
                X64RMOperand{false, Size::DWORD, sourceReg},
                constant
            );
            eStates[static_cast<int32_t>(targetReg)] = true;
            zfTemp1 = -1;
    }

    // 公共收尾
    switch (std::abs(constant)) {
        case 3: case 5: case 9: case 15: case 25: case 27: case 45: case 81:
            // 纯 lea
            eStates[static_cast<int32_t>(targetReg)] = true;
            // 如果 target 被用于条件跳转，在纯 lea 中必须冲刷掉 zf 标记，防止后面漏生成 test 指令
            if (isCondJmp) {
                zfTemp1 = -1;
            }
            break;
        case 6: case 10: case 12: case 18: case 20: case 24: case 36: case 40: case 48: case 72: case 80: case 96:
            // lea 加加法/移位
            eStates[static_cast<int32_t>(targetReg)] = !isCondJmp;
            zfTemp1 = isCondJmp ? targetId : -1;
            break;
        default: // 纯移位/乘法不在这里收尾
    }

    if (isSpecial && constant < 0 && constant != -128) {
        m_asms.emplace_back(
            AsmOp::NEG,
            X64RMOperand{false, isCondJmp ? Size::BYTE : Size::DWORD, targetReg}
        );
        eStates[static_cast<int32_t>(targetReg)] = !isCondJmp;
        zfTemp1 = isCondJmp ? targetId : -1;
    }
    return zfTemp1;
}

bool X64BasicBlock::isNextToCondJmp(int32_t index, int32_t target) const {
    for (int32_t i = index + 1, size = m_irs.size(); i < size; i++) {
        auto& code = m_irs[i];
        if (code.type == OpType::JZ || code.type == OpType::JNZ) {
            if (code.arg1.value == target) {
                return true;
            }
            return false;
        }
        if (code.type != OpType::ASSIGN) {
            return false;
        }
        // if (code.type != OpType::ASSIGN && code.type != OpType::PTR_ADD) {
            // return false;
        // }
    }
    return false;
}

X64RMOperand::Reg X64BasicBlock::ensureOldTempInReg(
    int32_t index,
    int32_t target,
    std::unordered_map<int32_t, TempAsmInfo>& tempInfos,
    int32_t* regUse,
    bool* eStates,
    std::vector<int32_t>& spills
) {
    if (tempInfos[target].isSpill) {
        auto reg = findReg(index, target, tempInfos, regUse, spills);
        spills[tempInfos[target].currentRegOrMem] = -1;
        m_asms.emplace_back(
            AsmOp::MOVZX,
            X64RMOperand(false, Size::DWORD, reg),
            X64RMOperand(true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + tempInfos[target].currentRegOrMem)
        );
        tempInfos[target].isSpill = false;
        tempInfos[target].currentRegOrMem = static_cast<int32_t>(reg);
        regUse[static_cast<int32_t>(reg)] = target;
        eStates[static_cast<int32_t>(reg)] = true;
    }
    return static_cast<Reg>(tempInfos[target].currentRegOrMem);
}

X64RMOperand::Reg X64BasicBlock::findAnotherRegForMul(
    int32_t index,
    std::optional<X64RMOperand::Reg> exclude,
    std::unordered_map<int32_t, TempAsmInfo>& tempInfos,
    int32_t* regUse,
    std::vector<int32_t>& spills
) {
    std::vector regs{Reg::AX, Reg::CX, Reg::DX, Reg::R8, Reg::R9, Reg::R10, Reg::R11, Reg::BP, Reg::R12, Reg::R13, Reg::R14, Reg::R15};
    if (m_canUseGetcharReg) {
        regs.push_back(Reg::DI);
    }
    if (exclude.has_value()) {
        std::erase(regs, exclude.value());
    }
    return findRegInCandidates(index, tempInfos, regUse, spills, regs);
}

X64RMOperand::Reg X64BasicBlock::findReg(
    int32_t index,
    int32_t target,
    std::unordered_map<int32_t, TempAsmInfo>& tempInfos,
    int32_t* regUse,
    std::vector<int32_t>& spills
) {
    if (tempInfos[target].isCrossFuncs) {
        // 跨函数的变量只能放在非易失寄存器，无论是否要被 putchar
        // 可以先判断这个基本块有没有用到 putchar getchar，如果没有则可以用，离开基本块时把函数指针拉回来即可
        std::vector regs{Reg::BP, Reg::R12, Reg::R13, Reg::R14, Reg::R15};
        if (m_canUseGetcharReg) {
            regs.push_back(Reg::DI);
        }
        return findRegInCandidates(index, tempInfos, regUse, spills, regs);
    }
    if (tempInfos[target].isUsedByPutchar) {
        // 如果被 putchar 使用，却不跨 putchar 和 getchar，直接挪到 cx 好了
        return findRegInCandidates(index, tempInfos, regUse, spills, std::array{Reg::CX});
    }
    // 如果活跃区间与 putchar 临时变量相交，则不用 cx，否则可以用 cx
    bool canUseCX = true;
    for (auto& [id, info] : tempInfos) {
        if (id == target) {
            continue;
        }
        if (info.isUsedByPutchar && tempInfos[target].liveBegin <= info.liveEnd && tempInfos[target].liveEnd >= info.liveBegin) {
            canUseCX = false;
            break;
        }
    }
    std::vector regs{Reg::AX, Reg::DX, Reg::R8, Reg::R9, Reg::R10, Reg::R11, Reg::BP, Reg::R12, Reg::R13, Reg::R14, Reg::R15};
    if (canUseCX) {
        regs.insert(regs.begin() + 1, Reg::CX);
    }
    if (m_canUseGetcharReg) {
        regs.push_back(Reg::DI);
    }
    return findRegInCandidates(index, tempInfos, regUse, spills, regs);
}

X64RMOperand::Reg X64BasicBlock::findRegInCandidates(
    int32_t index,
    std::unordered_map<int32_t, TempAsmInfo>& tempInfos,
    int32_t* regUse, std::vector<int32_t>& spills,
    std::span<const X64RMOperand::Reg> regCandidates
) {
    for (auto reg : regCandidates) {
        // 空的寄存器直接分配
        if (regUse[static_cast<int32_t>(reg)] < 0) {
            return reg;
        }
        // 已经不活跃的寄存器直接分配
        if (auto& info = tempInfos[regUse[static_cast<int32_t>(reg)]]; index < info.liveBegin || index > info.liveEnd) {
            return reg;
        }
    }
    // 选一个最晚被用到的受害者移到栈上
    int32_t lastUse = -1;
    auto selectedReg = Reg::AX;
    for (auto reg : regCandidates) {
        auto& info = tempInfos[regUse[static_cast<int32_t>(reg)]];
        if (auto it = info.usePos.lower_bound(index); *it > lastUse) {
            lastUse = *it;
            selectedReg = reg;
        }
    }
    // 栈上找一个空位
    int32_t stackOffset = -1;
    for (int32_t i = 0, size = spills.size(); i < size; ++i) {
        if (spills[i] < 0) {
            stackOffset = i;
            break;
        }
        if (auto& info = tempInfos[spills[i]]; index < info.liveBegin || index > info.liveEnd) {
            stackOffset = i;
            break;
        }
    }
    if (stackOffset < 0) {
        spills.push_back(-1);
        stackOffset = spills.size() - 1;
    }

    // 发出 mov
    m_asms.emplace_back(
        AsmOp::MOV,
        X64RMOperand{true, Size::BYTE, Reg::SP, WindowsPEPlusGenerator::STACK_SPILL_OFFSET + stackOffset},
        X64RMOperand{false, Size::BYTE, selectedReg}
    );
    tempInfos[regUse[static_cast<int32_t>(selectedReg)]].isSpill = true;
    tempInfos[regUse[static_cast<int32_t>(selectedReg)]].currentRegOrMem = stackOffset;
    spills[stackOffset] = regUse[static_cast<int32_t>(selectedReg)];
    regUse[static_cast<int32_t>(selectedReg)] = -1;

    return selectedReg;
}

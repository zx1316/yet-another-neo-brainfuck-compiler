#include "x64_encoding_ctx.h"
#include <cassert>

using AsmOp = X64Asm::Operator;
using Size = X64RMOperand::Size;
using Reg = X64RMOperand::Reg;

void X64EncodingCtx::emit(const X64Asm& code, bool forceNearJmp) {
    emitOperandSizeOverride(code);
    emitRex(code);
    // todo 稍后插入特殊 opcode 路径
    emitOpcode(code, forceNearJmp);
    emitAddressing(code);

    emitImm(code, forceNearJmp);
}

void X64EncodingCtx::dryEmit(const X64Asm &code, bool forceNearJmp) {
    if (code.hasRm1 && code.rm1.size == Size::WORD) {
        m_dryLen++;  // 66h prefix
    }
    if (isNeedRex(code)) {
        m_dryLen++;
    }
    // todo 稍后插入特殊 opcode 路径
    m_dryLen += calcOpcodeLen(code, forceNearJmp);
    m_dryLen += calcAddressingLen(code);

    m_dryLen += calcImmLen(code, forceNearJmp);
}

void X64EncodingCtx::dryEmitNop(int32_t len) {
    assert(len >= 0 && len <= 15);
    m_dryLen += len;
}

void X64EncodingCtx::emitNop(int32_t len) {
    assert(len >= 0 && len <= 15);
    m_bytes.append_range(NOPS[len]);
}

void X64EncodingCtx::emitOperandSizeOverride(const X64Asm& code) {
    // 子集中只有 mov word ptr, imm16 和 mov word ptr, reg16 需要发出 0x66
    if (code.hasRm1 && code.rm1.size == Size::WORD) {
        m_bytes.push_back(0x66);
    }
}

// 约定：所有 (r, r) 和 (m, r) 型的都使用 (r/m, r) 的 opcode，(r, m) 型的才使用 (r, r/m) 的 opcode
// 这决定了 r 走的是 modr/m 的 base 还是 reg
// movzx/imul 为特例，因为只有 (r, r/m) 这一种形式
void X64EncodingCtx::emitRex(const X64Asm& code) {
    if (!code.hasRm1) {
        return;
    }
    auto& operand11 = code.rm1;
    uint8_t rex = 0x40;
    if (code.hasRm2) {
        auto& operand22 = code.rm2;
        if (operand22.isMem || code.op == AsmOp::MOVZX || code.op == AsmOp::IMUL) {
            // r, m 型和 movzx/imul 特判
            if (static_cast<int>(operand22.reg) >= 8 && operand22.reg != Reg::IP) {
                rex |= 0x1; // B
            }
            if (operand22.isMem && operand22.scale != 0 && static_cast<int>(operand22.reg2) >= 8) {
                rex |= 0x2; // X
            }
            if (static_cast<int>(operand11.reg) >= 8) {
                rex |= 0x4; // R
            }
        } else {
            // m, r 型和 r, r 型
            if (static_cast<int>(operand11.reg) >= 8 && operand11.reg != Reg::IP) {
                rex |= 0x1; // B
            }
            if (operand11.isMem && operand11.scale != 0 && static_cast<int>(operand11.reg2) >= 8) {
                rex |= 0x2; // X
            }
            if (static_cast<int>(operand22.reg) >= 8) {
                rex |= 0x4; // R
            }
        }
    } else {
        // 没有第二个寄存器/内存操作数
        if (operand11.isMem) {
            // 第一个操作数是内存
            if (static_cast<int>(operand11.reg) >= 8 && operand11.reg != Reg::IP) {
                rex |= 0x1; // B
            }
            if (operand11.scale != 0 && static_cast<int>(operand11.reg2) >= 8) {
                rex |= 0x2; // X
            }
        } else if (static_cast<int>(operand11.reg) >= 8) {
            // 通常来说，如果指令没有第二个寄存器/内存操作数，他们 ModR/M 的 r 字段是操作码的一部分（r 不编码寄存器）
            // 例外是 mov r, imm，opcode 是 B0+r 或 B8+r，没有 ModR/M，所以应当发出 R 而不是 B
            // 但对于 mov r64, imm，必须 64 位表示的 imm 才用 B8+r（R），否则应该用 C7（B）
            if (code.op == AsmOp::MOV && !(operand11.size == Size::QWORD && code.imm >= INT32_MIN && code.imm <= INT32_MAX)) {
                rex |= 0x4; // R
            } else {
                rex |= 0x1; // B
            }
        }
    }
    // call jmp jcc 默认 64 位，除了这几个，64 位操作数都要有 W
    if (operand11.size == Size::QWORD && code.op != AsmOp::CALL && code.op != AsmOp::JMP && code.op != AsmOp::JNZ && code.op != AsmOp::JZ) {
        rex |= 0x8; // W
    }
    // 还有高手！如果用到了 spl bpl sil dil，还得发 0x40 rex 前缀
    if (rex != 0x40
        || !operand11.isMem && operand11.size == Size::BYTE && static_cast<int>(operand11.reg) >= 4 && static_cast<int>(operand11.reg) <= 7
        || code.hasRm2 && !code.rm2.isMem && code.rm2.size == Size::BYTE && static_cast<int>(code.rm2.reg) >= 4 && static_cast<int>(code.rm2.reg) <= 7) {
        m_bytes.push_back(rex);
    }
}

bool X64EncodingCtx::isNeedRex(const X64Asm &code) {
    if (!code.hasRm1) {
        return false;
    }
    auto& operand11 = code.rm1;
    if (code.hasRm2) {
        auto& operand22 = code.rm2;
        if (operand22.isMem || code.op == AsmOp::MOVZX || code.op == AsmOp::IMUL) {
            // r, m 型和 movzx/imul 特判
            if (static_cast<int>(operand22.reg) >= 8 && operand22.reg != Reg::IP
                || operand22.isMem && operand22.scale != 0 && static_cast<int>(operand22.reg2) >= 8
                || static_cast<int>(operand11.reg) >= 8) {
                return true;
            }
        } else {
            // m, r 型和 r, r 型
            if (static_cast<int>(operand11.reg) >= 8 && operand11.reg != Reg::IP
                || operand11.isMem && operand11.scale != 0 && static_cast<int>(operand11.reg2) >= 8
                || static_cast<int>(operand22.reg) >= 8) {
                return true;
            }
        }
    } else {
        // 没有第二个寄存器/内存操作数
        if (operand11.isMem) {
            // 第一个操作数是内存
            if (static_cast<int>(operand11.reg) >= 8 && operand11.reg != Reg::IP
                || operand11.scale != 0 && static_cast<int>(operand11.reg2) >= 8) {
                return true;
            }
        } else if (static_cast<int>(operand11.reg) >= 8) {
            return true;
        }
    }
    // call jmp jcc 默认 64 位，除了这几个，64 位操作数都要有 W
    if (operand11.size == Size::QWORD && code.op != AsmOp::CALL && code.op != AsmOp::JMP && code.op != AsmOp::JNZ && code.op != AsmOp::JZ) {
        return true; // W
    }
    // 还有高手！如果用到了 spl bpl sil dil，还得发 0x40 rex 前缀
    return !operand11.isMem && operand11.size == Size::BYTE && static_cast<int>(operand11.reg) >= 4 && static_cast<int>(operand11.reg) <= 7
        || code.hasRm2 && !code.rm2.isMem && code.rm2.size == Size::BYTE && static_cast<int>(code.rm2.reg) >= 4 && static_cast<int>(code.rm2.reg) <= 7;
}

// 约定：所有 (r, r) 和 (m, r) 型的都使用 (r/m, r) 的 opcode，(r, m) 型的才使用 (r, r/m) 的 opcode
// 这决定了 r 走的是 modr/m 的 base 还是 reg
// movzx/imul 为特例，因为只有 (r, r/m) 这一种形式
void X64EncodingCtx::emitOpcode(const X64Asm& code, bool forceNearJmp) {
    switch (code.op) {
        case AsmOp::MOV: {
            // mov 有八种写法，你们知道吗
            auto& operand11 = code.rm1;
            if (code.hasRm2) {
                if (code.rm2.isMem) {
                    // mov r, m
                    m_bytes.push_back(operand11.size == Size::BYTE ? 0x8a : 0x8b);
                } else {
                    // mov r/m, r
                    m_bytes.push_back(operand11.size == Size::BYTE ? 0x88 : 0x89);
                }
            } else if (operand11.isMem || operand11.size == Size::QWORD && code.imm >= INT32_MIN && code.imm <= INT32_MAX) {
                // mov r/m, imm
                m_bytes.push_back(operand11.size == Size::BYTE ? 0xc6 : 0xc7);
            } else {
                // mov r, imm
                m_bytes.push_back((operand11.size == Size::BYTE ? 0xb0 : 0xb8) + (static_cast<int>(operand11.reg) & 7));
            }
            break;
        }
        case AsmOp::MOVZX:
            // movzx r32, r/m16 或 movzx r32, r/m8
            m_bytes.push_back(0x0f);
            m_bytes.push_back(code.rm2.size == Size::BYTE ? 0xb6 : 0xb7);
            break;
        case AsmOp::LEA:
            // 只有一种情况
            m_bytes.push_back(0x8d);
            break;
        case AsmOp::ADD:
        case AsmOp::SUB:
        case AsmOp::CMP: {
            // add sub cmp 的 7 种写法
            auto& operand11 = code.rm1;
            if (code.hasRm2) {
                if (code.rm2.isMem) {
                    // r, m
                    if (code.op == AsmOp::ADD) {
                        m_bytes.push_back(operand11.size == Size::BYTE ? 0x02 : 0x03);
                    } else if (code.op == AsmOp::SUB) {
                        m_bytes.push_back(operand11.size == Size::BYTE ? 0x2a : 0x2b);
                    } else {
                        m_bytes.push_back(operand11.size == Size::BYTE ? 0x3a : 0x3b);
                    }
                } else {
                    // r/m, r
                    if (code.op == AsmOp::ADD) {
                        m_bytes.push_back(operand11.size == Size::BYTE ? 0x00 : 0x01);
                    } else if (code.op == AsmOp::SUB) {
                        m_bytes.push_back(operand11.size == Size::BYTE ? 0x28 : 0x29);
                    } else {
                        m_bytes.push_back(operand11.size == Size::BYTE ? 0x38 : 0x39);
                    }
                }
            } else {
                // r/m, imm
                // 哈哈，这三个的 opcode 是一样的，怎么区分呢？下集揭晓
                if (operand11.size == Size::BYTE) {
                    m_bytes.push_back(0x80);
                } else if (code.imm > INT8_MAX || code.imm < INT8_MIN) {
                    m_bytes.push_back(0x81);
                } else {
                    m_bytes.push_back(0x83);
                }
            }
            break;
        }
        case AsmOp::SHL:
            if (code.imm == 1) {
                m_bytes.push_back(code.rm1.size == Size::BYTE ? 0xd0 : 0xd1);
            } else {
                m_bytes.push_back(code.rm1.size == Size::BYTE ? 0xc0 : 0xc1);
            }
            break;
        case AsmOp::NEG:
            m_bytes.push_back(code.rm1.size == Size::BYTE ? 0xf6 : 0xf7);
            break;
        case AsmOp::IMUL:
            if (code.hasImm) {
                if (code.imm > INT8_MAX || code.imm < INT8_MIN) {
                    m_bytes.push_back(0x69);
                } else {
                    m_bytes.push_back(0x6b);
                }
            } else {
                m_bytes.push_back(0x0f);
                m_bytes.push_back(0xaf);
            }
            break;
        case AsmOp::TEST:
            // 只有一种情况 test r/m8, r8
            m_bytes.push_back(0x84);
            break;
        case AsmOp::JMP:
            if (forceNearJmp || code.imm > INT8_MAX || code.imm < INT8_MIN) {
                m_bytes.push_back(0xe9);
            } else {
                m_bytes.push_back(0xeb);
            }
            break;
        case AsmOp::JZ:
            if (forceNearJmp || code.imm > INT8_MAX || code.imm < INT8_MIN) {
                m_bytes.push_back(0x0f);
                m_bytes.push_back(0x84);
            } else {
                m_bytes.push_back(0x74);
            }
            break;
        case AsmOp::JNZ:
            if (forceNearJmp || code.imm > INT8_MAX || code.imm < INT8_MIN) {
                m_bytes.push_back(0x0f);
                m_bytes.push_back(0x85);
            } else {
                m_bytes.push_back(0x75);
            }
            break;
        case AsmOp::CALL:
            if (code.hasRm1) {
                m_bytes.push_back(0xff); // call r/m
            } else {
                m_bytes.push_back(0xe8); // call rel
            }
            break;
        case AsmOp::XOR:
            // 只有一种情况 xor r/m, r
            m_bytes.push_back(0x31);
            break;
        case AsmOp::INC:
        case AsmOp::DEC:
            // 哈哈，这两个的 opcode 是一样的，怎么区分呢？下集揭晓
            m_bytes.push_back(code.rm1.size == Size::BYTE ? 0xfe : 0xff);
            break;
    }
}

int32_t X64EncodingCtx::calcOpcodeLen(const X64Asm &code, bool forceNearJmp) {
    switch (code.op) {
        case AsmOp::MOVZX:
            return 2;
        case AsmOp::IMUL:
            return code.hasImm ? 1 : 2;
        case AsmOp::JNZ:
        case AsmOp::JZ:
            return forceNearJmp || code.imm > INT8_MAX || code.imm < INT8_MIN ? 2 : 1;
        default:
            return 1;
    }
}

// 约定：所有 (r, r) 和 (m, r) 型的都使用 (r/m, r) 的 opcode，(r, m) 型的才使用 (r, r/m) 的 opcode
// movzx/imul 为特例，因为只有 (r, r/m) 这一种形式
void X64EncodingCtx::emitAddressing(const X64Asm& code) {
    // 条件跳转不需要 modr/m
    if (code.op == AsmOp::JMP || code.op == AsmOp::JZ || code.op == AsmOp::JNZ || !code.hasRm1) {
        return;
    }
    auto& operand11 = code.rm1;
    // mov r, imm 不需要 modr/m，但 mov r64, imm 当 imm 可以用 32 位表示时需要 modr/m
    if (code.op == AsmOp::MOV && !operand11.isMem && !code.hasRm2
        && !(operand11.size == Size::QWORD && code.imm >= INT32_MIN && code.imm <= INT32_MAX)) {
        return;
    }

    // modrm
    uint8_t modrm = 0;
    // r 字段
    // 揭晓答案：有一些特殊 opcode 需要借用 r 字段
    if (code.hasImm && code.op != AsmOp::IMUL) {
        // 除了 imul 外所有有立即数的
        if (code.op == AsmOp::MOV || code.op == AsmOp::ADD) {
            modrm |= 0 << 3;
        } else if (code.op == AsmOp::SUB) {
            modrm |= 5 << 3;
        } else if (code.op == AsmOp::CMP) {
            modrm |= 7 << 3;
        } else if (code.op == AsmOp::SHL) {
            modrm |= 4 << 3;
        }
    } else if (code.op == AsmOp::CALL) {
        modrm |= 2 << 3;
    } else if (code.op == AsmOp::DEC) {
        modrm |= 1 << 3;
    } else if (code.op == AsmOp::INC) {
        modrm |= 0 << 3;
    } else if (code.op == AsmOp::NEG) {
        modrm |= 3 << 3;
    } else {
        // 常规 r 字段，operand2 必存在
        auto& operand22 = code.rm2;
        if (operand22.isMem || code.op == AsmOp::MOVZX || code.op == AsmOp::IMUL) {
            // (r, m) 型和 movzx/imul 特判
            modrm |= (static_cast<int>(operand11.reg) & 7) << 3;
        } else {
            // (r/m, r) 型
            modrm |= (static_cast<int>(operand22.reg) & 7) << 3;
        }
    }

    // mod 和 r/m 字段
    const X64RMOperand* modrmOperand;
    if (code.hasRm2 && (code.rm2.isMem || code.op == AsmOp::MOVZX || code.op == AsmOp::IMUL)) {
        modrmOperand = &code.rm2;   // (r, m) 型和 movzx/imul (r, r/m) 特判
    } else {
        modrmOperand = &operand11;   // (r/m, r) 型和 (r)/(m) 型
    }

    // rip 相对寻址特判，跳过 sib 直接 disp 早返回
    if (modrmOperand->isMem && modrmOperand->reg == Reg::IP) {
        modrm |= 5;
        m_bytes.push_back(modrm);
        auto* disp = reinterpret_cast<const uint8_t*>(&modrmOperand->disp);
        m_bytes.push_back(disp[0]);
        m_bytes.push_back(disp[1]);
        m_bytes.push_back(disp[2]);
        m_bytes.push_back(disp[3]);
        return;
    }

    if (modrmOperand->isMem) {
        // mod
        if (modrmOperand->disp >= 128 || modrmOperand->disp <= -129) {
            modrm |= 2 << 6;
        } else if (modrmOperand->reg == Reg::BP || modrmOperand->reg == Reg::R13 || modrmOperand->disp != 0) {
            modrm |= 1 << 6;    // bp 和 r13 没有不带位移的 modr/m
        } else {
            modrm |= 0 << 6;
        }
        // r/m
        if (modrmOperand->reg == Reg::SP || modrmOperand->reg == Reg::R12 || modrmOperand->scale != 0) {
            modrm |= 4; // sib
        } else {
            modrm |= static_cast<int>(modrmOperand->reg) & 7;
        }
    } else {
        modrm |= 3 << 6;    // mod
        modrm |= static_cast<int>(modrmOperand->reg) & 7;   // r/m
    }
    m_bytes.push_back(modrm);

    // sib
    if (modrmOperand->isMem && (modrmOperand->reg == Reg::SP || modrmOperand->reg == Reg::R12 || modrmOperand->scale != 0)) {
        uint8_t sib = static_cast<int>(modrmOperand->reg) & 7;   // base
        if (modrmOperand->scale == 0) {
            sib |= 4 << 3;  // scale and index is none
        } else {
            // scale
            if (modrmOperand->scale == 1) {
                sib |= 0 << 6;
            } else if (modrmOperand->scale == 2) {
                sib |= 1 << 6;
            } else if (modrmOperand->scale == 4) {
                sib |= 2 << 6;
            } else {
                sib |= 3 << 6;
            }
            sib |= (static_cast<int>(modrmOperand->reg2) & 7) << 3;  // index
        }
        m_bytes.push_back(sib);
    }

    // disp
    if (modrmOperand->isMem) {
        if (modrmOperand->disp > INT8_MAX || modrmOperand->disp < INT8_MIN) {
            auto* disp = reinterpret_cast<const uint8_t*>(&modrmOperand->disp);
            m_bytes.push_back(disp[0]);
            m_bytes.push_back(disp[1]);
            m_bytes.push_back(disp[2]);
            m_bytes.push_back(disp[3]);
        } else if (modrmOperand->reg == Reg::BP || modrmOperand->reg == Reg::R13 || modrmOperand->disp != 0) {
            m_bytes.push_back(static_cast<uint8_t>(modrmOperand->disp));
        }
    }
}

int32_t X64EncodingCtx::calcAddressingLen(const X64Asm &code) {
    // 条件跳转不需要 modr/m
    if (code.op == AsmOp::JMP || code.op == AsmOp::JZ || code.op == AsmOp::JNZ || !code.hasRm1) {
        return 0;
    }
    auto& operand11 = code.rm1;
    // mov r, imm 不需要 modr/m，但 mov r64, imm 当 imm 可以用 32 位表示时需要 modr/m
    if (code.op == AsmOp::MOV && !operand11.isMem && !code.hasRm2
        && !(operand11.size == Size::QWORD && code.imm >= INT32_MIN && code.imm <= INT32_MAX)) {
        return 0;
    }

    // mod 和 r/m 字段
    const X64RMOperand* modrmOperand;
    if (code.hasRm2 && (code.rm2.isMem || code.op == AsmOp::MOVZX || code.op == AsmOp::IMUL)) {
        modrmOperand = &code.rm2;   // (r, m) 型和 movzx/imul (r, r/m) 特判
    } else {
        modrmOperand = &operand11;   // (r/m, r) 型和 (r)/(m) 型
    }

    // rip 相对寻址特判，跳过 sib 直接 disp 早返回
    if (modrmOperand->isMem && modrmOperand->reg == Reg::IP) {
        return 5;
    }

    int32_t len = 1;    // modr/m 至少 1
    // sib
    if (modrmOperand->isMem && (modrmOperand->reg == Reg::SP || modrmOperand->reg == Reg::R12 || modrmOperand->scale != 0)) {
        len++;
    }
    // disp
    if (modrmOperand->isMem) {
        if (modrmOperand->disp > INT8_MAX || modrmOperand->disp < INT8_MIN) {
            len += 4;
        } else if (modrmOperand->reg == Reg::BP || modrmOperand->reg == Reg::R13 || modrmOperand->disp != 0) {
            len++;
        }
    }
    return len;
}

void X64EncodingCtx::emitImm(const X64Asm& code, bool forceNearJmp) {
    // shl 1 不需要发出立即数
    if (code.hasImm && !(code.op == AsmOp::SHL && code.imm == 1)) {
        // 强制 near 跳转、mov 非 8 位必须用 32 位立即数
        if (code.imm > INT8_MAX || code.imm < INT8_MIN || code.op == AsmOp::CALL
            || forceNearJmp && (code.op == AsmOp::JNZ || code.op == AsmOp::JZ || code.op == AsmOp::JMP)
            || code.op == AsmOp::MOV && code.rm1.size != Size::BYTE) {
            auto* imm = reinterpret_cast<const uint8_t*>(&code.imm);
            // 16
            m_bytes.push_back(imm[0]);
            m_bytes.push_back(imm[1]);
            // 若没发出 66H 则至少 32 位 imm
            if (!(code.hasRm1 && code.rm1.size == Size::WORD)) {
                // 32
                m_bytes.push_back(imm[2]);
                m_bytes.push_back(imm[3]);
                // movabs 64 位 imm
                if (code.op == AsmOp::MOV && !code.rm1.isMem && !code.hasRm2
                    && code.rm1.size == Size::QWORD && (code.imm < INT32_MIN || code.imm > INT32_MAX)) {
                    // 64
                    m_bytes.push_back(imm[4]);
                    m_bytes.push_back(imm[5]);
                    m_bytes.push_back(imm[6]);
                    m_bytes.push_back(imm[7]);
                }
            }
        } else {
            m_bytes.push_back(static_cast<uint8_t>(code.imm));  // 8
        }
    }
}

int32_t X64EncodingCtx::calcImmLen(const X64Asm &code, bool forceNearJmp) {
    // shl 1 不需要发出立即数
    if (code.hasImm && !(code.op == AsmOp::SHL && code.imm == 1)) {
        // 强制 near 跳转、mov 非 8 位必须用 32 位立即数
        if (code.imm > INT8_MAX || code.imm < INT8_MIN || code.op == AsmOp::CALL
            || forceNearJmp && (code.op == AsmOp::JNZ || code.op == AsmOp::JZ || code.op == AsmOp::JMP)
            || code.op == AsmOp::MOV && code.rm1.size != Size::BYTE) {
            int32_t len = 2;
            // 若没发出 66H 则至少 32 位 imm
            if (!(code.hasRm1 && code.rm1.size == Size::WORD)) {
                len += 2;
                // movabs 64 位 imm
                if (code.op == AsmOp::MOV && !code.rm1.isMem && !code.hasRm2
                    && code.rm1.size == Size::QWORD && (code.imm < INT32_MIN || code.imm > INT32_MAX)) {
                    len += 4;
                }
            }
            return len;
        }
        return 1;
    }
    return 0;
}

bool X64EncodingCtx::emitAxSpecial(const X64Asm &code) {
    bool isSpecial = isAxSpecial(code);
    if (isSpecial) {
        // 特殊 opcode
        switch (code.op) {
            case AsmOp::ADD: m_bytes.push_back(code.rm1.size == Size::BYTE ? 0x04 : 0x05); break;
            case AsmOp::SUB: m_bytes.push_back(code.rm1.size == Size::BYTE ? 0x2c : 0x2d); break;
            default: m_bytes.push_back(code.rm1.size == Size::BYTE ? 0x3c : 0x3d); break;
        }
    }
    return isSpecial;
}

bool X64EncodingCtx::isAxSpecial(const X64Asm &code) {
    // (add|sub|cmp) ax, imm 有特殊 opcode，可能可以少一个字节，其实 test 也有，但是这里用不上
    if (code.op != AsmOp::ADD && code.op != AsmOp::SUB && code.op != AsmOp::CMP
        || code.rm1.isMem || code.rm1.reg != Reg::AX || !code.hasImm) {
        return false;
    }
    return code.rm1.size == Size::BYTE || code.imm < INT8_MIN || code.imm > INT8_MAX;
}

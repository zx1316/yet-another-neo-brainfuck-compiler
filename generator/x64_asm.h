#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_X64_ASM_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_X64_ASM_H

#include <array>
#include <cstdint>
#include <ostream>

struct X64RMOperand {
    enum class Reg : uint8_t {
        AX, CX, DX, BX, SP, BP, SI, DI, R8, R9, R10, R11, R12, R13, R14, R15, IP
    };

    enum class Size : uint8_t {
        BYTE, WORD, DWORD, QWORD
    };

    static constexpr auto TRANSLATE_BYTE = std::array{"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"};
    static constexpr auto TRANSLATE_WORD = std::array{"ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"};
    static constexpr auto TRANSLATE_DWORD = std::array{"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
    static constexpr auto TRANSLATE_QWORD = std::array{"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rip"};

    bool isMem = false;
    Size size = Size::BYTE;
    Reg reg = Reg::AX;
    int32_t disp = 0;
    uint8_t scale = 0;
    Reg reg2 = Reg::AX;

    friend std::ostream& operator<<(std::ostream& os, const X64RMOperand& obj) {
        if (obj.isMem) {
            switch (obj.size) {
                case Size::BYTE: os << "byte ptr "; break;
                case Size::WORD: os << "word ptr "; break;
                case Size::DWORD: os << "dword ptr "; break;
                case Size::QWORD: os << "qword ptr "; break;
            }
            if (obj.scale != 0) {
                if (obj.disp != 0) {
                    os << '[' << TRANSLATE_QWORD[static_cast<int32_t>(obj.reg)] << " + " << static_cast<int32_t>(obj.scale) << '*' << TRANSLATE_QWORD[static_cast<int32_t>(obj.reg2)] << " + " << obj.disp << ']';
                } else {
                    os << '[' << TRANSLATE_QWORD[static_cast<int32_t>(obj.reg)] << " + " << static_cast<int32_t>(obj.scale) << '*' << TRANSLATE_QWORD[static_cast<int32_t>(obj.reg2)] << ']';
                }
            } else if (obj.disp != 0) {
                os << '[' << TRANSLATE_QWORD[static_cast<int32_t>(obj.reg)] << " + " << obj.disp << ']';
            } else {
                os << '[' << TRANSLATE_QWORD[static_cast<int32_t>(obj.reg)] << ']';
            }
        } else {
            switch (obj.size) {
                case Size::BYTE: os << TRANSLATE_BYTE[static_cast<int32_t>(obj.reg)]; break;
                case Size::WORD: os << TRANSLATE_WORD[static_cast<int32_t>(obj.reg)]; break;
                case Size::DWORD: os << TRANSLATE_DWORD[static_cast<int32_t>(obj.reg)]; break;
                case Size::QWORD: os << TRANSLATE_QWORD[static_cast<int32_t>(obj.reg)]; break;
            }
        }
        return os;
    }

    friend bool operator==(const X64RMOperand& lhs, const X64RMOperand& rhs) {
        if (lhs.isMem != rhs.isMem) return false;
        if (!lhs.isMem) return lhs.size == rhs.size && lhs.reg == rhs.reg;
        if (lhs.scale != rhs.scale) return false;
        if (lhs.scale == 0) return lhs.size == rhs.size && lhs.reg == rhs.reg && lhs.disp == rhs.disp;
        return lhs.size == rhs.size && lhs.reg == rhs.reg && lhs.disp == rhs.disp && lhs.reg2 == rhs.reg2;
    }

    friend bool operator!=(const X64RMOperand& lhs, const X64RMOperand& rhs) { return !(lhs == rhs); }
};

struct X64Asm {
    enum class Operator : uint8_t {
        MOV, MOVZX, LEA, ADD, SUB, CMP, SHL, NEG, IMUL, TEST, JMP, JZ, JNZ, CALL, XOR, INC, DEC
    };

    static constexpr auto OPERATOR_TRANSLATE = std::array{
        "mov", "movzx", "lea", "add", "sub", "cmp", "shl", "neg", "imul", "test", "jmp", "jz", "jnz", "call", "xor", "inc", "dec"
    };

    Operator op;
    bool hasRm1;
    bool hasRm2;
    bool hasImm;
    X64RMOperand rm1;
    X64RMOperand rm2;
    int64_t imm;

    X64Asm(Operator op, const X64RMOperand& operand1) :
        op(op), hasRm1(true), hasRm2(false), hasImm(false), rm1(operand1), imm(0) {}

    X64Asm(Operator op, int64_t operand1) :
        op(op), hasRm1(false), hasRm2(false), hasImm(true), imm(operand1) {}

    X64Asm(Operator op, const X64RMOperand& operand1, const X64RMOperand& operand2) :
        op(op), hasRm1(true), hasRm2(true), hasImm(false), rm1(operand1), rm2(operand2), imm(0) {}

    X64Asm(Operator op, const X64RMOperand& operand1, int64_t operand2) :
        op(op), hasRm1(true), hasRm2(false), hasImm(true), rm1(operand1), imm(operand2) {}

    X64Asm(Operator op, const X64RMOperand& operand1, const X64RMOperand& operand2, int64_t operand3) :
        op(op), hasRm1(true), hasRm2(true), hasImm(true), rm1(operand1), rm2(operand2), imm(operand3) {}

    friend std::ostream& operator<<(std::ostream& os, const X64Asm& obj) {
        os << OPERATOR_TRANSLATE[static_cast<int>(obj.op)] << '\t';
        if (obj.hasRm1) {
            os << obj.rm1;
        }
        if (obj.hasRm2) {
            os << ", " << obj.rm2;
        }
        if (obj.hasImm) {
            if (obj.hasRm1) {
                os << ", " << obj.imm;
            } else {
                os << obj.imm;
            }
        }
        return os;
    }

    bool operator==(const X64Asm& other) const {
        if (op != other.op || hasRm1 != other.hasRm1 || hasRm2 != other.hasRm2 || hasImm != other.hasImm) {
            return false;
        }
        if (hasRm1 && rm1 != other.rm1) {
            return false;
        }
        if (hasRm2 && rm2 != other.rm2) {
            return false;
        }
        return !(hasImm && imm != other.imm);
    }

    bool operator!=(const X64Asm& other) const {
        return !operator==(other);
    }
};


#endif //YETANOTHERNEOBRAINFUCKCOMPILER_X64_ASM_H

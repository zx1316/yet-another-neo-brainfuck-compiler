#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_X64_ASM_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_X64_ASM_H

#include <cstdint>
#include <optional>
#include <ostream>

struct X64AsmOperand {
    enum class Reg : uint8_t {
        AX, CX, DX, BX, SP, BP, SI, DI, R8, R9, R10, R11, R12, R13, R14, R15
    };

    enum class Size : uint8_t {
        BYTE, WORD, DWORD, QWORD
    };

    static const char * const TRANSLATE_BYTE[];
    static const char * const TRANSLATE_WORD[];
    static const char * const TRANSLATE_DWORD[];
    static const char * const TRANSLATE_QWORD[];

    bool isMem = false;
    Size size = Size::BYTE;
    Reg reg = Reg::AX;
    int32_t disp = 0;
    uint8_t scale = 0;
    Reg reg2 = Reg::AX;

    friend std::ostream& operator<<(std::ostream& os, const X64AsmOperand& obj) {
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

    friend bool operator==(const X64AsmOperand& lhs, const X64AsmOperand& rhs) {
        if (lhs.isMem != rhs.isMem) return false;
        if (!lhs.isMem) return lhs.size == rhs.size && lhs.reg == rhs.reg;
        if (lhs.scale != rhs.scale) return false;
        if (lhs.scale == 0) return lhs.size == rhs.size && lhs.reg == rhs.reg && lhs.disp == rhs.disp;
        return lhs.size == rhs.size && lhs.reg == rhs.reg && lhs.disp == rhs.disp && lhs.reg2 == rhs.reg2;
    }

    friend bool operator!=(const X64AsmOperand& lhs, const X64AsmOperand& rhs) { return !(lhs == rhs); }
};

struct X64Asm {
    enum class Operator : uint8_t {
        MOV, MOVZX, LEA, ADD, SUB, CMP, SHL, NEG, IMUL, TEST, JMP, JZ, JNZ, CALL, XOR, INC, DEC
    };

    static const char * const OPERATOR_TRANSLATE[];

    Operator op = Operator::MOV;
    std::optional<X64AsmOperand> operand1;
    std::optional<X64AsmOperand> operand2;
    std::optional<int32_t> imm;

    friend std::ostream& operator<<(std::ostream& os, const X64Asm& obj) {
        os << OPERATOR_TRANSLATE[static_cast<int32_t>(obj.op)] << '\t';
        if (obj.operand1.has_value()) {
            os << obj.operand1.value();
        }
        if (obj.operand2.has_value()) {
            os << ", " << obj.operand2.value();
        }
        if (obj.imm.has_value()) {
            if (obj.operand1.has_value()) {
                os << ", " << obj.imm.value();
            } else {
                os << obj.imm.value();
            }
        }
        return os;
    }
};



#endif //YETANOTHERNEOBRAINFUCKCOMPILER_X64_ASM_H

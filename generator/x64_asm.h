#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_X64_ASM_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_X64_ASM_H

#include <cstdint>
#include <optional>
#include <ostream>

struct X64RMOperand {
    enum class Reg : uint8_t {
        AX, CX, DX, BX, SP, BP, SI, DI, R8, R9, R10, R11, R12, R13, R14, R15, IP
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

    static const char * const OPERATOR_TRANSLATE[];

    Operator op = Operator::MOV;
    std::optional<X64RMOperand> operand1;
    std::optional<X64RMOperand> operand2;
    std::optional<int64_t> imm;

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

struct X64Asm2 {
    enum class Operator : uint8_t {
        MOV, MOVZX, LEA, ADD, SUB, CMP, SHL, NEG, IMUL, TEST, JMP, JZ, JNZ, CALL, XOR, INC, DEC
    };

    enum OperandBit : uint8_t {
        RM1 = 0x1,
        RM2 = 0x2,
        IMM = 0x4
    };

    static const char * const OPERATOR_TRANSLATE[];

    X64Asm2(Operator op, const X64RMOperand& operand1) :
        m_comb(RM1), m_op(op), m_rm1(operand1), m_imm(0) {}
    X64Asm2(Operator op, int64_t operand1) :
        m_comb(IMM), m_op(op), m_imm(operand1) {}
    X64Asm2(Operator op, const X64RMOperand& operand1, const X64RMOperand& operand2) :
        m_comb(RM1 | RM2), m_op(op), m_rm1(operand1), m_rm2(operand2), m_imm(0) {}
    X64Asm2(Operator op, const X64RMOperand& operand1, int64_t operand2) :
        m_comb(RM1 | IMM), m_op(op), m_rm1(operand1), m_imm(operand2) {}
    X64Asm2(Operator op, const X64RMOperand& operand1, const X64RMOperand& operand2, int64_t operand3) :
        m_comb(RM1 | RM2 | IMM), m_op(op), m_rm1(operand1), m_rm2(operand2), m_imm(operand3) {}

    [[nodiscard]] uint8_t comb() const { return m_comb; }
    [[nodiscard]] Operator op() const { return m_op; }
    [[nodiscard]] const X64RMOperand& rm1() const { return m_rm1; }
    [[nodiscard]] const X64RMOperand& rm2() const { return m_rm2; }
    [[nodiscard]] int64_t imm() const { return m_imm; }

    void setOp(Operator op) { m_op = op; }
    void setImm(int64_t imm) { m_imm = imm; }

    friend std::ostream& operator<<(std::ostream& os, const X64Asm2& obj) {
        os << OPERATOR_TRANSLATE[static_cast<int>(obj.m_op)] << '\t';
        if (obj.comb() & RM1) {
            os << obj.m_rm1;
        }
        if (obj.comb() & RM2) {
            os << ", " << obj.m_rm2;
        }
        if (obj.comb() & IMM) {
            if (obj.comb() & RM1) {
                os << ", " << obj.m_imm;
            } else {
                os << obj.m_imm;
            }
        }
        return os;
    }

    bool operator==(const X64Asm2& other) const {
        if (m_comb != other.m_comb || m_op != other.m_op) {
            return false;
        }
        if (m_comb & RM1 && m_rm1 != other.m_rm1) {
            return false;
        }
        if (m_comb & RM2 && m_rm2 != other.m_rm2) {
            return false;
        }
        return !(m_comb & IMM && m_imm != other.m_imm);
    }

    bool operator!=(const X64Asm2& other) const {
        return !operator==(other);
    }

private:
    int8_t m_comb;
    Operator m_op;
    X64RMOperand m_rm1;
    X64RMOperand m_rm2;
    int64_t m_imm;
};


#endif //YETANOTHERNEOBRAINFUCKCOMPILER_X64_ASM_H

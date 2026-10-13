#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_X64_ENCODING_CTX_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_X64_ENCODING_CTX_H

#include "x64_asm.h"
#include <cstdint>
#include <vector>
#include <array>

// 可以处理这些汇编：
// 几乎所有情况的 add, sub, cmp, lea, inc, dec, neg, movzx, nop
// mov，除了 `mov rax, [绝对地址]`
// shl r/m, imm
// imul r, r/m, (imm)
// xor r/m16/32/64, r16/32/64
// test r/m8, r8
// jmp rel, jz rel, jnz rel
// call r/m, call rel
// 其他的汇编指令行为未定义。
// 假设宿主机是小端。
class X64EncodingCtx {
public:
    [[nodiscard]] int32_t size() const { return m_bytes.empty() ? m_dryLen : m_bytes.size(); }
    [[nodiscard]] const uint8_t* data() const { return m_bytes.data(); }
    void clear() { m_bytes.clear(); m_dryLen = 0; }
    void dryEmit(const X64Asm& code, bool forceNearJmp);
    void emit(const X64Asm& code, bool forceNearJmp);
    void dryEmitNop(int32_t len);
    void emitNop(int32_t len);

private:
    void emitOperandSizeOverride(const X64Asm& code);
    void emitRex(const X64Asm& code);
    void emitOpcode(const X64Asm& code, bool forceNearJmp);
    void emitAddressing(const X64Asm& code);
    void emitImm(const X64Asm& code, bool forceNearJmp);
    bool emitAxSpecial(const X64Asm& code);

    static bool isNeedRex(const X64Asm& code);
    static int32_t calcOpcodeLen(const X64Asm& code, bool forceNearJmp);
    static int32_t calcAddressingLen(const X64Asm& code);
    static int32_t calcImmLen(const X64Asm& code, bool forceNearJmp);
    static bool isAxSpecial(const X64Asm& code);

    std::vector<uint8_t> m_bytes;
    int32_t m_dryLen = 0;

    static constexpr auto NOPS = std::array{
        "",
        "\x90",
        "\x66\x90",
        "\x0f\x1f\x00",
        "\x0f\x1f\x40\x00",
        "\x0f\x1f\x44\x00\x00",
        "\x66\x0f\x1f\x44\x00\x00",
        "\x0f\x1f\x80\x00\x00\x00\x00",
        "\x0f\x1f\x84\x00\x00\x00\x00\x00",
        "\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
        "\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
        "\x66\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
        "\x66\x66\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
        "\x66\x66\x66\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
        "\x66\x66\x66\x66\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
        "\x66\x66\x66\x66\x66\x66\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
    };
};



#endif //YETANOTHERNEOBRAINFUCKCOMPILER_X64_ENCODING_CTX_H

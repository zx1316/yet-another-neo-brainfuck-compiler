#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_X64_ENCODING_CTX_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_X64_ENCODING_CTX_H

#include "x64_asm.h"

#include <cstdint>
#include <vector>

// 做出以下假设：
// 寄存器-寄存器、寄存器-立即数的 mov 可能是 8/32/64 位，寄存器-内存、内存-寄存器的 mov 可能是 8/32/64 位，内存-立即数的 mov 可能是 8/16/32 位
// 寄存器-寄存器、寄存器-立即数（8 位）的 add/sub/cmp 只可能是 8/32 位，寄存器-内存、内存-寄存器的 add/sub/cmp 只可能是 8 位，内存-立即数的 add/sub/cmp 只可能是 8 位
// imul 要么是双 32 位寄存器，要么是双 32 位寄存器 + 8 位立即数
// movzx 只可能从 8 位寄存器/内存 mov 到 32 位寄存器
// lea 的目标寄存器是 32/64 位
// shl 第一个操作数可能是 8/32 位寄存器或 8 位内存，第二个操作数为 8 位立即数
// neg/inc/dec 的操作数可能是 8/32 位寄存器或 8 位内存
// xor 的两个操作数一定是 32 位寄存器
// test 的两个操作数一定是 8 位寄存器
// call 的操作数一定是 64 位寄存器
// jmp/jz/jnz 先按照大跳来计算长度
class X64EncodingCtx {
public:
    [[nodiscard]] int32_t size() const { return m_bytes.size(); }
    [[nodiscard]] const uint8_t* data() const { return m_bytes.data(); }
    void clear() { m_bytes.clear(); }
    void emit(const X64Asm& code, bool forceNearJmp);

private:
    void emitOperandSizeOverride(const X64Asm& code);
    void emitRex(const X64Asm& code);
    void emitOpcode(const X64Asm& code, bool forceNearJmp);
    void emitAddressing(const X64Asm& code);
    void emitImm(const X64Asm& code, bool forceNearJmp);

    std::vector<uint8_t> m_bytes;
    bool m_isOperandSizeOverride = false;
};



#endif //YETANOTHERNEOBRAINFUCKCOMPILER_X64_ENCODING_CTX_H

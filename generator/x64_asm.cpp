#include "x64_asm.h"

const char * const X64RMOperand::TRANSLATE_BYTE[] = {"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"};
const char * const X64RMOperand::TRANSLATE_WORD[] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"};
const char * const X64RMOperand::TRANSLATE_DWORD[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
const char * const X64RMOperand::TRANSLATE_QWORD[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rip"};

const char * const X64Asm::OPERATOR_TRANSLATE[] = {"mov", "movzx", "lea", "add", "sub", "cmp", "shl", "neg", "imul", "test", "jmp", "jz", "jnz", "call", "xor", "inc", "dec"};
const char * const X64Asm2::OPERATOR_TRANSLATE[] = {"mov", "movzx", "lea", "add", "sub", "cmp", "shl", "neg", "imul", "test", "jmp", "jz", "jnz", "call", "xor", "inc", "dec"};

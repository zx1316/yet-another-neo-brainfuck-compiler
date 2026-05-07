#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_WINDOWS_PE_PLUS_GENERATOR_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_WINDOWS_PE_PLUS_GENERATOR_H

#include "x64_basic_block.h"
#include "../optimizer/mid_basic_block.h"


class WindowsPEPlusGenerator {
public:
    // 注意：该构造函数会移动 MidBasicBlock 中的中间代码数组
    WindowsPEPlusGenerator(const std::vector<std::unique_ptr<MidBasicBlock>>& basicBlocks, int32_t tapeSize, int32_t initOffset);

    [[nodiscard]] const std::vector<std::unique_ptr<X64BasicBlock>>& getBasicBlocks() const { return m_basicBlocks; }
    [[nodiscard]] int32_t countASMs() const;

    // 一键生成 PE+ 文件
    void generate(int32_t optimizeLevel, bool isModern, bool printInfo, std::ostream& os);

    // 重排列中间代码用于最终生成，仅能在优化开启时调用，否则出错
    void rearrangeIRs(bool printInfo) const;

    // IR -> ASM
    void generateASM(int32_t optimizeLevel, bool printInfo) const;

    // 放置基本块，先决条件：generateASM
    void placeBasicBlocks();

    // 设置基本块的相对跳转值，先决条件：placeBasicBlocks
    void setBlocksRelJmp() const;

    // 写入 PE+ 文件，先决条件：placeBasicBlocks
    void writeTo(std::ostream& os, bool isModern) const;

    // 打印中间代码与 ASM
    void printIRAndASM() const;

    static constexpr int32_t STACK_SPILL_OFFSET = 40;

private:
    void legacyWriteTo(std::ostream& os) const;
    void modernWriteTo(std::ostream& os) const;

    // 以下偏移量对应的数如未说明均为 dword
    static std::array<unsigned char, 990> s_legacyPEPlusRuntimeData;
    static constexpr int32_t LEGACY_SIZE_OF_CODE_OFFSET = 0xc4; // 0xc4，实际代码长度向上取整到 512
    static constexpr int32_t LEGACY_SIZE_OF_IMAGE_OFFSET = 0xf8; // 0xf8，(4096 + sizeOfCode) 向上取整到 4096
    static constexpr int32_t LEGACY_TEXT_VIRTUAL_SIZE_OFFSET = 0x1b8; // 0x1b8，实际代码长度
    static constexpr int32_t LEGACY_TEXT_RAW_SIZE_OFFSET = 0x1c0; // 0x1c0，与 SizeOfCode 一致
    static constexpr int32_t LEGACY_GETCHAR_PART_OFFSET = 0x39f;
    static constexpr int32_t LEGACY_CALLOC_PART_OFFSET = 0x3ba;
    static constexpr int32_t LEGACY_CALLOC_ARRAY_SIZE_OFFSET = 0x3d8;
    static constexpr int32_t LEGACY_NORMAL_PART_LEN = LEGACY_GETCHAR_PART_OFFSET - 0x200;
    static constexpr int32_t LEGACY_GETCHAR_PART_LEN = LEGACY_CALLOC_PART_OFFSET - LEGACY_GETCHAR_PART_OFFSET;
    static constexpr int32_t LEGACY_CALLOC_PART_LEN = s_legacyPEPlusRuntimeData.size() - LEGACY_CALLOC_PART_OFFSET;

    static std::array<unsigned char, 592> s_modernPEPlusHeader;
    static constexpr int32_t MODERN_SIZE_OF_CODE_OFFSET = 0xe4; // 实际代码长度向上取整到 512
    static constexpr int32_t MODERN_SIZE_OF_INITIALIZED_DATA_OFFSET = 0xe8; // 512 + 实际数组长度向上取整到 512
    static constexpr int32_t MODERN_SIZE_OF_IMAGE_OFFSET = 0x118;   // 8192 + 实际代码长度向上取整到 4096 + 实际数组长度向上取整到 4096
    static constexpr int32_t MODERN_IMPORT_DIRECTORY_RVA_OFFSET = 0x158;    // rdata 的 virtualAddress + 0x138
    static constexpr int32_t MODERN_DEBUG_DIRECTORY_RVA_OFFSET = 0x180; // rdata 的 virtualAddress + 0x28
    static constexpr int32_t MODERN_IMPORT_ADDRESS_TABLE_DIRECTORY_RVA_OFFSET = 0x1b0;  // rdata 的 virtualAddress
    static constexpr int32_t MODERN_TEXT_VIRTUAL_SIZE_OFFSET = 0x1d8;   // 实际代码长度
    static constexpr int32_t MODERN_TEXT_RAW_SIZE_OFFSET = 0x1e0;   // 与 SizeOfCode 一致
    static constexpr int32_t MODERN_RDATA_VIRTUAL_ADDRESS_OFFSET = 0x204;   // 0x1000 + 实际代码长度向上取整到 4096
    static constexpr int32_t MODERN_RDATA_RAW_ADDRESS_OFFSET = 0x20c;   // 0x400 + SizeOfCode
    static constexpr int32_t MODERN_DATA_VIRTUAL_SIZE_OFFSET = 0x228;   // 实际数组长度（至少为 1）
    static constexpr int32_t MODERN_DATA_VIRTUAL_ADDRESS_OFFSET = 0x22c;    // 0x2000 + 实际代码长度向上取整到 4096

    static std::array<unsigned char, 37> s_modernPEPlusInitInsts;
    static constexpr int32_t MODERN_MOV_EXIT_PROCESS_RIP_RELATIVE_OFFSET = 7;   // 0xff5 + (实际代码长度向上取整到 4096 - 4096)
    static constexpr int32_t MODERN_MOV_PUTCHAR_RIP_RELATIVE_OFFSET = 14;   // 0x1006 + (实际代码长度向上取整到 4096 - 4096)
    static constexpr int32_t MODERN_MOV_GETCHAR_RIP_RELATIVE_OFFSET = 21;   // 0xff7 + (实际代码长度向上取整到 4096 - 4096)
    static constexpr int32_t MODERN_LEA_PTR_RIP_RELATIVE_OFFSET = 33;   // 0xfdb + 实际代码长度向上取整到 4096 + 起始偏移量

    static std::array<unsigned char, 512> s_modernPEPlusRData;
    static constexpr int32_t MODERN_DEBUG_ADDRESS_OF_RAW_DATA_OFFSET = 0x3c;    // rdata 的 virtualAddress + 0x5c
    static constexpr int32_t MODERN_DEBUG_POINTER_TO_RAW_DATA = 0x40;   // rdata 的 rawAddress + 0x5c
    static constexpr int32_t MODERN_IMPORT_KERNEL32_OFTS = 0x138;   // rdata 的 virtualAddress + 0x178
    static constexpr int32_t MODERN_IMPORT_KERNEL32_NAME_RVA = 0x144;   // rdata 的 virtualAddress + 0x1ae
    static constexpr int32_t MODERN_IMPORT_KERNEL32_FTS = 0x148;    // rdata 的 virtualAddress
    static constexpr int32_t MODERN_IMPORT_STDIO_OFTS = 0x14c;  // rdata 的 virtualAddress + 0x188
    static constexpr int32_t MODERN_IMPORT_STDIO_NAME_RVA = 0x158;  // rdata 的 virtualAddress + 0x1d0
    static constexpr int32_t MODERN_IMPORT_STDIO_FTS = 0x15c;   // rdata 的 virtualAddress + 0x10
    // 下方变为 qword
    static constexpr int32_t MODERN_IMPORT_EXIT_PROCESS_OFTS = 0x178;   // rdata 的 virtualAddress + 0x1a0
    static constexpr int32_t MODERN_IMPORT_EXIT_PROCESS_FTS = 0x0;  // rdata 的 virtualAddress + 0x1a0
    static constexpr int32_t MODERN_IMPORT_GETCHAR_OFTS = 0x188;    // rdata 的 virtualAddress + 0x1c6
    static constexpr int32_t MODERN_IMPORT_GETCHAR_FTS = 0x10;  // rdata 的 virtualAddress + 0x1c6
    static constexpr int32_t MODERN_IMPORT_PUTCHAR_OFTS = 0x190;    // rdata 的 virtualAddress + 0x1bc
    static constexpr int32_t MODERN_IMPORT_PUTCHAR_FTS = 0x18;  // rdata 的 virtualAddress + 0x1bc

    std::vector<std::unique_ptr<X64BasicBlock>> m_basicBlocks;
    std::vector<X64BasicBlock*> m_orderedBlocks;
    int32_t m_tapeSize;
    int32_t m_initOffset;
    bool m_needGetchar;
};


#endif //YETANOTHERNEOBRAINFUCKCOMPILER_WINDOWS_PE_PLUS_GENERATOR_H
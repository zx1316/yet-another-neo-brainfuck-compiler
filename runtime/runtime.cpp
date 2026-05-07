#include <windows.h>

int strEquals(const char *s1, const char *s2) {
    while (true) {
        if (*s1 == 0 && *s2 == 0) {
            return 1;
        }
        if (*s1 != *s2) {
            return 0;
        }
        s1++;
        s2++;
    }
}

FARPROC myGetProcAddress(const HMODULE hModule, const char* targetFuncName) {
    // 分析 PE 文件找到导出表
    auto pNtHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE *>(hModule) + reinterpret_cast<PIMAGE_DOS_HEADER>(hModule)->e_lfanew);
    auto pExportDirectory = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(reinterpret_cast<BYTE *>(hModule) + pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

    // 获取导出表的各个信息
    auto pFunctionNames = reinterpret_cast<DWORD *>(reinterpret_cast<BYTE *>(hModule) + pExportDirectory->AddressOfNames);
    auto pFunctionAddresses = reinterpret_cast<DWORD *>(reinterpret_cast<BYTE *>(hModule) + pExportDirectory->AddressOfFunctions);
    auto pFunctionOrdinals = reinterpret_cast<WORD *>(reinterpret_cast<BYTE *>(hModule) + pExportDirectory->AddressOfNameOrdinals);

    // 遍历导出表，查找目标函数
    for (DWORD i = 0; i < pExportDirectory->NumberOfNames; i++) {
        auto *functionName = reinterpret_cast<char *>(reinterpret_cast<BYTE *>(hModule) + pFunctionNames[i]);
        // 找到函数名，获取其地址
        if (strEquals(functionName, targetFuncName)) {
            return reinterpret_cast<FARPROC>(reinterpret_cast<BYTE *>(hModule) + pFunctionAddresses[pFunctionOrdinals[i]]);
        }
    }

    return nullptr;
}

HMODULE findHKernel32() {
    // 从网上复制的结构定义
    struct UNICODE_STRING {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR  Buffer;
    };

    auto peb = reinterpret_cast<PVOID64>(__readgsqword(0x60));
    PVOID64 ldrDataAddr = *reinterpret_cast<PVOID64 **>(static_cast<BYTE *>(peb) + 0x018);  // 0x018 是 LDR 相对于 PEB 偏移，存放着 LDR 的基地址
    auto pNode = reinterpret_cast<LIST_ENTRY *>(*reinterpret_cast<PVOID64 **>(static_cast<BYTE *>(ldrDataAddr) + 0x30));  // 偏移到 InInitializationOrderModuleList
    while (true) {
        auto *fullName = reinterpret_cast<UNICODE_STRING *>(reinterpret_cast<BYTE *>(pNode) + 0x38);    // BaseDllName 基于 InInitializationOrderModuleList 的偏移
        // 太粗暴了，KERNEL32.DLL 刚好是12个字符，但是有效，到目前为止还没有失败过
        if(fullName->Buffer[12] == L'\0') {
            return reinterpret_cast<HMODULE>(*reinterpret_cast<ULONG64 *>(reinterpret_cast<BYTE *>(pNode) + 0x10)); // DllBase
        }
        pNode = pNode->Flink;
    }
}

int main() {
    // 不依赖任何数据段，用于比较的符号名放在栈上
    char nameBuf[16];

    auto hKernel32 = findHKernel32();

    // find GetProcAddress manually from hKernel32
    nameBuf[0] = 'G'; nameBuf[1] = 'e'; nameBuf[2] = 't'; nameBuf[3] = 'P';
    nameBuf[4] = 'r'; nameBuf[5] = 'o'; nameBuf[6] = 'c'; nameBuf[7] = 'A';
    nameBuf[8] = 'd'; nameBuf[9] = 'd'; nameBuf[10] = 'r'; nameBuf[11] = 'e';
    nameBuf[12] = 's'; nameBuf[13] = 's'; nameBuf[14] = 0; nameBuf[15] = 0;
    auto pGetProcAddress = reinterpret_cast<FARPROC(*)(HMODULE, LPCSTR)>(myGetProcAddress(hKernel32, nameBuf));

    // find LoadLibraryA from hKernel32
    nameBuf[0] = 'L'; nameBuf[1] = 'o'; nameBuf[2] = 'a'; nameBuf[3] = 'd';
    nameBuf[4] = 'L'; nameBuf[5] = 'i'; nameBuf[6] = 'b'; nameBuf[7] = 'r';
    nameBuf[8] = 'a'; nameBuf[9] = 'r'; nameBuf[10] = 'y'; nameBuf[11] = 'A';
    nameBuf[12] = 0; nameBuf[13] = 0; nameBuf[14] = 0; nameBuf[15] = 0;
    auto pLoadLibraryA = reinterpret_cast<HMODULE(*)(LPCSTR)>(pGetProcAddress(hKernel32, nameBuf));

    // find ExitProcess from hKernel32
    nameBuf[0] = 'E'; nameBuf[1] = 'x'; nameBuf[2] = 'i'; nameBuf[3] = 't';
    nameBuf[4] = 'P'; nameBuf[5] = 'r'; nameBuf[6] = 'o'; nameBuf[7] = 'c';
    nameBuf[8] = 'e'; nameBuf[9] = 's'; nameBuf[10] = 's'; nameBuf[11] = 0;
    // store pExitProcess in stack, not in register
    volatile auto pExitProcess = reinterpret_cast<void (*)(DWORD)>(pGetProcAddress(hKernel32, nameBuf));

    // load ucrtbase.dll
    nameBuf[0] = 'u'; nameBuf[1] = 'c'; nameBuf[2] = 'r'; nameBuf[3] = 't';
    nameBuf[4] = 'b'; nameBuf[5] = 'a'; nameBuf[6] = 's'; nameBuf[7] = 'e';
    nameBuf[8] = '.'; nameBuf[9] = 'd'; nameBuf[10] = 'l'; nameBuf[11] = 'l';
    nameBuf[12] = 0; nameBuf[13] = 0; nameBuf[14] = 0; nameBuf[15] = 0;
    auto hUcrtOrMsvcrt = pLoadLibraryA(nameBuf);
    if (hUcrtOrMsvcrt == nullptr) {
        // load msvcrt.dll as fallback
        nameBuf[0] = 'm'; nameBuf[1] = 's'; nameBuf[2] = 'v'; nameBuf[3] = 'c';
        nameBuf[4] = 'r'; nameBuf[5] = 't'; nameBuf[6] = '.'; nameBuf[7] = 'd';
        nameBuf[8] = 'l'; nameBuf[9] = 'l'; nameBuf[10] = 0; nameBuf[11] = 0;
        hUcrtOrMsvcrt = pLoadLibraryA(nameBuf);
    }

    // find putchar from hUcrtOrMsvcrt
    nameBuf[0] = 'p'; nameBuf[1] = 'u'; nameBuf[2] = 't'; nameBuf[3] = 'c';
    nameBuf[4] = 'h'; nameBuf[5] = 'a'; nameBuf[6] = 'r'; nameBuf[7] = 0;
    auto pPutchar = reinterpret_cast<void (*)(int)>(pGetProcAddress(hUcrtOrMsvcrt, nameBuf));

    // find getchar from hUcrtOrMsvcrt
    nameBuf[0] = 'g'; nameBuf[1] = 'e'; nameBuf[2] = 't'; nameBuf[3] = 'c';
    nameBuf[4] = 'h'; nameBuf[5] = 'a'; nameBuf[6] = 'r'; nameBuf[7] = 0;
    auto pGetchar = reinterpret_cast<int (*)()>(pGetProcAddress(hUcrtOrMsvcrt, nameBuf));

    // find calloc from hUcrtOrMsvcrt
    nameBuf[0] = 'c'; nameBuf[1] = 'a'; nameBuf[2] = 'l'; nameBuf[3] = 'l';
    nameBuf[4] = 'o'; nameBuf[5] = 'c'; nameBuf[6] = 0; nameBuf[7] = 0;
    auto pCalloc = reinterpret_cast<void *(*)(size_t, size_t)>(pGetProcAddress(hUcrtOrMsvcrt, nameBuf));

    auto *tempBuf = static_cast<unsigned char *>(pCalloc(1, 4096));

    pPutchar('h');
    pPutchar('e');
    pPutchar('l');
    pPutchar('l');
    pPutchar('o');
    pPutchar(':');
    pPutchar(' ');
    auto ch = pGetchar();
    tempBuf[0] = ch;
    pPutchar('c');
    pPutchar('h');
    pPutchar(':');
    pPutchar(' ');
    pPutchar(ch);

    // 不需要FreeLibrary，不需要free，操作系统来擦屁股
    pExitProcess(0);
    return 0;   // 其实这里的return没用
}

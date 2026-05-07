#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_X64_BASIC_BLOCK_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_X64_BASIC_BLOCK_H

#include <ostream>
#include <set>
#include <span>
#include <vector>

#include "x64_asm.h"
#include "x64_encoding_ctx.h"
#include "../optimizer/quadruple.h"
#include "../interpreter/interpreter.h"

class X64BasicBlock {
public:
    friend std::ostream& operator<<(std::ostream& os, const X64BasicBlock& obj);

    // 获取解释器 getters
    static GettersForInterpret getGettersForInterpreter();

    void setIRs(const std::vector<Quadruple>& irs);
    void setIRs(std::vector<Quadruple>&& irs) noexcept;
    [[nodiscard]] const std::vector<Quadruple>& irs() const { return m_irs; }
    [[nodiscard]] const std::vector<X64Asm>& asms() const { return m_asms; }
    void addPred(X64BasicBlock* pred) { m_preds.push_back(pred); }
    void setSuccN(X64BasicBlock* succN) { m_succN = succN; }
    void setSuccY(X64BasicBlock* succY) { m_succY = succY; }
    [[nodiscard]] X64BasicBlock* succN() const { return m_succN; }
    [[nodiscard]] X64BasicBlock* succY() const { return m_succY; }
    [[nodiscard]] const std::vector<X64BasicBlock*>& preds() const { return m_preds; }
    [[nodiscard]] int32_t spillCount() const { return m_spillCount; }
    [[nodiscard]] int32_t codeSize() const { return m_codeSize; }
    [[nodiscard]] bool hasGetchar() const { return m_hasGetchar; }
    [[nodiscard]] bool hasPutchar() const { return m_hasPutchar; }
    [[nodiscard]] bool canUseGetcharReg() const { return m_canUseGetcharReg; }
    void setCanUseGetcharReg(bool b) { m_canUseGetcharReg = b; }
    // [[nodiscard]] bool isAlign() const { return m_isAlign; }
    // void setIsAlign(bool b) { m_isAlign = b; }

    // 等效翻转条件跳转，jnz <-> jz，交换 succY 和 succN
    void equivFlipCondJmp();

    // 在末尾额外放置一个 jmp
    void placeJmp();

    // 消除相邻跳转
    void removeJmpIfJmpTo(int32_t target);

    // 在跳转指令里临时填充跳转目的基本块的 id
    void setJmpTarget(const std::unordered_map<X64BasicBlock*, int32_t>& idMap);

    // 首次计算代码大小
    void firstCalcCodeSize();

    // 尝试用 short 跳转替换 near
    bool tryShortJmp(int32_t thisId, const int32_t* codePos);

    // 最终设置跳转相对量
    void finalSetJmpRel(int32_t thisId, const int32_t* codePos);

    // 最终编码为机器码
    void finalEncode(X64EncodingCtx& ctx) const;

    // 重排序并合并临时变量，为生成汇编做铺垫
    // 优化后才能调用，未优化时调用则必出错
    // 返回重排后的中间代码数
    int32_t rearrange();

    // 生成 windows 汇编
    // 听我说，看看头文件好了，不要看实现
    void generateWindows(int32_t optimizeLevel);

private:
    struct TempAsmInfo {
        int32_t liveBegin;  // 闭区间
        int32_t liveEnd;    // 闭区间
        std::set<int32_t> usePos;
        int32_t currentRegOrMem;
        bool isUsedByPutchar;
        bool isCrossFuncs;
        bool isSpill;
    };

    // 下面的函数顾名思义
    
    void collectTempInfo(std::unordered_map<int32_t, TempAsmInfo>& tempInfos) const;
    void postOptimize(int32_t optimizeLevel);

    // 整数常数在 [-128, -3]∪[3, 127]，照理来说优化后不可能出现乘 0 或 ±1 的情况，重排序时已经处理了乘 ±2 的情况
    int32_t imulConstant(
        int32_t index,
        int32_t targetId,
        X64AsmOperand::Reg sourceReg,
        X64AsmOperand::Reg targetReg,
        int32_t constant,
        int32_t oldZfTemp,
        bool* eStates
    );

    [[nodiscard]] bool isNextToCondJmp(int32_t index, int32_t target) const;

    X64AsmOperand::Reg ensureOldTempInReg(
        int32_t index,
        int32_t target,
        std::unordered_map<int32_t, TempAsmInfo>& tempInfos,
        int32_t* regUse,
        bool* eStates,
        std::vector<int32_t>& spills
    );

    X64AsmOperand::Reg findReg(
        int32_t index,
        int32_t target,
        std::unordered_map<int32_t, TempAsmInfo>& tempInfos,
        int32_t* regUse,
        std::vector<int32_t>& spills
    );

    X64AsmOperand::Reg findAnotherRegForMul(
        int32_t index,
        std::optional<X64AsmOperand::Reg> exclude,
        std::unordered_map<int32_t, TempAsmInfo>& tempInfos,
        int32_t* regUse,
        std::vector<int32_t>& spills
    );

    X64AsmOperand::Reg findRegInCandidates(
        int32_t index,
        std::unordered_map<int32_t, TempAsmInfo>& tempInfos,
        int32_t* regUse,
        std::vector<int32_t>& spills,
        std::span<const X64AsmOperand::Reg> regCandidates
    );

    std::vector<Quadruple> m_irs;
    std::vector<X64Asm> m_asms;
    std::vector<X64BasicBlock*> m_preds;
    X64BasicBlock* m_succN = nullptr;
    X64BasicBlock* m_succY = nullptr;
    int32_t m_spillCount = 0;
    int32_t m_codeSize = 0;
    bool m_isRearranged = false;
    bool m_lastJmpIsShort = false;
    bool m_secondLastJmpIsShort = false;
    bool m_hasPutchar = false;
    bool m_hasGetchar = false;
    bool m_canUseGetcharReg = false;
    // bool m_isAlign = false;
};



#endif //YETANOTHERNEOBRAINFUCKCOMPILER_X64_BASIC_BLOCK_H

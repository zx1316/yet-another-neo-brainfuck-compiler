#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_MID_BASIC_BLOCK_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_MID_BASIC_BLOCK_H

#include <ostream>
#include <unordered_set>
#include <vector>

#include "block_live_var_info.h"
#include "ptr_state.h"
#include "quadruple.h"
#include "var_value_info.h"
#include "../interpreter/interpreter.h"

struct MidBasicBlock {
    static GettersForInterpret getGettersForInterpreter();
    static int32_t findAvailableTempId(const std::vector<Quadruple>& codes);

    [[nodiscard]] bool isPtrStateUnknown() const { return ptrIn.isUnknown || ptrIn.isUnreachable; }

    // 计算指针移动量
    int32_t calcDelta();

    // 计算变量值变化，需要初始化变量值信息
    void calcVarChange() const;

    // 计算 use define，需要初始化活跃变量信息
    void calcUseDef() const;

    // 获取进入基本块时指针所指变量的零/非零情况
    // 其实就是读前驱节点的JZ JNZ
    // 全局优化时不适用，因为全局优化时变量为全局偏移量，而且跳转前可能压根就不写入那个变量
    [[nodiscard]] VarValueInfo getZeroVarInfoLocally() const;

    // 获取“变量”，即用到的所有 offset
    [[nodiscard]] std::unordered_set<int32_t> getVars() const;

    // 获取无 ptr_add 的四元式列表
    // 返回 [无 ptr_add 四元式列表, 指针总移动量]
    [[nodiscard]] std::pair<std::vector<Quadruple>, int32_t> getNoPtrMoveForm() const;

    // 尝试消除某些类型的循环，isStrict 为 false 时为激进模式
    // 返回 [是否产生优化, 负保护长度, 正保护长度]
    std::tuple<bool, int32_t, int32_t> eliminateLoop(bool isStrict);

    // 通用优化
    // varInfos 为进入基本块时的变量信息，key 是相对进入基本块时的指针的位置，key 不存在也表示值未知
    bool generalOptimize(const std::unordered_map<int32_t, VarValueInfo>& varInfos);

    // 尝试提升循环中的常量赋值
    // 调用前必须预先优化基本块，每个变量只能被赋值一次
    std::unique_ptr<MidBasicBlock> hoistLoopConstAssign();

    // 消除指针移动
    // 调用前必须进行指针分析
    std::unique_ptr<MidBasicBlock> eliminatePtrMove();

    // 全局消除无用赋值
    // 调用前必须进行活跃变量分析
    bool eliminateUselessVarAssign();

    // 变量重命名
    void renameVar(const std::unordered_map<int32_t, int32_t>& offsetMap);

    friend std::ostream& operator<<(std::ostream& os, const MidBasicBlock& obj);
    void printBrief(std::ostream& os) const;

    std::vector<Quadruple> irs;
    std::vector<MidBasicBlock*> preds;
    MidBasicBlock* succN = nullptr;    // if jmp condition is not met or unconditional
    MidBasicBlock* succY = nullptr;    // if jmp condition is met
    std::unique_ptr<BlockVarValueInfo> varValueInfo;
    std::unique_ptr<BlockLiveVarInfo> liveVarInfo;
    PtrState ptrIn = PtrState::Unreachable();
    PtrState ptrOut = PtrState::Unreachable();
    int32_t ptrDelta = 0;

private:
    static void shrinkOptimize(std::vector<Quadruple>& src, std::vector<Quadruple>& dst, bool (Quadruple::*mergeFunc)(const Quadruple &));

    // Extended Euclidean algorithm.
    static void exGCD(int32_t a, int32_t b, int32_t* x, int32_t* y);

    // Returns the modular inverse of a modulo m.
    // Assumes that gcd(a, 256) = 1, so the inverse exists.
    static int32_t inverse256(int32_t a);

};


#endif //YETANOTHERNEOBRAINFUCKCOMPILER_MID_BASIC_BLOCK_H
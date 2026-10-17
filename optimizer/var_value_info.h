#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_VAR_INFO_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_VAR_INFO_H

#include <cstdint>
#include <ranges>
#include <unordered_map>

struct VarValueInfo {
    enum class InfoType {
        UNKNOWN = 0, NOT_ZERO, EXACT, UNREACHABLE
    };

    constexpr static VarValueInfo unknown() {
        return {InfoType::UNKNOWN};
    }

    constexpr static VarValueInfo notZero() {
        return {InfoType::NOT_ZERO};
    }

    constexpr static VarValueInfo exact(int32_t exactValue) {
        return {InfoType::EXACT, exactValue};
    }

    constexpr static VarValueInfo unreachable() {
        return {InfoType::UNREACHABLE};
    }

    friend constexpr bool operator==(const VarValueInfo& lhs, const VarValueInfo& rhs) {
        if (lhs.type != rhs.type) return false;
        if (lhs.type != InfoType::EXACT) return true;
        return lhs.exactValue == rhs.exactValue;
    }

    friend constexpr bool operator!=(const VarValueInfo& lhs, const VarValueInfo& rhs) { return !(lhs == rhs); }

    [[nodiscard]] constexpr VarValueInfo join(const VarValueInfo &other) const {
        // 相同直接返回
        if (*this == other) return *this;
        // 不可达则返回另一个
        if (this->type == InfoType::UNREACHABLE) return other;
        if (other.type == InfoType::UNREACHABLE) return *this;
        // 有一个未知则得到未知
        if (this->type == InfoType::UNKNOWN || other.type == InfoType::UNKNOWN) return unknown();
        // 一个非零一个精确，若精确非 0 则得到非 0，否则位置
        if (this->type == InfoType::NOT_ZERO && other.type == InfoType::EXACT) {
            if (other.exactValue != 0) return notZero();
            return unknown();
        }
        if (this->type == InfoType::EXACT && other.type == InfoType::NOT_ZERO) {
            if (this->exactValue != 0) return notZero();
            return unknown();
        }
        // 两个都精确且值不同，若都非 0 则得到非 0，否则未知（精确值相同的前面检查过了）
        if (this->exactValue != 0 && other.exactValue != 0) return notZero();
        return unknown();
    }

    InfoType type = InfoType::UNKNOWN;
    int32_t exactValue = 0;
};

struct BlockVarValueInfo {
    std::unordered_map<int32_t, VarValueInfo> in;
    std::unordered_map<int32_t, VarValueInfo> out;
    std::unordered_map<int32_t, VarValueInfo> change;
    Quadruple::OpType jmpType;
    int32_t jmpTestOffset;

    explicit BlockVarValueInfo(const std::unordered_set<int32_t> &vars) : jmpType(Quadruple::OpType::JMP), jmpTestOffset(0) {
        for (auto var : vars) {
            in[var] = VarValueInfo::unreachable();
            out[var] = VarValueInfo::unreachable();
        }
    }

    void setAllInZero() {
        for (auto& info : in | std::views::values) {
            info = VarValueInfo::exact(0);
        }
    }

    // 把所有值重置为"不可达"，等价于重新构造一次但保留已分配的桶，
    // 用于增量重算时复用对象（变量集合不变的前提下）。
    void resetValues() {
        for (auto& info : in | std::views::values) {
            info = VarValueInfo::unreachable();
        }
        for (auto& info : out | std::views::values) {
            info = VarValueInfo::unreachable();
        }
    }

    void calcOut() {
        for (auto &[offset, varInfo] : in) {
            if (varInfo.type == VarValueInfo::InfoType::UNREACHABLE || !change.contains(offset)) {
                out[offset] = varInfo;
            } else {
                out[offset] = change[offset];
            }
        }
    }

    bool join(const BlockVarValueInfo& pred, bool isSuccY) {
        // 原来每次 join 都整表拷贝 in、再整表比较，是热点内层循环。
        // 改成原地逐项更新：只有真正改变的项才写回，同时记录是否有变化。
        bool hasChanged = false;
        auto joinInto = [this, &hasChanged](int32_t offset, const VarValueInfo& value) {
            auto it = in.find(offset);
            if (it == in.end()) {
                // 缺项等价于 UNKNOWN，插入本身就和原来的「表大小变了」一样算变化
                in.emplace(offset, VarValueInfo{});
                hasChanged = true;
                return;
            }
            auto joined = it->second.join(value);
            if (joined != it->second) {
                it->second = joined;
                hasChanged = true;
            }
        };

        for (auto &[offset, varInfo] : pred.out) {
            if (offset == pred.jmpTestOffset) [[unlikely]] {
                // 跳转覆写
                if (pred.jmpType == Quadruple::OpType::JZ) {
                    joinInto(pred.jmpTestOffset, isSuccY ? VarValueInfo::exact(0) : VarValueInfo::notZero());
                } else if (pred.jmpType == Quadruple::OpType::JNZ) {
                    joinInto(pred.jmpTestOffset, isSuccY ? VarValueInfo::notZero() : VarValueInfo::exact(0));
                } else {
                    joinInto(offset, varInfo);
                }
            } else {
                joinInto(offset, varInfo);
            }
        }
        return hasChanged;
    }

};

#endif //YETANOTHERNEOBRAINFUCKCOMPILER_VAR_INFO_H

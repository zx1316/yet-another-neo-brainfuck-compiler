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
        auto newIn = in;
        for (auto &[offset, varInfo] : pred.out) {
            if (offset == pred.jmpTestOffset) [[unlikely]] {
                // 跳转覆写
                if (pred.jmpType == Quadruple::OpType::JZ) {
                    newIn[pred.jmpTestOffset] = newIn[pred.jmpTestOffset].join(isSuccY ? VarValueInfo::exact(0) : VarValueInfo::notZero());
                } else if (pred.jmpType == Quadruple::OpType::JNZ) {
                    newIn[pred.jmpTestOffset] = newIn[pred.jmpTestOffset].join(isSuccY ? VarValueInfo::notZero() : VarValueInfo::exact(0));
                } else {
                    newIn[offset] = newIn[offset].join(varInfo);
                }
            } else {
                newIn[offset] = newIn[offset].join(varInfo);
            }
        }
        bool hasChanged = newIn != in;
        if (hasChanged) {
            in = std::move(newIn);
        }
        return hasChanged;
    }

};

#endif //YETANOTHERNEOBRAINFUCKCOMPILER_VAR_INFO_H

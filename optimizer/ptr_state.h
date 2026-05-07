#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_PTR_STATE_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_PTR_STATE_H

#include <cstdint>
#include <ostream>


struct PtrState {
    static PtrState Unreachable() {
        return {false, true, 0};
    }
    static PtrState Unknown() {
        return {true, false, 0};
    }
    static PtrState Constant(int32_t pos) {
        return {false, false, pos};
    }

    friend bool operator==(const PtrState &lhs, const PtrState &rhs) {
        return lhs.isUnknown == rhs.isUnknown
               && lhs.isUnreachable == rhs.isUnreachable
               && lhs.pos == rhs.pos;
    }

    friend bool operator!=(const PtrState &lhs, const PtrState &rhs) {
        return !(lhs == rhs);
    }

    friend std::ostream & operator<<(std::ostream &os, const PtrState &obj) {
        if (obj.isUnknown) {
            return os << "PtrState{UNKNOWN}";
        }
        if (obj.isUnreachable) {
            return os << "PtrState{UNREACHABLE}";
        }
        return os << "PtrState{CONSTANT, " << obj.pos << '}';
    }

    [[nodiscard]] PtrState join(const PtrState &other) const {
        if (this->isUnreachable) return other;
        if (other.isUnreachable) return *this;
        if (this->isUnknown || other.isUnknown) return Unknown();   // 未知传染
        if (this->pos == other.pos) return *this;   // 合并相同的位置
        return Unknown(); // 两个不同的位置汇聚，变为未知
    }

    bool isUnknown;
    bool isUnreachable;
    int32_t pos;
};

#endif //YETANOTHERNEOBRAINFUCKCOMPILER_PTR_STATE_H

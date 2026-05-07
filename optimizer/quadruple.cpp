#include "quadruple.h"

#include <array>

const char * const QuadrupleArg::ARG_TYPE_TRANSLATE[4] = {
    "NIL", "PTR_OFFSET", "BB_TEMP", "CONSTANT",
};

const char * const Quadruple::CODE_TYPE_TRANSLATE[12] = {
    "HALT", "PTR_ADD", "ADD", "SUB", "MUL", "NEG", "ASSIGN", "JNZ", "JZ", "JMP", "PUTCHAR", "GETCHAR",
};

using ArgType = QuadrupleArg::ArgType;

// constant in add and mul must in arg2
const std::array<Quadruple::SingleSimpRule, 13> Quadruple::SINGLE_SIMP_RULES = {
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::ADD && current.arg1.type == ArgType::CONSTANT && current.arg2.type == ArgType::CONSTANT;
        },
        [](Quadruple &current) {
            // constant folding
            current.type = OpType::ASSIGN;
            current.arg1.value = static_cast<int8_t>(current.arg1.value + current.arg2.value);
            current.arg2.type = ArgType::NIL;
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::ADD && current.arg2.type == ArgType::CONSTANT && current.arg2.value == 0;
        },
        [](Quadruple &current) {
            // a + 0 = a
            current.type = OpType::ASSIGN;
            current.arg2.type = ArgType::NIL;
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::ADD && current.arg1 == current.arg2;
        },
        [](Quadruple &current) {
            // a + a = a * 2
            current.type = OpType::MUL;
            current.arg2 = {ArgType::CONSTANT, 2};
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::SUB && current.arg1 == current.arg2;
        },
        [](Quadruple &current) {
            // a - a = 0
            current.type = OpType::ASSIGN;
            current.arg1 = {ArgType::CONSTANT, 0};
            current.arg2.type = ArgType::NIL;
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::SUB && current.arg1.type == ArgType::CONSTANT && current.arg2.type == ArgType::CONSTANT;
        },
        [](Quadruple &current) {
            // constant folding
            current.type = OpType::ASSIGN;
            current.arg1.value = static_cast<int8_t>(current.arg1.value - current.arg2.value);
            current.arg2.type = ArgType::NIL;
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::SUB && current.arg2.type == ArgType::CONSTANT && current.arg2.value == 0;
        },
        [](Quadruple &current) {
            // a - 0 = a
            current.type = OpType::ASSIGN;
            current.arg2.type = ArgType::NIL;
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::SUB && current.arg1.type == ArgType::CONSTANT && current.arg1.value == 0;
        },
        [](Quadruple &current) {
            // 0 - a = -a
            current.type = OpType::NEG;
            current.arg1 = current.arg2;
            current.arg2.type = ArgType::NIL;
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::SUB && current.arg2.type == ArgType::CONSTANT;
        },
        [](Quadruple &current) {
            // a - c = a + (-c)
            current.type = OpType::ADD;
            current.arg2.value = static_cast<int8_t>(-current.arg2.value);
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::MUL && current.arg1.type == ArgType::CONSTANT && current.arg2.type == ArgType::CONSTANT;
        },
        [](Quadruple &current) {
            // constant folding
            current.type = OpType::ASSIGN;
            current.arg1.value = static_cast<int8_t>(current.arg1.value * current.arg2.value);
            current.arg2.type = ArgType::NIL;
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::MUL && current.arg2.type == ArgType::CONSTANT && current.arg2.value == 0;
        },
        [](Quadruple &current) {
            // a * 0 = 0
            current.type = OpType::ASSIGN;
            current.arg1 = {ArgType::CONSTANT, 0};
            current.arg2.type = ArgType::NIL;
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::MUL && current.arg2.type == ArgType::CONSTANT && current.arg2.value == 1;
        },
        [](Quadruple &current) {
            // a * 1 = a
            current.type = OpType::ASSIGN;
            current.arg2.type = ArgType::NIL;
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::MUL && current.arg2.type == ArgType::CONSTANT && current.arg2.value == -1;
        },
        [](Quadruple &current) {
            // a * -1 = -a
            current.type = OpType::NEG;
            current.arg2.type = ArgType::NIL;
        }
    },
    SingleSimpRule{
        [](const Quadruple &current) {
            return current.type == OpType::NEG && current.arg1.type == ArgType::CONSTANT;
        },
        [](Quadruple &current) {
            // constant folding
            current.type = OpType::ASSIGN;
            current.arg1.value = static_cast<int8_t>(-current.arg1.value);
        }
    },
};

// constant in add and mul must in arg2
const std::array<Quadruple::MergeRule, 24> Quadruple::MERGE_RULES = {
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x
            // t3 = y op t2
            return other.type == OpType::ASSIGN && (current.type == OpType::ADD || current.type == OpType::MUL || current.type == OpType::SUB) && other.result == current.arg2;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x op y
            current.arg2 = other.arg1;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x
            // t3 = t2 op y
            return other.type == OpType::ASSIGN && (current.type == OpType::ADD || current.type == OpType::MUL
                || current.type == OpType::SUB || current.type == OpType::NEG || current.type == OpType::PUTCHAR
                || (current.type == OpType::ASSIGN && current.result.type == ArgType::BB_TEMP))
                && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x op y
            current.arg1 = other.arg1;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x * c
            // t3 = t2 + x or t3 = x + t2
            return other.type == OpType::MUL && other.arg2.type == ArgType::CONSTANT && current.type == OpType::ADD && (other.result == current.arg1 && other.arg1 == current.arg2 || other.result == current.arg2 && other.arg1 == current.arg1);
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x * (c + 1)
            current.type = OpType::MUL;
            current.arg1 = other.arg1;
            current.arg2 = QuadrupleArg{ArgType::CONSTANT, static_cast<int8_t>(other.arg2.value + 1)};
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x * c
            // t3 = t2 - x
            return other.type == OpType::MUL && other.arg2.type == ArgType::CONSTANT && current.type == OpType::SUB && other.result == current.arg1 && other.arg1 == current.arg2;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x * (c - 1)
            current.type = OpType::MUL;
            current.arg1 = other.arg1;
            current.arg2 = QuadrupleArg{ArgType::CONSTANT, static_cast<int8_t>(other.arg2.value - 1)};
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x * c
            // t3 = x - t2
            return other.type == OpType::MUL && other.arg2.type == ArgType::CONSTANT && current.type == OpType::SUB && other.result == current.arg2 && other.arg1 == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x * (1 - c)
            current.type = OpType::MUL;
            current.arg2 = QuadrupleArg{ArgType::CONSTANT, static_cast<int8_t>(1 - other.arg2.value)};
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = -x
            // t3 = t2 * c1
            return other.type == OpType::NEG && current.type == OpType::MUL && current.arg2.type == ArgType::CONSTANT && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x * -c1
            current.arg1 = other.arg1;
            current.arg2.value = static_cast<int8_t>(-other.arg2.value);
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = -x
            // t4 = t2 + y
            return other.type == OpType::NEG && current.type == OpType::ADD && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t4 = y - x
            current.type = OpType::SUB;
            current.arg1 = current.arg2;
            current.arg2 = other.arg1;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = -x
            // t4 = y + t2
            return other.type == OpType::NEG && current.type == OpType::ADD && other.result == current.arg2;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t4 = y - x
            current.type = OpType::SUB;
            current.arg2 = other.arg1;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = -x
            // t4 = y - t2
            return other.type == OpType::NEG && current.type == OpType::SUB && other.result == current.arg2;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t4 = y + x
            current.type = OpType::ADD;
            current.arg2 = other.arg1;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = -x
            // t4 = t2 - c3
            return other.type == OpType::NEG && current.type == OpType::SUB && current.arg2.type == ArgType::CONSTANT && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t4 = -c3 - x
            current.arg1 = {ArgType::CONSTANT, static_cast<int8_t>(-current.arg2.value)};
            current.arg2 = other.arg1;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = -x
            // t4 = -t2
            return other.type == OpType::NEG && current.type == OpType::NEG && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t4 = x
            // 若 x 为临时变量则将后续的所有 t4 替换为 x，删除这条赋值
            current.type = OpType::ASSIGN;
            current.arg1 = other.arg1;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x * c1
            // t3 = t2 * c2
            return other.type == OpType::MUL && other.arg2.type == ArgType::CONSTANT && current.type == OpType::MUL && current.arg2.type == ArgType::CONSTANT && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x * (c1 * c2)
            current.arg1 = other.arg1;
            current.arg2.value = static_cast<int8_t>(other.arg2.value * current.arg2.value);
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x * c1
            // t3 = -t2
            return other.type == OpType::MUL && other.arg2.type == ArgType::CONSTANT && current.type == OpType::NEG && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x * -c1
            current.type = OpType::MUL;
            current.arg1 = other.arg1;
            current.arg2 = {ArgType::CONSTANT, static_cast<int8_t>(-other.arg2.value)};
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x - y
            // t3 = -t2
            return other.type == OpType::SUB && current.type == OpType::NEG && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = y - x
            current.type = OpType::SUB;
            current.arg1 = other.arg2;
            current.arg2 = other.arg1;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x - c1
            // t3 = t2 + c2
            return other.type == OpType::SUB && other.arg2.type == ArgType::CONSTANT && current.type == OpType::ADD && current.arg2.type == ArgType::CONSTANT && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x + (c2 - c1)
            current.type = OpType::ADD;
            current.arg1 = other.arg1;
            current.arg2.value = static_cast<int8_t>(current.arg2.value - other.arg2.value);
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = c1 - x
            // t3 = t2 + c2
            return other.type == OpType::SUB && other.arg1.type == ArgType::CONSTANT && current.type == OpType::ADD && current.arg2.type == ArgType::CONSTANT && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = (c1 + c2) - x
            current.type = OpType::SUB;
            current.arg1 = {ArgType::CONSTANT, static_cast<int8_t>(other.arg1.value + current.arg2.value)};
            current.arg2 = other.arg2;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x - c1
            // t3 = t2 - c2
            return other.type == OpType::SUB && other.arg2.type == ArgType::CONSTANT && current.type == OpType::SUB && current.arg2.type == ArgType::CONSTANT && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x + (-c1 - c2)
            current.type = OpType::ADD;
            current.arg1 = other.arg1;
            current.arg2.value = static_cast<int8_t>(-other.arg2.value - current.arg2.value);
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = c1 - x
            // t3 = t2 - c2
            return other.type == OpType::SUB && other.arg1.type == ArgType::CONSTANT && current.type == OpType::SUB && current.arg2.type == ArgType::CONSTANT && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = (c1 - c2) - x
            current.arg1 = {ArgType::CONSTANT, static_cast<int8_t>(other.arg1.value - current.arg2.value)};
            current.arg2 = other.arg2;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x - c1
            // t3 = c2 - t2
            return other.type == OpType::SUB && other.arg2.type == ArgType::CONSTANT && current.type == OpType::SUB && current.arg1.type == ArgType::CONSTANT && other.result == current.arg2;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = (c1 + c2) - x
            current.arg1.value = static_cast<int8_t>(other.arg2.value + current.arg1.value);
            current.arg2 = other.arg1;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = c1 - x
            // t3 = c2 - t2
            return other.type == OpType::SUB && other.arg1.type == ArgType::CONSTANT && current.type == OpType::SUB && current.arg1.type == ArgType::CONSTANT && other.result == current.arg2;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x + (c2 - c1)
            current.type = OpType::ADD;
            current.arg2 = {ArgType::CONSTANT, static_cast<int8_t>(current.arg1.value - other.arg1.value)};
            current.arg1 = other.arg2;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x + c1
            // t3 = -t2
            return other.type == OpType::ADD && other.arg2.type == ArgType::CONSTANT && current.type == OpType::NEG && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = -c1 - x
            current.type = OpType::SUB;
            current.arg1 = {ArgType::CONSTANT, static_cast<int8_t>(-other.arg2.value)};
            current.arg2 = other.arg1;
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x + c1
            // t3 = t2 + c2
            return other.type == OpType::ADD && other.arg2.type == ArgType::CONSTANT && current.type == OpType::ADD && current.arg2.type == ArgType::CONSTANT && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x + (c1 + c2)
            current.arg1 = other.arg1;
            current.arg2.value = static_cast<int8_t>(other.arg2.value + current.arg2.value);
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x + c1
            // t3 = t2 - c2
            return other.type == OpType::ADD && other.arg2.type == ArgType::CONSTANT && current.type == OpType::SUB && current.arg2.type == ArgType::CONSTANT && other.result == current.arg1;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = x + (c1 - c2)
            current.type = OpType::ADD;
            current.arg1 = other.arg1;
            current.arg2.value = static_cast<int8_t>(other.arg2.value - current.arg2.value);
        }
    },
    MergeRule{
        [](const Quadruple &current, const Quadruple &other) {
            // t2 = x + c1
            // t3 = c2 - t2
            return other.type == OpType::ADD && other.arg2.type == ArgType::CONSTANT && current.type == OpType::SUB && current.arg1.type == ArgType::CONSTANT && other.result == current.arg2;
        },
        [](Quadruple &current, const Quadruple &other) {
            // 内联为 t3 = (c2 - c1) - x
            current.arg1.value = static_cast<int8_t>(current.arg1.value - other.arg2.value);
            current.arg2 = other.arg1;
        }
    },
};

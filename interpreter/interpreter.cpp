#include "interpreter.h"

#include <string>

using OpType = Quadruple::OpType;
using ArgType = QuadrupleArg::ArgType;

std::expected<void, Error> Interpreter::interpret(const void* block, const GettersForInterpret& getters, int32_t tapeSize, int32_t ptr, bool checkBound) {
    auto tape = std::make_unique<uint8_t[]>(tapeSize);
    std::unordered_map<int32_t, uint8_t> temps;
    if (checkBound) {
        do {
            auto& codes = getters.getCode(block);
            for (auto& code : codes) {
                if (auto errStr = checkIndex(tapeSize, ptr, code, block, codes); !errStr.empty()) {
                    return std::unexpected(Error{Error::INTERPRET_ERR, std::move(errStr)});
                }
                ptr = executeCode(tape.get(), temps, ptr, code);
            }
            block = getNextBlock(tape.get(), temps, block, ptr, codes, getters.getSuccY, getters.getSuccN);
        } while (block != nullptr);
    } else {
        do {
            auto& codes = getters.getCode(block);
            for (auto& code : codes) {
                ptr = executeCode(tape.get(), temps, ptr, code);
            }
            block = getNextBlock(tape.get(), temps, block, ptr, codes, getters.getSuccY, getters.getSuccN);
        } while (block != nullptr);
    }
    return {};
}

const void* Interpreter::getNextBlock(
    const uint8_t* tape,
    std::unordered_map<int32_t, uint8_t>& temps,
    const void* block,
    int32_t ptr,
    const std::vector<Quadruple>& codes,
    const void* (*succYGetter)(const void*),
    const void* (*succNGetter)(const void*)
) {
    auto* succN = succNGetter(block);
    if (codes.empty()) {
        if (succN == nullptr) {
            return nullptr;
        }
        return succN;
    }
    auto& lastCode = codes.back();
    if (lastCode.type == OpType::JZ) {
        if (lastCode.arg1.type == ArgType::BB_TEMP) {
            return temps[lastCode.arg1.value] == 0 ? succYGetter(block) : succN;
        }
        if (lastCode.arg1.type == ArgType::PTR_OFFSET) {
            return tape[ptr + lastCode.arg1.value] == 0 ? succYGetter(block) : succN;
        }
        return lastCode.arg1.value == 0 ? succYGetter(block) : succN;
    }
    if (lastCode.type == OpType::JNZ) {
        if (lastCode.arg1.type == ArgType::BB_TEMP) {
            return temps[lastCode.arg1.value] != 0 ? succYGetter(block) : succN;
        }
        if (lastCode.arg1.type == ArgType::PTR_OFFSET) {
            return tape[ptr + lastCode.arg1.value] != 0 ? succYGetter(block) : succN;
        }
        return lastCode.arg1.value != 0 ? succYGetter(block) : succN;
    }
    if (succN == nullptr) {
        return nullptr;
    }
    return succN;
}

int32_t Interpreter::executeCode(uint8_t* tape, std::unordered_map<int32_t, uint8_t>& temps, int32_t ptr, const Quadruple& code) {
    switch (code.type) {
        case OpType::PTR_ADD:
            ptr += code.arg1.value;
            break;
        case OpType::ADD:
            if (code.result.type == ArgType::BB_TEMP) {
                if (code.arg1.type == ArgType::BB_TEMP) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        temps[code.result.value] = temps[code.arg1.value] + temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        temps[code.result.value] = temps[code.arg1.value] + tape[ptr + code.arg2.value];
                    } else {
                        temps[code.result.value] = temps[code.arg1.value] + code.arg2.value;
                    }
                } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        temps[code.result.value] = tape[ptr + code.arg1.value] + temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        temps[code.result.value] = tape[ptr + code.arg1.value] + tape[ptr + code.arg2.value];
                    } else {
                        temps[code.result.value] = tape[ptr + code.arg1.value] + code.arg2.value;
                    }
                } else {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        temps[code.result.value] = code.arg1.value + temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        temps[code.result.value] = code.arg1.value + tape[ptr + code.arg2.value];
                    } else {
                        temps[code.result.value] = code.arg1.value + code.arg2.value;
                    }
                }
            } else if (code.result.type == ArgType::PTR_OFFSET) {
                if (code.arg1.type == ArgType::BB_TEMP) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        tape[ptr + code.result.value] = temps[code.arg1.value] + temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        tape[ptr + code.result.value] = temps[code.arg1.value] + tape[ptr + code.arg2.value];
                    } else {
                        tape[ptr + code.result.value] = temps[code.arg1.value] + code.arg2.value;
                    }
                } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        tape[ptr + code.result.value] = tape[ptr + code.arg1.value] + temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        tape[ptr + code.result.value] = tape[ptr + code.arg1.value] + tape[ptr + code.arg2.value];
                    } else {
                        tape[ptr + code.result.value] = tape[ptr + code.arg1.value] + code.arg2.value;
                    }
                } else {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        tape[ptr + code.result.value] = code.arg1.value + temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        tape[ptr + code.result.value] = code.arg1.value + tape[ptr + code.arg2.value];
                    } else {
                        tape[ptr + code.result.value] = code.arg1.value + code.arg2.value;
                    }
                }
            }
            break;
        case OpType::SUB:
            if (code.result.type == ArgType::BB_TEMP) {
                if (code.arg1.type == ArgType::BB_TEMP) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        temps[code.result.value] = temps[code.arg1.value] - temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        temps[code.result.value] = temps[code.arg1.value] - tape[ptr + code.arg2.value];
                    } else {
                        temps[code.result.value] = temps[code.arg1.value] - code.arg2.value;
                    }
                } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        temps[code.result.value] = tape[ptr + code.arg1.value] - temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        temps[code.result.value] = tape[ptr + code.arg1.value] - tape[ptr + code.arg2.value];
                    } else {
                        temps[code.result.value] = tape[ptr + code.arg1.value] - code.arg2.value;
                    }
                } else {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        temps[code.result.value] = code.arg1.value - temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        temps[code.result.value] = code.arg1.value - tape[ptr + code.arg2.value];
                    } else {
                        temps[code.result.value] = code.arg1.value - code.arg2.value;
                    }
                }
            } else if (code.result.type == ArgType::PTR_OFFSET) {
                if (code.arg1.type == ArgType::BB_TEMP) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        tape[ptr + code.result.value] = temps[code.arg1.value] - temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        tape[ptr + code.result.value] = temps[code.arg1.value] - tape[ptr + code.arg2.value];
                    } else {
                        tape[ptr + code.result.value] = temps[code.arg1.value] - code.arg2.value;
                    }
                } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        tape[ptr + code.result.value] = tape[ptr + code.arg1.value] - temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        tape[ptr + code.result.value] = tape[ptr + code.arg1.value] - tape[ptr + code.arg2.value];
                    } else {
                        tape[ptr + code.result.value] = tape[ptr + code.arg1.value] - code.arg2.value;
                    }
                } else {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        tape[ptr + code.result.value] = code.arg1.value - temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        tape[ptr + code.result.value] = code.arg1.value - tape[ptr + code.arg2.value];
                    } else {
                        tape[ptr + code.result.value] = code.arg1.value - code.arg2.value;
                    }
                }
            }
            break;
        case OpType::MUL:
            if (code.result.type == ArgType::BB_TEMP) {
                if (code.arg1.type == ArgType::BB_TEMP) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        temps[code.result.value] = temps[code.arg1.value] * temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        temps[code.result.value] = temps[code.arg1.value] * tape[ptr + code.arg2.value];
                    } else {
                        temps[code.result.value] = temps[code.arg1.value] * code.arg2.value;
                    }
                } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        temps[code.result.value] = tape[ptr + code.arg1.value] * temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        temps[code.result.value] = tape[ptr + code.arg1.value] * tape[ptr + code.arg2.value];
                    } else {
                        temps[code.result.value] = tape[ptr + code.arg1.value] * code.arg2.value;
                    }
                } else {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        temps[code.result.value] = code.arg1.value * temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        temps[code.result.value] = code.arg1.value * tape[ptr + code.arg2.value];
                    } else {
                        temps[code.result.value] = code.arg1.value * code.arg2.value;
                    }
                }
            } else if (code.result.type == ArgType::PTR_OFFSET) {
                if (code.arg1.type == ArgType::BB_TEMP) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        tape[ptr + code.result.value] = temps[code.arg1.value] * temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        tape[ptr + code.result.value] = temps[code.arg1.value] * tape[ptr + code.arg2.value];
                    } else {
                        tape[ptr + code.result.value] = temps[code.arg1.value] * code.arg2.value;
                    }
                } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        tape[ptr + code.result.value] = tape[ptr + code.arg1.value] * temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        tape[ptr + code.result.value] = tape[ptr + code.arg1.value] * tape[ptr + code.arg2.value];
                    } else {
                        tape[ptr + code.result.value] = tape[ptr + code.arg1.value] * code.arg2.value;
                    }
                } else {
                    if (code.arg2.type == ArgType::BB_TEMP) {
                        tape[ptr + code.result.value] = code.arg1.value * temps[code.arg2.value];
                    } else if (code.arg2.type == ArgType::PTR_OFFSET) {
                        tape[ptr + code.result.value] = code.arg1.value * tape[ptr + code.arg2.value];
                    } else {
                        tape[ptr + code.result.value] = code.arg1.value * code.arg2.value;
                    }
                }
            }
            break;
        case OpType::NEG:
            if (code.result.type == ArgType::BB_TEMP) {
                if (code.arg1.type == ArgType::BB_TEMP) {
                    temps[code.result.value] = -temps[code.arg1.value];
                } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                    temps[code.result.value] = -tape[ptr + code.arg1.value];
                } else {
                    temps[code.result.value] = -code.arg1.value;
                }
            } else if (code.result.type == ArgType::PTR_OFFSET) {
                if (code.arg1.type == ArgType::BB_TEMP) {
                    tape[ptr + code.result.value] = -temps[code.arg1.value];
                } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                    tape[ptr + code.result.value] = -tape[ptr + code.arg1.value];
                } else {
                    tape[ptr + code.result.value] = -code.arg1.value;
                }
            }
            break;
        case OpType::ASSIGN:
            if (code.result.type == ArgType::BB_TEMP) {
                if (code.arg1.type == ArgType::BB_TEMP) {
                    temps[code.result.value] = temps[code.arg1.value];
                } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                    temps[code.result.value] = tape[ptr + code.arg1.value];
                } else {
                    temps[code.result.value] = code.arg1.value;
                }
            } else if (code.result.type == ArgType::PTR_OFFSET) {
                if (code.arg1.type == ArgType::BB_TEMP) {
                    tape[ptr + code.result.value] = temps[code.arg1.value];
                } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                    tape[ptr + code.result.value] = tape[ptr + code.arg1.value];
                } else {
                    tape[ptr + code.result.value] = code.arg1.value;
                }
            }
            break;
        case OpType::PUTCHAR:
            if (code.arg1.type == ArgType::BB_TEMP) {
                putchar(temps[code.arg1.value]);
            } else if (code.arg1.type == ArgType::PTR_OFFSET) {
                putchar(tape[ptr + code.arg1.value]);
            } else {
                putchar(code.arg1.value);
            }
        case OpType::GETCHAR:
            if (code.result.type == ArgType::BB_TEMP) {
                temps[code.result.value] = getchar();
            } else if (code.result.type == ArgType::PTR_OFFSET) {
                tape[ptr + code.result.value] = getchar();
            }
        default: ;
    }
    return ptr;
}

std::string Interpreter::checkIndex(int32_t tapeSize, int32_t ptr, const Quadruple& code, const void* block, const std::vector<Quadruple>& codes) {
    if (code.result.type == ArgType::PTR_OFFSET && (ptr + code.result.value >= tapeSize || ptr + code.result.value < 0)) [[unlikely]] {
        return std::format("index out of range. tapeSize={}, ptr={}{:+d}, block={}, pc={}", tapeSize, ptr, code.result.value, block, &code - &codes.front());
    }
    if (code.arg1.type == ArgType::PTR_OFFSET && (ptr + code.arg1.value >= tapeSize || ptr + code.arg1.value < 0)) [[unlikely]] {
        return std::format("index out of range. tapeSize={}, ptr={}{:+d}, block={}, pc={}", tapeSize, ptr, code.arg1.value, block, &code - &codes.front());
    }
    if (code.arg2.type == ArgType::PTR_OFFSET && (ptr + code.arg2.value >= tapeSize || ptr + code.arg2.value < 0)) [[unlikely]] {
        return std::format("index out of range. tapeSize={}, ptr={}{:+d}, block={}, pc={}", tapeSize, ptr, code.arg2.value, block, &code - &codes.front());
    }
    return {};
}

#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_INTERPRETER_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_INTERPRETER_H

#include <unordered_map>
#include <vector>
#include <expected>

#include "../error.h"
#include "../optimizer/quadruple.h"

// 孩子们这是个设计失误，聪明的你一定发现了，这其实是个虚表
struct GettersForInterpret {
    const std::vector<Quadruple>& (*getCode)(const void*);
    const void* (*getSuccY)(const void*);
    const void* (*getSuccN)(const void*);
};

class Interpreter {
public:
    [[nodiscard]] static std::expected<void, Error> interpret(const void* block, const GettersForInterpret& getters, int32_t tapeSize, int32_t ptr);

private:
    [[nodiscard]] static const void* getNextBlock(
        const uint8_t* tape,
        std::unordered_map<int32_t, uint8_t>& temps,
        const void* block,
        int32_t ptr,
        const std::vector<Quadruple>& codes,
        const void* (*succYGetter)(const void*),
        const void* (*succNGetter)(const void*)
    );
    [[nodiscard]] static int32_t executeCode(uint8_t* tape, std::unordered_map<int32_t, uint8_t>& temps, int32_t ptr, const Quadruple& code);
    [[nodiscard]] static bool checkIndex(int32_t tapeSize, int32_t ptr, const Quadruple& code, const void* block, const std::vector<Quadruple>& codes, std::string& msgOut);
};


#endif //YETANOTHERNEOBRAINFUCKCOMPILER_INTERPRETER_H

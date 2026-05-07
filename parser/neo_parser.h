#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_NEO_PARSER_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_NEO_PARSER_H

#include <vector>

#include "../optimizer/quadruple.h"

class NeoParser {
public:
    static void toC(int32_t tapeSize, std::istream& input, std::ostream& output);
    static std::vector<Quadruple> parse(std::istream& input);
};

#endif //YETANOTHERNEOBRAINFUCKCOMPILER_NEO_PARSER_H

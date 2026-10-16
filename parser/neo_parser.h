#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_NEO_PARSER_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_NEO_PARSER_H

#include <vector>
#include <expected>
#include "../optimizer/quadruple.h"
#include "../error.h"

void parseToC(int32_t tapeSize, std::istream& input, std::ostream& output);
std::expected<std::vector<Quadruple>, Error> parseToQuads(std::istream& input);

#endif //YETANOTHERNEOBRAINFUCKCOMPILER_NEO_PARSER_H

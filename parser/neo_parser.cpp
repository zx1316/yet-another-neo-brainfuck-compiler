#include "neo_parser.h"

#include <iostream>
#include <string>

using OpType = Quadruple::OpType;
using ArgType = QuadrupleArg::ArgType;

void parseToC(int32_t tapeSize, std::istream& input, std::ostream& output) {
    int32_t state = 0;
    int32_t acc = 0;
    output << "#include <stdio.h>\nunsigned char tape[" << tapeSize << "];\nint main() {\nunsigned char* p = tape;\n";
    while (true) {
        int32_t c = input.peek();   // 偷看
        switch (state) {
            case 0:
                input.get();    // 消费
                switch (c) {
                    case '+': acc = 1; state = 1; break;
                    case '-': acc = -1; state = 1; break;
                    case '>': acc = 1; state = 2; break;
                    case '<': acc = -1; state = 2; break;
                    case '.':
                        output << "putchar(*p);\n";
                        break;
                    case ',':
                        output << "*p = getchar();\n";
                        break;
                    case '[':
                        output << "while (*p) {\n";
                        break;
                    case ']': {
                        output << "}\n";
                        break;
                    }
                    case -1:
                        output << "return 0;\n}\n";
                        return;
                    default:
                        break;
                }
                break;
            case 1:
                if (c == '+') {
                    input.get();
                    acc++;
                } else if (c == '-') {
                    input.get();
                    acc--;
                } else {
                    if (acc != 0) {
                        output << "*p = *p + " << acc << ";\n";
                        acc = 0;
                    }
                    state = 0;
                }
                break;
            case 2:
                if (c == '>') {
                    input.get();
                    acc++;
                } else if (c == '<') {
                    input.get();
                    acc--;
                } else {
                    if (acc != 0) {
                        output << "p = p + " << acc << ";\n";
                        acc = 0;
                    }
                    state = 0;
                }
                break;
            default: __builtin_unreachable();
        }
    }
}

std::expected<std::vector<Quadruple>, Error> parseToQuads(std::istream& input) {
    int32_t pos = 0;    // 对非单字节字符集不一定对
    int32_t state = 0;
    int32_t acc = 0;
    std::vector<int32_t> stack;
    std::vector<Quadruple> codes;
    while (true) {
        int32_t c = input.peek();   // 偷看
        switch (state) {
            case 0:
                input.get();    // 消费
                pos++;
                switch (c) {
                    case '+': acc = 1; state = 1; break;
                    case '-': acc = -1; state = 1; break;
                    case '>': acc = 1; state = 2; break;
                    case '<': acc = -1; state = 2; break;
                    case '.':
                        codes.emplace_back(OpType::PUTCHAR, QuadrupleArg{}, QuadrupleArg{ArgType::PTR_OFFSET});
                        break;
                    case ',':
                        codes.emplace_back(OpType::GETCHAR, QuadrupleArg{ArgType::PTR_OFFSET});
                        break;
                    case '[':
                        codes.emplace_back(
                            OpType::JZ,
                            QuadrupleArg{},
                            QuadrupleArg{ArgType::PTR_OFFSET},
                            QuadrupleArg{ArgType::CONSTANT}
                        );  // dest val unknown
                        stack.push_back(codes.size() - 1);    // push backpatch index
                        break;
                    case ']': {
                        if (stack.empty()) {
                            return std::unexpected(Error{Error::PARSE_ERR, std::format("{}: no matching '['", pos)});
                        }
                        int32_t index = stack.back();
                        stack.pop_back();
                        codes.emplace_back(
                            OpType::JNZ,
                            QuadrupleArg{},
                            QuadrupleArg{ArgType::PTR_OFFSET},
                            QuadrupleArg{ArgType::CONSTANT, index + 1}
                        );
                        codes[index].arg2.value = codes.size();
                        break;
                    }
                    case -1:
                        if (!stack.empty()) {
                            return std::unexpected(Error{Error::PARSE_ERR, std::format("{}: missing ']'", pos)});
                        }
                        codes.emplace_back(OpType::HALT);
                        return codes;
                    default:
                        break;
                }
                break;
            case 1:
                if (c == '+') {
                    input.get();
                    pos++;
                    acc++;
                } else if (c == '-') {
                    input.get();
                    pos++;
                    acc--;
                } else {
                    acc = static_cast<int8_t>(acc);
                    if (acc != 0) {
                        codes.emplace_back(
                            OpType::ADD,
                            QuadrupleArg{ArgType::PTR_OFFSET},
                            QuadrupleArg{ArgType::PTR_OFFSET},
                            QuadrupleArg{ArgType::CONSTANT, acc}
                        );
                        acc = 0;
                    }
                    state = 0;
                }
                break;
            case 2:
                if (c == '>') {
                    input.get();
                    pos++;
                    acc++;
                } else if (c == '<') {
                    input.get();
                    pos++;
                    acc--;
                } else {
                    if (acc != 0) {
                        codes.emplace_back(OpType::PTR_ADD, QuadrupleArg{}, QuadrupleArg{ArgType::CONSTANT, acc});
                        acc = 0;
                    }
                    state = 0;
                }
                break;
            default: __builtin_unreachable();
        }
        if (pos < 0) {
            return std::unexpected(Error{Error::PARSE_ERR, "file too large"});
        }
    }
}

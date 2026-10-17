#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_ERROR_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_ERROR_H

#include <string>

/**
 * 错误信息结构
 * 错误处理协议：
 * 1. 不使用 c++ 异常，也不要中途调用 exit 等函数
 * 2. 如果函数正常路径有返回，返回类型用 std::expected<T, Error>
 * 3. 如果函数正常路径无返回，返回类型用 std::expected<void, Error>
 */
struct Error {
    enum EC {
        INPUT_FILE_ERR = 1,
        OUTPUT_FILE_ERR,
        PARSE_ERR,
        INTERPRET_ERR,
        VERIFY_ERR,
        ARG_ERR = 10000
    };

    int code;
    std::string msg;
};


#endif //YETANOTHERNEOBRAINFUCKCOMPILER_ERROR_H

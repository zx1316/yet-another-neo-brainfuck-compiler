#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_OPTION_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_OPTION_H
#include <span>
#include <string>
#include <unordered_map>


struct Option {
    const std::string name;
    const std::string longName;
    const std::string description;
    const std::string argValName;
    void (* const validator)(std::string_view) = nullptr;
};

struct ParsedArgs {
    std::unordered_map<int32_t, std::string_view> optArgs;
    std::vector<std::string_view> posArgs;
};

void printHelp(const char* intro, std::span<const Option> options);

// 返回的 string_view.data() 一定以 '\0' 结尾，可以安全地传入 C API
ParsedArgs parseOptions(int argc, char* argv[], std::span<const Option> options);

#endif //YETANOTHERNEOBRAINFUCKCOMPILER_OPTION_H

#include "option.h"

#include <iostream>

void printHelp(const char* intro, std::span<const Option> options) {
    constexpr auto MAX_OPT_LEN = 23;
    std::cout << intro;

    for (auto& option : options) {
        std::cout << "\n  ";
        int32_t len = 0;

        if (!option.name.empty()) {
            std::cout << option.name;
            len += option.name.size();
            if (!option.argValName.empty()) {
                std::cout << "=<" << option.argValName << '>';
                len += option.argValName.size() + 3;
            }
        }

        if (!option.longName.empty()) {
            if (!option.name.empty()) {
                std::cout << ", ";
                len += 2;
            }
            std::cout << option.longName;
            len += option.longName.size();
            if (!option.argValName.empty()) {
                std::cout << "=<" << option.argValName << '>';
                len += option.argValName.size() + 3;
            }
        }

        if (!option.description.empty()) {
            if (len <= MAX_OPT_LEN) {
                for (int32_t i = 0, size = MAX_OPT_LEN - len + 1; i < size; i++) {
                    std::cout << ' ';
                }
            } else {
                std::cout << '\n';
                for (int32_t i = 0; i < MAX_OPT_LEN + 3; i++) {
                    std::cout << ' ';
                }
            }
            std::cout << option.description;
        }
    }
}

std::expected<ParsedArgs, Error> parseOptions(int argc, char* argv[], std::span<const Option> options) {
    ParsedArgs parsedArgs;
    int32_t waitingOpt = -1;
    bool optEnd = false;

    // 从 1 开始遍历 args
    for (int i = 1; i < argc; ++i) {
        auto* arg = argv[i];
        if (optEnd) {
            // -- 之后的位置参数
            parsedArgs.posArgs.emplace_back(arg);
        } else {
            if (arg[0] == '-') {
                // - 开头的视为可选参数
                if (waitingOpt >= 0) {
                    // 如果已经有一个在等待参数值的可选参数，报错（如果参数值以 - 开头，应该用 -arg1=-xxx）
                    return std::unexpected(Error{Error::PARSE_ERR + i, std::format("missing argument for '{}'", argv[i - 1])});
                }
                if (arg[1] == '-' && arg[2] == '\0') {
                    // --，此后所有参数视为位置参数
                    optEnd = true;
                } else if (auto* equPos = strchr(arg, '='); equPos == nullptr) {
                    // 没有 =，比较全字符串确定参数
                    auto it = std::ranges::find_if(options, [arg](const Option& opt) {
                        return opt.name == arg || opt.longName == arg;
                    });
                    if (it != options.end()) {
                        if (it->argValName.empty()) {
                            parsedArgs.optArgs[it - options.begin()] = {};
                        } else {
                            // 如果这个参数需要参数值，等待
                            waitingOpt = it - options.begin();
                        }
                    }
                } else {
                    // 有 =，比较 = 之前的字符串确定参数
                    auto it = std::ranges::find_if(options, [arg, equPos](const Option& opt) {
                        return opt.name == std::string_view(arg, equPos) || opt.longName == std::string_view(arg, equPos);
                    });
                    if (it != options.end()) {
                        if (it->argValName.empty()) {
                            parsedArgs.optArgs[it - options.begin()] = {};
                        } else {
                            if (equPos[1] == '\0') {
                                // 如果这个参数需要参数值，等号后却没有，报错
                                return std::unexpected(Error{Error::PARSE_ERR + i, std::format("missing argument: '{}'", arg)});
                            }
                            std::string_view sv(equPos + 1);
                            if (it->validator != nullptr) {
                                if (auto err = it->validator(sv); !err.empty()) {
                                    return std::unexpected(Error{Error::PARSE_ERR + i, std::move(err)});
                                }
                            }
                            parsedArgs.optArgs[it - options.begin()] = sv;
                        }
                    }
                }
            } else {
                if (waitingOpt >= 0) {
                    // 如果有在等待的，记录参数值
                    std::string_view sv(arg);
                    if (options[waitingOpt].validator != nullptr) {
                        if (auto err = options[waitingOpt].validator(sv); !err.empty()) {
                            return std::unexpected(Error{Error::ARG_ERR + i, std::move(err)});
                        }
                    }
                    parsedArgs.optArgs[waitingOpt] = sv;
                    waitingOpt = -1;
                } else {
                    // 位置参数
                    parsedArgs.posArgs.emplace_back(arg);
                }
            }
        }
    }

    return parsedArgs;
}

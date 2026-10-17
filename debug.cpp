#include "debug.h"

#include <iostream>

namespace dbg {

namespace {
    Options g_options;
}

void configure(const Options& options) {
    g_options = options;
}

const Options& options() {
    return g_options;
}

bool wantAsm() {
    return g_options.asmDump;
}

bool wantVerify() {
    return g_options.verify;
}

std::ostream& out() {
    return std::cerr;
}

bool wantStage(std::string_view stage) {
    if (stage == "parse") {
        return g_options.parse;
    }
    if (stage == "final") {
        return g_options.final;
    }
    if (stage.starts_with("stage:")) {
        return g_options.stages;
    }
    return false;
}

std::string applyStageList(Options& options, std::string_view list) {
    if (list.empty()) {
        return "--dump-ir 需要一个值：parse,final,stages,all";
    }
    std::size_t begin = 0;
    while (true) {
        auto comma = list.find(',', begin);
        auto item = list.substr(begin, comma == std::string_view::npos ? std::string_view::npos : comma - begin);
        while (!item.empty() && item.front() == ' ') {
            item.remove_prefix(1);
        }
        while (!item.empty() && item.back() == ' ') {
            item.remove_suffix(1);
        }
        if (!item.empty()) {
            if (item == "all") {
                options.parse = options.final = options.stages = true;
            } else if (item == "parse") {
                options.parse = true;
            } else if (item == "final") {
                options.final = true;
            } else if (item == "stages") {
                options.stages = true;
            } else {
                return "未知的 --dump-ir 阶段 '" + std::string(item)
                       + "'；可选 parse,final,stages,all";
            }
        }
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }
    return {};
}

}  // namespace dbg

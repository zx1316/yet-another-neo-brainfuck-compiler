#include <filesystem>
#include <iostream>
#include <fstream>
#include <array>

#include "interpreter/interpreter.h"
#include "generator/windows_pe_plus_generator.h"
#include "optimizer/code_optimizer.h"
#include "parser/neo_parser.h"
#include "option.h"
#include "error.h"

namespace {
    enum OutputFormat {
        OUT_PE, OUT_ELF, OUT_C
    };
}

// TODO: 未来可以考虑 elf
#ifdef HOST_OS_WIN
#define DEFAULT_OUT_FORMAT OUT_PE
#define OUT_FORMAT_OPTION "pe (default), c"
#else
#define DEFAULT_OUT_FORMAT OUT_PE
#define OUT_FORMAT_OPTION "pe (default), c"
#endif

#define STRINGIFY(x) #x
#define STR(x) STRINGIFY(x)

#define DEFAULT_MEM_SIZE_STR STR(DEFAULT_MEM_SIZE)

static constexpr auto HELP_INTRO = "OVERVIEW: Brainfuck compiler\n\n"
        "USAGE: bf-compiler [options] file...\n\n"
        "OPTIONS:";

static constexpr auto VER_STR = "Yet Another Neo Brainfuck Compiler version 1.0.0\nTarget: x86_64-pc-windows\n";

static constexpr auto OPTIONS = std::array{
    Option{
        .name = "-falign-loops",
        .description = "N must be 0 or 16. Align loops to the boundary (default 16)",
        .argValName = "N",
        .validator = [](std::string_view arg) {
            if (arg != "0" && arg != "16") {
                return std::format("invalid alignment in '-falign-loops {}'", arg);
            }
            return std::string();
        }
    },
    Option{
        .name = "-fclj",
        .description = "CMP-LEA-JNZ (experimental, current default is ADD-CMP-JNZ)"
    },
    Option{
        .name = "-help",
        .longName = "--help",
        .description = "Display available options"
    },
    Option{
        .name = "-mmem-size",
        .description = "Set the runtime memory size in bytes (1~2147483647, default " DEFAULT_MEM_SIZE_STR "). This does not include the guard space required by aggressive optimization. When optimization is enabled and the pointer position is globally determinable, this option is automatically ignored.",
        .argValName = "value",
        .validator = [](std::string_view arg) {
            int32_t memSize = 0;
            auto* last = arg.data() + arg.size();
            if (auto [ptr, ec] = std::from_chars(arg.data(), last, memSize); ec != std::errc{} || ptr != last || memSize <= 0) {
                return std::format("invalid size in '-mmem-size {}'", arg);
            }
            return std::string();
        }
    },
    Option{
        .name = "-O0",
        .description = "Disable optimization"
    },
    Option{
        .name = "-O1",
        .description = "Enable aggressive optimization (default)"
    },
    Option{
        .name = "-Ostrict",
        .description = "Enable strict, conservative optimization and no additional guard space is required"
    },
    Option{
        .name = "-o",
        .description = "Write output to <file>",
        .argValName = "file"
    },
    Option{
        .longName = "--out-format",
        .description = "Set output format (option: " OUT_FORMAT_OPTION ")",
        .argValName = "value",
        .validator = [](std::string_view arg) {
            if (arg != "pe" && arg != "c") {
                return std::format("unknown format in '--out-format {}'", arg);
            }
            return std::string();
        }
    },
    Option{
        .longName = "--print-info",
        .description = "Print optimization and generation information during compilation"
    },
    Option{
        .name = "-v",
        .longName ="--version",
        .description = "Print version information"
    }
};

namespace {
    enum OptionOrdinal {
        OPT_ALIGN_LOOPS,
        OPT_CLJ,
        OPT_HELP,
        OPT_MEM_SIZE,
        OPT_O0,
        OPT_O1,
        OPT_O_STRICT,
        OPT_OUT,
        OPT_OUT_FORMAT,
        OPT_PRINT_INFO,
        OPT_VERSION,
    };
}

static int32_t getMemSize(const std::unordered_map<int32_t, std::string_view>& opts) {
    if (auto memSizeIt = opts.find(OPT_MEM_SIZE); memSizeIt != opts.end()) {
        return strtol(memSizeIt->second.data(), nullptr, 10);
    }
    return DEFAULT_MEM_SIZE;
}

static OptimizationLevel getOptimizationLevel(const std::unordered_map<int32_t, std::string_view>& opts) {
    if (opts.contains(OPT_O1)) {
        return OptimizationLevel::O1;
    }
    if (opts.contains(OPT_O_STRICT)) {
        return OptimizationLevel::O_STRICT;
    }
    return OptimizationLevel::O0;
}

static OutputFormat getOutputFormat(const std::unordered_map<int32_t, std::string_view>& opts) {
    if (auto it = opts.find(OPT_OUT_FORMAT); it != opts.end()) {
        if (it->second == "pe") {
            return OUT_PE;
        }
        if (it->second == "elf") {
            return OUT_ELF;
        }
        return OUT_C;
    }
    return DEFAULT_OUT_FORMAT;
}

static std::expected<void, Error> compileAndSave(std::string_view outPathSv, OutputFormat outFormat, std::ifstream& input, int32_t memSize, OptimizationLevel level, bool printInfo) {
    auto outPath = std::filesystem::path(outPathSv);
    if (outFormat == OUT_C) {
        std::ofstream output(outPath, std::ios::binary);
        if (!output) {
            return std::unexpected(Error{Error::OUTPUT_FILE_ERR, "cannot open output file"});
        }
        parseToC(memSize, input, output);
        return {};
    }

    auto parsedResult = parseToQuads(input);
    if (!parsedResult) {
        return std::unexpected(std::move(parsedResult.error()));
    }
    CodeOptimizer optimizer(*parsedResult, memSize);
    optimizer.optimize(level, printInfo);
    WindowsPEPlusGenerator generator(optimizer.getBasicBlocks(), optimizer.getTapeSize(), optimizer.getInitOffset());
    std::ofstream output(outPath, std::ios::binary);
    if (!output) {
        return std::unexpected(Error{Error::OUTPUT_FILE_ERR, "cannot open output file"});
    }
    generator.generate(level, true, printInfo, output);
    return {};
}

static std::expected<void, Error> compileAndExec(std::ifstream& input, int32_t memSize, OptimizationLevel level, bool printInfo) {
    auto parsedResult = parseToQuads(input);
    if (!parsedResult) {
        return std::unexpected(std::move(parsedResult.error()));
    }

    CodeOptimizer optimizer(*parsedResult, memSize);
    optimizer.optimize(level, printInfo);
    WindowsPEPlusGenerator generator(optimizer.getBasicBlocks(), optimizer.getTapeSize(), optimizer.getInitOffset());
    if (level != OptimizationLevel::O0) {
        generator.rearrangeIRs(printInfo);
    }
    std::cout << "interpreting:\n";
    return Interpreter::interpret(
        generator.getBasicBlocks().front().get(),
        X64BasicBlock::getGettersForInterpreter(),
        optimizer.getTapeSize(),
        optimizer.getInitOffset()
    );
}

static std::expected<void, Error> mainCompile(int argc, char* argv[]) {
    auto parsedArgs = parseOptions(argc, argv, OPTIONS);
    if (!parsedArgs) {
        return std::unexpected(std::move(parsedArgs.error()));
    }
    auto &[optArgs, posArgs] = *parsedArgs;
    if (posArgs.empty()) {
        if (optArgs.contains(OPT_VERSION)) {
            std::cout << VER_STR;
            return {};
        }
        if (optArgs.contains(OPT_HELP)) {
            printHelp(HELP_INTRO, OPTIONS);
            return {};
        }
        return std::unexpected(Error{Error::INPUT_FILE_ERR, "no input file"});
    }

    std::ifstream input(std::filesystem::path(posArgs[0]), std::ios::binary);
    if (!input) {
        return std::unexpected(Error{Error::INPUT_FILE_ERR, "cannot open input file"});
    }
    auto memSize = getMemSize(optArgs);
    auto optimizeLevel = getOptimizationLevel(optArgs);
    bool printInfo = optArgs.contains(OPT_PRINT_INFO);
    if (auto outFilePathIt = optArgs.find(OPT_OUT); outFilePathIt != optArgs.end()) {
        return compileAndSave(outFilePathIt->second, getOutputFormat(optArgs), input, memSize, optimizeLevel, printInfo);
    }
    return compileAndExec(input, memSize, optimizeLevel, printInfo);
}

// windows 也用 main，char* 本地编码应该是 ok 的，filesystem::path 会自动处理
int main(int argc, char* argv[]) {
    auto noErr = mainCompile(argc, argv);
    if (noErr) {
        return 0;
    }
    std::cerr << "error: " << noErr.error().msg << '\n';
    return noErr.error().code;
}

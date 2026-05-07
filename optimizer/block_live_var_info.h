#ifndef YETANOTHERNEOBRAINFUCKCOMPILER_BLOCK_LIVE_VAR_INFO_H
#define YETANOTHERNEOBRAINFUCKCOMPILER_BLOCK_LIVE_VAR_INFO_H

#include <cstdint>
#include <unordered_set>


struct BlockLiveVarInfo {
    std::unordered_set<int32_t> in;
    std::unordered_set<int32_t> out;
    std::unordered_set<int32_t> use;
    std::unordered_set<int32_t> def;
};



#endif //YETANOTHERNEOBRAINFUCKCOMPILER_BLOCK_LIVE_VAR_INFO_H

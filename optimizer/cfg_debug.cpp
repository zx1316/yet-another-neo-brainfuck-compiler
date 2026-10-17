#include "cfg_debug.h"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "mid_basic_block.h"
#include "quadruple.h"
#include "../debug.h"

using OpType = Quadruple::OpType;

namespace {

    using BlockIdMap = std::unordered_map<const MidBasicBlock*, std::int32_t>;

    BlockIdMap buildIdMap(const std::vector<std::unique_ptr<MidBasicBlock>>& blocks) {
        BlockIdMap ids;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            ids[blocks[i].get()] = static_cast<std::int32_t>(i);
        }
        return ids;
    }

    std::string idOf(const MidBasicBlock* block, const BlockIdMap& ids) {
        if (block == nullptr) {
            return "-";
        }
        auto it = ids.find(block);
        if (it == ids.end()) {
            return "?";
        }
        return "B" + std::to_string(it->second);
    }

    bool endsWithCondJmp(const MidBasicBlock& block) {
        if (block.irs.empty()) {
            return false;
        }
        const auto type = block.irs.back().type;
        return type == OpType::JZ || type == OpType::JNZ;
    }

    // 条件跳转的 arg2 是解析阶段留下的 IR 下标，划分基本块之后已经无意义
    // （真正的后继由 succY/succN 表示），打出来只会误导，所以在这里省掉。
    std::string formatInst(const Quadruple& inst) {
        if (inst.type == OpType::JZ || inst.type == OpType::JNZ || inst.type == OpType::JMP) {
            std::ostringstream os;
            os << Quadruple::CODE_TYPE_TRANSLATE[static_cast<int32_t>(inst.type)];
            if (inst.type == OpType::JMP) {
                os << "\t--";
            } else {
                os << '\t' << inst.arg1;
            }
            return os.str();
        }
        std::ostringstream os;
        os << inst;
        return os.str();
    }

}  // namespace

void dumpCFG(const std::string_view title,
             const std::vector<std::unique_ptr<MidBasicBlock>>& blocks,
             const bool withInsts) {
    const auto ids = buildIdMap(blocks);
    std::int64_t totalInsts = 0;
    for (const auto& block : blocks) {
        totalInsts += static_cast<std::int64_t>(block->irs.size());
    }

    auto& os = dbg::out();
    os << "===== IR[" << title << "] blocks=" << blocks.size() << " insts=" << totalInsts << " =====\n";
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const auto& block = *blocks[i];
        os << 'B' << i << "  preds=[";
        for (std::size_t k = 0; k < block.preds.size(); ++k) {
            if (k != 0) {
                os << ", ";
            }
            os << idOf(block.preds[k], ids);
        }
        os << "]  succN=" << idOf(block.succN, ids)
           << "  succY=" << idOf(block.succY, ids)
           << "  ptrDelta=" << block.ptrDelta
           << "  ptrIn=" << block.ptrIn
           << "  ptrOut=" << block.ptrOut;
        if (endsWithCondJmp(block)) {
            os << "  cond=" << (block.irs.back().type == OpType::JZ ? "JZ" : "JNZ");
        }
        os << '\n';
        if (!withInsts) {
            continue;
        }
        for (std::size_t k = 0; k < block.irs.size(); ++k) {
            os << "    [" << std::setw(4) << k << "] " << formatInst(block.irs[k]) << '\n';
        }
    }
    os.flush();
}

std::int32_t verifyCFG(const std::string_view stage, const std::vector<std::unique_ptr<MidBasicBlock>>& blocks) {
    const auto ids = buildIdMap(blocks);
    std::int32_t errors = 0;

    auto fail = [&](const std::size_t blockId, const std::string& message) {
        dbg::out() << "verify[" << stage << "] B" << blockId << ": " << message << '\n';
        ++errors;
    };

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const auto& block = *blocks[i];
        const bool cond = endsWithCondJmp(block);

        // 1. 后继必须在本图内
        if (block.succN != nullptr && !ids.contains(block.succN)) {
            fail(i, "succN 指向图外的块");
        }
        if (block.succY != nullptr && !ids.contains(block.succY)) {
            fail(i, "succY 指向图外的块");
        }

        // 2. 末尾跳转形态与 succY 一致
        if (cond && block.succY == nullptr) {
            fail(i, "末尾是条件跳转但 succY 为空");
        }
        if (!cond && block.succY != nullptr) {
            fail(i, "末尾不是条件跳转但 succY 非空");
        }

        // 3. 前驱/后继双向一致
        for (const auto* succ : {block.succN, block.succY}) {
            if (succ == nullptr || !ids.contains(succ)) {
                continue;
            }
            if (std::ranges::find(succ->preds, &block) == succ->preds.end()) {
                fail(i, "后继 " + idOf(succ, ids) + " 的 preds 里没有本块");
            }
        }
        for (const auto* pred : block.preds) {
            if (pred == nullptr) {
                fail(i, "preds 里有空指针");
                continue;
            }
            if (!ids.contains(pred)) {
                fail(i, "preds 里有图外的块");
                continue;
            }
            if (pred->succN != &block && pred->succY != &block) {
                fail(i, "前驱 " + idOf(pred, ids) + " 的 succ 里没有本块");
            }
        }

        // 4. 指针状态与指针增量一致（只在两侧都确定时检查）
        if (!block.ptrIn.isUnknown && !block.ptrIn.isUnreachable
            && !block.ptrOut.isUnknown && !block.ptrOut.isUnreachable) {
            if (block.ptrOut.pos != block.ptrIn.pos + block.ptrDelta) {
                fail(i, "ptrOut(" + std::to_string(block.ptrOut.pos) + ") != ptrIn("
                        + std::to_string(block.ptrIn.pos) + ") + ptrDelta("
                        + std::to_string(block.ptrDelta) + ")");
            }
        }
    }

    if (errors == 0) {
        dbg::out() << "verify[" << stage << "] OK (blocks=" << blocks.size() << ")\n";
    } else {
        dbg::out() << "verify[" << stage << "] " << errors << " 个问题\n";
    }
    dbg::out().flush();
    return errors;
}

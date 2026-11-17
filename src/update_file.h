#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "diff.h"
#include "lang_file.h"

namespace mcd {

// 用户从报告粘贴进来的一条新翻译条目。
struct PastedEntry {
    std::string key;    // 解码后的键
    std::string raw;    // JSON："key": "value"；LANG：key=value
    uint32_t line = 1;  // 在粘贴文本中的行号
};

struct PastedBlock {
    std::vector<PastedEntry> entries;
};

struct UpdateStats {
    size_t inserted = 0;   // 目标文件中原本不存在的必需键
    size_t updated = 0;    // 目标文件中已存在、被替换并重新定位的必需键
    size_t removed = 0;    // 目标文件中实际删除的旧键
    size_t untouched = 0;  // 保留下来的其他条目
    size_t totalAfter = 0; // 更新后的条目总数
};

// 解析粘贴块。format 决定语法；允许空块、JSON 外层花括号与末尾逗号。
// 失败时 err 为 UTF-8 错误文本（含行号）。
bool ParsePastedBlock(std::string_view text, FileFormat format, PastedBlock& out, std::string& err);

// 校验粘贴内容是否覆盖全部新增键与值变更键，并按新文件顺序更新目标文件：
// - 新增/值变更条目插入到新文件中对应键的位置；
// - 删除旧 en_us 中已移除的键；
// - 保留目标文件其余条目、注释与换行风格；
// - 写回时不带 BOM，先写临时文件校验后再原子替换。
bool ApplyTranslationUpdate(const std::wstring& targetPath, const DiffResult& diff,
                            const PastedBlock& pasted, UpdateStats& stats, std::string& err);

// 本次需要粘贴的键（新增 + 值变更），按新文件出现顺序。
std::vector<std::string_view> CollectRequiredKeys(const DiffResult& diff);

}  // namespace mcd

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

enum class TargetConsistency {
    Unknown = 0,
    MatchesOld,  // 键集合与旧 en_us 完全一致
    MatchesNew,  // 键集合与新 en_us 完全一致
    Mismatch,    // 与旧/新都不一致
    Error        // 操作文件读取/解析失败
};

struct ConsistencyReport {
    TargetConsistency state = TargetConsistency::Unknown;
    std::vector<std::string> missing;  // 旧 en_us 有、操作文件没有的键（旧文件顺序）
    std::vector<std::string> extra;    // 操作文件有、旧 en_us 没有的键（操作文件顺序）
    std::string error;                 // state == Error 时的错误信息
};

// 比较操作文件与旧 en_us 的键集合（忽略顺序）。
// 读取失败时返回 false，state=Error；成功时返回 true，并给出 missing/extra。
bool CheckTargetConsistency(const std::wstring& targetPath, const DiffResult& diff,
                            ConsistencyReport& report);

// 生成“操作文件相对旧 en_us 缺失的条目”纯净模板（JSON 为条目原文，LANG 为原始行），
// 可直接复制后翻译。模板条目按旧 en_us 顺序排列。
bool BuildMissingEntriesTemplate(const DiffResult& diff, const ConsistencyReport& report,
                                 std::string& text);

// 将操作文件同步到旧 en_us：粘贴缺失键的翻译条目，删除多余键，按旧文件顺序注入。
// 不要求任何值变更条目。失败时原文件不变。
bool ApplyTargetSync(const std::wstring& targetPath, const DiffResult& diff,
                     const PastedBlock& pasted, UpdateStats& stats, std::string& err);

// 解析粘贴块。format 决定语法；允许空块、JSON 外层花括号与末尾逗号。
// 失败时 err 为 UTF-8 错误文本（含行号）。
bool ParsePastedBlock(std::string_view text, FileFormat format, PastedBlock& out, std::string& err);

// 校验粘贴内容是否覆盖全部新增键与值变更键，并按新文件顺序更新目标文件：
// - 更新前要求操作文件的键集合与旧 en_us 完全一致；
// - 新增/值变更条目插入到新文件中对应键的位置；
// - 删除旧 en_us 中已移除的键；
// - 保留目标文件其余条目、注释与换行风格；
// - 写回时不带 BOM，先写临时文件校验后再原子替换。
bool ApplyTranslationUpdate(const std::wstring& targetPath, const DiffResult& diff,
                            const PastedBlock& pasted, UpdateStats& stats, std::string& err);

// 本次需要粘贴的键（新增 + 值变更），按新文件出现顺序。
std::vector<std::string_view> CollectRequiredKeys(const DiffResult& diff);

}  // namespace mcd

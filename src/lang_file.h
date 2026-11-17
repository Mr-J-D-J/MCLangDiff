#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hash.h"

namespace mcd {

// 单个文件的大小上限（512 MB），避免误拖超大文件导致内存耗尽。
constexpr uint64_t kMaxFileBytes = 512ull * 1024ull * 1024ull;

enum class FileFormat { Unknown = 0, Json, Lang };

// 一条键值条目。所有 string_view 都指向 LangFile 内部稳定存储。
struct Entry {
    std::string_view key;     // 解码后的键（JSON 转义已还原）
    std::string_view value;   // 解码后的值（JSON 转义已还原），用于比较
    std::string_view raw;     // 原始条目文本：JSON 为 "key": "value"；LANG 为整行
    std::string_view indent;  // JSON 条目所在行的前导空白；LANG 为空
    std::string_view lead;    // JSON 条目之前的空白（含换行与缩进）；LANG 为空
    uint32_t line = 1;        // 起始行号（从 1 开始）
};

// 解析后的语言文件。内部持有完整文件缓冲，Entry 以视图引用它，
// 因此本类型不可拷贝/移动，应通过 unique_ptr 持有。
struct LangFile {
    FileFormat format = FileFormat::Unknown;
    std::string buffer;
    std::deque<std::string> decoded;  // 存放转义解码后的字符串（引用稳定）
    std::vector<Entry> entries;       // 保持文件中的出现顺序
    std::unordered_map<std::string_view, uint32_t, KeyHash> index;
    std::string_view prefix;  // JSON：文件开头到 '{'（含）；LANG 为空
    std::string_view suffix;  // JSON：最后一个条目结束后的内容；LANG 为空
    uint64_t byteSize = 0;
    std::string path;  // UTF-8

    LangFile() = default;
    LangFile(const LangFile&) = delete;
    LangFile& operator=(const LangFile&) = delete;
    LangFile(LangFile&&) = delete;
    LangFile& operator=(LangFile&&) = delete;
};

FileFormat FormatFromPath(const std::wstring& path);
const char* FormatName(FileFormat format);

// 读取文件；处理空文件、超大文件、读取失败等情况。
bool ReadFileBytes(const std::wstring& path, std::string& out, uint64_t maxBytes, std::string& err);

// 读取并解析语言文件。expected 为 Unknown 时按扩展名判断；否则要求一致。
// 自动去掉 UTF-8 BOM，拒绝 UTF-16/UTF-32、非法 UTF-8、重复键等。
bool LoadLangFile(const std::wstring& path, FileFormat expected, LangFile& out, std::string& err);

// 判断两个路径是否指向同一个文件（解析为最终路径后忽略大小写比较）。
bool IsSameFile(const std::wstring& first, const std::wstring& second);

// 解码 JSON 字符串内部的转义序列（content 不含两侧引号），结果追加到 out。
// 失败时返回 false 并填充 err。
bool DecodeJsonEscapes(std::string_view content, std::string& out, std::string& err);

}  // namespace mcd

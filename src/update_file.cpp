#include "update_file.h"

#include <windows.h>

#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "util.h"

namespace mcd {
namespace {

constexpr size_t kMaxReportedProblems = 20;

bool IsCommentLine(std::string_view trimmed) {
    return !trimmed.empty() && (trimmed.front() == '#' ||
                                (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/'));
}

std::string DetectNewline(std::string_view text) {
    size_t crlf = 0;
    size_t lf = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            if (i > 0 && text[i - 1] == '\r') {
                ++crlf;
            } else {
                ++lf;
            }
        }
    }
    return crlf >= lf ? std::string("\r\n") : std::string("\n");
}

std::string DominantIndent(const LangFile& file) {
    std::map<std::string, int> counts;
    bool found = false;
    for (const Entry& entry : file.entries) {
        const size_t newline = entry.lead.find_last_of('\n');
        if (newline == std::string_view::npos) {
            continue;
        }
        found = true;
        counts[std::string(entry.lead.substr(newline + 1))]++;
    }
    if (!found) {
        return "  ";
    }
    size_t best = 0;
    std::string result;
    for (const auto& [indent, count] : counts) {
        if (static_cast<size_t>(count) > best) {
            best = static_cast<size_t>(count);
            result = indent;
        }
    }
    return result;
}

bool HasMultilineEntries(const LangFile& file) {
    for (const Entry& entry : file.entries) {
        if (entry.lead.find('\n') != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- 粘贴块：JSON

class JsonFragmentParser {
public:
    explicit JsonFragmentParser(std::string_view text) : text_(text) {}

    bool Parse(std::vector<PastedEntry>& out, std::string& err) {
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB &&
            static_cast<unsigned char>(text_[2]) == 0xBF) {
            text_.remove_prefix(3);
        }
        text_ = TrimAscii(text_);
        if (text_.empty()) {
            return true;
        }

        if (text_.front() == '{') {
            if (text_.back() != '}') {
                err = "JSON 粘贴块以 '{' 开头，但缺少结尾的 '}'";
                return false;
            }
            text_ = TrimAscii(text_.substr(1, text_.size() - 2));
            if (text_.empty()) {
                return true;
            }
        }

        while (true) {
            SkipWhitespace();
            if (pos_ >= text_.size()) {
                break;  // 允许末尾逗号
            }
            if (!ParseEntry(out)) {
                err = "JSON 粘贴块第 " + std::to_string(line_) + " 行：" + error_;
                return false;
            }
            SkipWhitespace();
            if (pos_ >= text_.size()) {
                break;
            }
            if (text_[pos_] != ',') {
                err = "JSON 粘贴块第 " + std::to_string(line_) + " 行：条目之间缺少逗号";
                return false;
            }
            ++pos_;
        }

        std::unordered_map<std::string, uint32_t> seen;
        for (const PastedEntry& entry : out) {
            const auto inserted = seen.emplace(entry.key, entry.line);
            if (!inserted.second) {
                err = "JSON 粘贴块第 " + std::to_string(entry.line) + " 行：键 \"" + entry.key +
                      "\" 重复（首次出现于第 " + std::to_string(inserted.first->second) + " 行）";
                return false;
            }
        }
        return true;
    }

private:
    bool Fail(const std::string& message) {
        error_ = message;
        return false;
    }

    void SkipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '\n') {
                ++pos_;
                ++line_;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool ParseEntry(std::vector<PastedEntry>& out) {
        const uint32_t entryLine = line_;
        const size_t rawStart = pos_;
        std::string key;
        if (!ParseString(key)) {
            return false;
        }
        SkipWhitespace();
        if (pos_ >= text_.size() || text_[pos_] != ':') {
            return Fail("键后缺少 ':'");
        }
        ++pos_;
        SkipWhitespace();
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            return Fail("值必须是字符串，格式应为 \"键\": \"值\"");
        }
        std::string value;
        if (!ParseString(value)) {
            return false;
        }
        const size_t rawEnd = pos_;

        PastedEntry entry;
        entry.key = std::move(key);
        entry.raw.assign(text_.substr(rawStart, rawEnd - rawStart));
        entry.line = entryLine;
        out.push_back(std::move(entry));
        return true;
    }

    bool ParseString(std::string& decoded) {
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            return Fail("缺少双引号");
        }
        ++pos_;
        const size_t contentStart = pos_;
        bool hasEscape = false;
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_]);
            if (c == '\\') {
                hasEscape = true;
                pos_ += 2;
                continue;
            }
            if (c == '"') {
                break;
            }
            if (c < 0x20) {
                return Fail("字符串中包含未转义的控制字符");
            }
            ++pos_;
        }
        if (pos_ >= text_.size()) {
            return Fail("字符串缺少结束引号");
        }
        const size_t contentEnd = pos_;
        ++pos_;

        const std::string_view content = text_.substr(contentStart, contentEnd - contentStart);
        if (!hasEscape) {
            decoded.assign(content);
            return true;
        }
        decoded.clear();
        decoded.reserve(content.size());
        std::string decodeError;
        if (!DecodeJsonEscapes(content, decoded, decodeError)) {
            return Fail(decodeError);
        }
        return true;
    }

    std::string_view text_;
    size_t pos_ = 0;
    uint32_t line_ = 1;
    std::string error_;
};

// ---------------------------------------------------------------- 粘贴块：LANG

bool ParseLangFragment(std::string_view text, std::vector<PastedEntry>& out, std::string& err) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }

    std::unordered_map<std::string, uint32_t> seen;
    size_t pos = 0;
    uint32_t lineNumber = 0;
    while (pos < text.size()) {
        const size_t newline = text.find('\n', pos);
        const size_t end = newline == std::string_view::npos ? text.size() : newline;
        std::string_view raw = text.substr(pos, end - pos);
        if (!raw.empty() && raw.back() == '\r') {
            raw.remove_suffix(1);
        }
        ++lineNumber;

        const std::string_view trimmed = TrimAscii(raw);
        if (!trimmed.empty() && !IsCommentLine(trimmed)) {
            const size_t equals = raw.find('=');
            if (equals == std::string_view::npos) {
                err = "LANG 粘贴块第 " + std::to_string(lineNumber) + " 行：缺少 '=' 分隔符";
                return false;
            }
            const std::string_view key = TrimAscii(raw.substr(0, equals));
            if (key.empty()) {
                err = "LANG 粘贴块第 " + std::to_string(lineNumber) + " 行：键为空";
                return false;
            }
            std::string keyText(key);
            const auto inserted = seen.emplace(keyText, lineNumber);
            if (!inserted.second) {
                err = "LANG 粘贴块第 " + std::to_string(lineNumber) + " 行：键 \"" + keyText +
                      "\" 重复（首次出现于第 " + std::to_string(inserted.first->second) + " 行）";
                return false;
            }
            PastedEntry entry;
            entry.key = std::move(keyText);
            entry.raw.assign(trimmed);
            entry.line = lineNumber;
            out.push_back(std::move(entry));
        }

        if (newline == std::string_view::npos) {
            break;
        }
        pos = newline + 1;
    }
    return true;
}

// ---------------------------------------------------------------- 目标文件构建：JSON

struct JsonNode {
    std::string_view lead;
    std::string_view raw;
};

bool BuildJsonContent(const LangFile& target, const LangFile& orderFile,
                      const std::unordered_map<std::string_view, const PastedEntry*>& pasted,
                      const std::unordered_set<std::string_view>& removedKeys,
                      const std::unordered_map<std::string_view, uint32_t>& newIndex,
                      UpdateStats& stats, std::string& content) {
    std::list<JsonNode> nodes;
    std::map<uint32_t, std::list<JsonNode>::iterator> anchors;

    for (const Entry& entry : target.entries) {
        if (pasted.find(entry.key) != pasted.end()) {
            continue;
        }
        if (removedKeys.find(entry.key) != removedKeys.end()) {
            continue;
        }
        nodes.push_back(JsonNode{entry.lead, entry.raw});
        const auto position = newIndex.find(entry.key);
        if (position != newIndex.end()) {
            anchors[position->second] = std::prev(nodes.end());
        }
        ++stats.untouched;
    }

    const bool multiline = HasMultilineEntries(target);
    const std::string newline = DetectNewline(target.buffer);
    const std::string indent = DominantIndent(target);
    std::string synthesizedLead;
    if (multiline) {
        synthesizedLead = newline + indent;
    } else if (!target.entries.empty()) {
        synthesizedLead.assign(target.entries.front().lead);
    } else {
        synthesizedLead = newline + "  ";
    }

    for (uint32_t i = 0; i < orderFile.entries.size(); ++i) {
        const std::string_view key = orderFile.entries[i].key;
        const auto pastedIt = pasted.find(key);
        if (pastedIt == pasted.end()) {
            continue;
        }
        const auto anchor = anchors.lower_bound(i);
        std::list<JsonNode>::iterator position;
        if (anchor == anchors.begin()) {
            position = nodes.begin();
        } else {
            position = std::next(std::prev(anchor)->second);
        }
        const auto inserted = nodes.insert(position, JsonNode{synthesizedLead, pastedIt->second->raw});
        anchors[i] = inserted;
    }

    for (const auto& item : pasted) {
        const std::string_view key = item.first;
        if (target.index.find(key) != target.index.end()) {
            ++stats.updated;
        } else {
            ++stats.inserted;
        }
    }
    for (const std::string_view key : removedKeys) {
        if (target.index.find(key) != target.index.end()) {
            ++stats.removed;
        }
    }
    stats.totalAfter = nodes.size();

    if (!target.entries.empty()) {
        content.clear();
        content.reserve(target.buffer.size() + 1024);
        content.append(target.prefix);
        bool first = true;
        for (const JsonNode& node : nodes) {
            if (!first) {
                content.push_back(',');
            }
            first = false;
            content.append(node.lead);
            content.append(node.raw);
        }
        content.append(target.suffix);
        return true;
    }

    if (nodes.empty()) {
        content.assign(target.buffer);
        return true;
    }

    content.clear();
    content.append(target.prefix);
    bool first = true;
    for (const JsonNode& node : nodes) {
        if (!first) {
            content.push_back(',');
        }
        first = false;
        content.append(newline);
        content.append("  ");
        content.append(node.raw);
    }
    content.append(newline);
    content.push_back('}');
    if (!target.buffer.empty() && target.buffer.back() == '\n') {
        content.append(newline);
    }
    return true;
}

// ---------------------------------------------------------------- 目标文件构建：LANG

struct LangLine {
    std::string text;
    std::string ending;
    bool isEntry = false;
    std::string key;
};

bool BuildLangContent(const LangFile& target, const LangFile& orderFile,
                      const std::unordered_map<std::string_view, const PastedEntry*>& pasted,
                      const std::unordered_set<std::string_view>& removedKeys,
                      const std::unordered_map<std::string_view, uint32_t>& newIndex,
                      UpdateStats& stats, std::string& content, std::string& err) {
    std::list<LangLine> lines;
    const std::string& buffer = target.buffer;
    size_t pos = 0;
    while (pos < buffer.size()) {
        const size_t newline = buffer.find('\n', pos);
        if (newline == std::string::npos) {
            LangLine line;
            line.text = buffer.substr(pos);
            lines.push_back(std::move(line));
            break;
        }
        const bool carriageReturn = newline > pos && buffer[newline - 1] == '\r';
        const size_t textEnd = carriageReturn ? newline - 1 : newline;
        LangLine line;
        line.text = buffer.substr(pos, textEnd - pos);
        line.ending = carriageReturn ? "\r\n" : "\n";
        lines.push_back(std::move(line));
        pos = newline + 1;
    }

    for (LangLine& line : lines) {
        const std::string_view trimmed = TrimAscii(line.text);
        if (trimmed.empty() || IsCommentLine(trimmed)) {
            continue;
        }
        const size_t equals = line.text.find('=');
        if (equals == std::string::npos) {
            err = "操作文件包含缺少 '=' 的行，无法更新";
            return false;
        }
        line.isEntry = true;
        line.key.assign(TrimAscii(std::string_view(line.text).substr(0, equals)));
    }

    const std::string newline = DetectNewline(buffer);
    const bool trailingNewline = !buffer.empty() && buffer.back() == '\n';
    std::map<uint32_t, std::list<LangLine>::iterator> anchors;

    std::vector<std::list<LangLine>::iterator> eraseList;
    for (auto it = lines.begin(); it != lines.end(); ++it) {
        if (!it->isEntry) {
            continue;
        }
        if (pasted.find(it->key) != pasted.end()) {
            ++stats.updated;
            eraseList.push_back(it);
            continue;
        }
        if (removedKeys.find(it->key) != removedKeys.end()) {
            ++stats.removed;
            eraseList.push_back(it);
            continue;
        }
        ++stats.untouched;
        const auto position = newIndex.find(it->key);
        if (position != newIndex.end()) {
            anchors[position->second] = it;
        }
    }
    for (auto it : eraseList) {
        lines.erase(it);
    }
    stats.inserted = pasted.size() - stats.updated;

    std::list<LangLine>::iterator headEntry = lines.end();
    for (auto it = lines.begin(); it != lines.end(); ++it) {
        if (it->isEntry) {
            headEntry = it;
            break;
        }
    }

    for (uint32_t i = 0; i < orderFile.entries.size(); ++i) {
        const std::string_view key = orderFile.entries[i].key;
        const auto pastedIt = pasted.find(key);
        if (pastedIt == pasted.end()) {
            continue;
        }
        const auto anchor = anchors.lower_bound(i);
        std::list<LangLine>::iterator position;
        if (anchor == anchors.begin()) {
            position = headEntry;
        } else {
            position = std::next(std::prev(anchor)->second);
        }
        LangLine line;
        line.text = pastedIt->second->raw;
        line.isEntry = true;
        line.key = std::string(key);
        const auto inserted = lines.insert(position, std::move(line));
        anchors[i] = inserted;
    }

    content.clear();
    content.reserve(buffer.size() + 1024);
    for (auto it = lines.begin(); it != lines.end(); ++it) {
        content.append(it->text);
        const bool last = std::next(it) == lines.end();
        if (!last) {
            content.append(it->ending.empty() ? newline : it->ending);
        } else if (trailingNewline) {
            content.append(it->ending.empty() ? newline : it->ending);
        }
    }

    stats.totalAfter = 0;
    for (const LangLine& line : lines) {
        if (line.isEntry) {
            ++stats.totalAfter;
        }
    }
    return true;
}

// ---------------------------------------------------------------- 写回

bool WriteUpdatedTarget(const std::wstring& targetPath, FileFormat format, std::string_view content,
                        std::string& err) {
    const std::wstring tempPath =
        targetPath + (format == FileFormat::Json ? L".mclangdiff.json" : L".mclangdiff.lang");
    DeleteFileW(tempPath.c_str());

    if (!WriteUtf8File(tempPath, content, false, err)) {
        DeleteFileW(tempPath.c_str());
        err = "无法写入临时文件：" + err;
        return false;
    }

    auto check = std::make_unique<LangFile>();
    std::string checkError;
    if (!LoadLangFile(tempPath, format, *check, checkError)) {
        DeleteFileW(tempPath.c_str());
        err = "生成的更新文件未通过校验，已取消写入：" + checkError;
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), targetPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(tempPath.c_str());
        err = "无法替换操作文件：" + WideToUtf8(Win32ErrorText(code));
        return false;
    }
    return true;
}

bool ValidatePastedCoverage(const std::vector<std::string_view>& requiredKeys,
                            const PastedBlock& pasted,
                            std::unordered_map<std::string_view, const PastedEntry*>& pastedMap,
                            std::string& err) {
    pastedMap.clear();
    pastedMap.reserve(pasted.entries.size() * 2 + 1);
    std::unordered_set<std::string_view> requiredSet(requiredKeys.begin(), requiredKeys.end());
    std::vector<std::string> problems;

    for (const PastedEntry& entry : pasted.entries) {
        if (requiredSet.find(entry.key) == requiredSet.end()) {
            problems.push_back("第 " + std::to_string(entry.line) + " 行：键 \"" + entry.key +
                               "\" 不属于本次需要粘贴的键");
            continue;
        }
        const auto inserted = pastedMap.emplace(entry.key, &entry);
        if (!inserted.second) {
            problems.push_back("第 " + std::to_string(entry.line) + " 行：键 \"" + entry.key +
                               "\" 重复（首次出现于第 " +
                               std::to_string(inserted.first->second->line) + " 行）");
        }
    }

    std::vector<std::string_view> missing;
    for (const std::string_view key : requiredKeys) {
        if (pastedMap.find(key) == pastedMap.end()) {
            missing.push_back(key);
        }
    }
    if (!missing.empty()) {
        std::string line = "缺少 " + std::to_string(missing.size()) + " 个键：";
        const size_t shown = std::min<size_t>(missing.size(), 10);
        for (size_t i = 0; i < shown; ++i) {
            if (i != 0) {
                line += "、";
            }
            line += std::string(missing[i]);
        }
        if (missing.size() > shown) {
            line += " 等";
        }
        problems.push_back(std::move(line));
    }

    if (!problems.empty()) {
        std::string message = "粘贴内容校验失败，未修改操作文件；请修正以下问题：\n\n";
        const size_t shown = std::min(problems.size(), kMaxReportedProblems);
        for (size_t i = 0; i < shown; ++i) {
            message += "- ";
            message += problems[i];
            message += "\n";
        }
        if (problems.size() > shown) {
            message += "- ……共 " + std::to_string(problems.size()) + " 个问题，仅显示前 " +
                       std::to_string(shown) + " 个\n";
        }
        message +=
            "\n提示：只粘贴要求的新翻译条目，不要粘贴无关或重复的键；LANG 文件使用 key=value 行。";
        err = message;
        return false;
    }
    return true;
}

struct KeyDiff {
    std::vector<std::string_view> missing;
    std::vector<std::string_view> extra;
};

void ComputeKeyDiff(const LangFile& target, const LangFile& reference, KeyDiff& diff) {
    diff.missing.clear();
    diff.extra.clear();
    for (const Entry& entry : reference.entries) {
        if (target.index.find(entry.key) == target.index.end()) {
            diff.missing.push_back(entry.key);
        }
    }
    for (const Entry& entry : target.entries) {
        if (reference.index.find(entry.key) == reference.index.end()) {
            diff.extra.push_back(entry.key);
        }
    }
}

std::string JoinKeys(const std::vector<std::string_view>& keys, size_t maxCount) {
    std::string text;
    const size_t shown = std::min(keys.size(), maxCount);
    for (size_t i = 0; i < shown; ++i) {
        if (i != 0) {
            text += "、";
        }
        text += std::string(keys[i]);
    }
    if (keys.size() > shown) {
        text += " 等";
    }
    return text;
}

std::string BuildConsistencyMessage(const KeyDiff& diff) {
    std::string message = "操作文件与旧 en_us 的键集合不一致，未修改文件。\n\n";
    if (!diff.missing.empty()) {
        message += "缺少 " + std::to_string(diff.missing.size()) +
                   " 个键：" + JoinKeys(diff.missing, 12) + "\n";
    }
    if (!diff.extra.empty()) {
        message += "多出 " + std::to_string(diff.extra.size()) +
                   " 个键：" + JoinKeys(diff.extra, 12) + "\n";
    }
    message += "\n请先在 E 页将操作文件同步到旧 en_us，再回到 D 页更新到新版本。";
    return message;
}

}  // namespace

std::vector<std::string_view> CollectRequiredKeys(const DiffResult& diff) {
    std::vector<std::string_view> keys;
    if (diff.newFile == nullptr) {
        return keys;
    }
    const auto& entries = diff.newFile->entries;
    std::vector<uint8_t> required(entries.size(), 0);
    for (const uint32_t index : diff.added) {
        if (index < entries.size()) {
            required[index] = 1;
        }
    }
    for (const ChangePair& pair : diff.changed) {
        if (pair.newIndex < entries.size()) {
            required[pair.newIndex] = 1;
        }
    }
    keys.reserve(diff.added.size() + diff.changed.size());
    for (size_t i = 0; i < required.size(); ++i) {
        if (required[i] != 0) {
            keys.push_back(entries[i].key);
        }
    }
    return keys;
}

bool ParsePastedBlock(std::string_view text, FileFormat format, PastedBlock& out, std::string& err) {
    out.entries.clear();
    if (format == FileFormat::Json) {
        std::vector<PastedEntry> entries;
        JsonFragmentParser parser(text);
        if (!parser.Parse(entries, err)) {
            return false;
        }
        out.entries = std::move(entries);
        return true;
    }
    if (format == FileFormat::Lang) {
        return ParseLangFragment(text, out.entries, err);
    }
    err = "未知的语言文件格式";
    return false;
}

bool CheckTargetConsistency(const std::wstring& targetPath, const DiffResult& diff,
                            ConsistencyReport& report) {
    report = ConsistencyReport{};
    if (diff.oldFile == nullptr || diff.newFile == nullptr) {
        report.state = TargetConsistency::Error;
        report.error = "比较结果缺少源文件数据，请重新比较。";
        return false;
    }

    auto target = std::make_unique<LangFile>();
    std::string loadError;
    if (!LoadLangFile(targetPath, diff.format, *target, loadError)) {
        report.state = TargetConsistency::Error;
        report.error = "操作文件：" + loadError;
        return false;
    }

    KeyDiff vsOld;
    ComputeKeyDiff(*target, *diff.oldFile, vsOld);
    report.missing.reserve(vsOld.missing.size());
    for (const std::string_view key : vsOld.missing) {
        report.missing.emplace_back(key);
    }
    report.extra.reserve(vsOld.extra.size());
    for (const std::string_view key : vsOld.extra) {
        report.extra.emplace_back(key);
    }

    if (report.missing.empty() && report.extra.empty()) {
        report.state = TargetConsistency::MatchesOld;
        return true;
    }

    KeyDiff vsNew;
    ComputeKeyDiff(*target, *diff.newFile, vsNew);
    report.state = (vsNew.missing.empty() && vsNew.extra.empty())
                       ? TargetConsistency::MatchesNew
                       : TargetConsistency::Mismatch;
    return true;
}

bool BuildMissingEntriesTemplate(const DiffResult& diff, const ConsistencyReport& report,
                                 std::string& text) {
    text.clear();
    if (diff.oldFile == nullptr) {
        return false;
    }
    bool first = true;
    for (const std::string& key : report.missing) {
        const auto found = diff.oldFile->index.find(key);
        if (found == diff.oldFile->index.end()) {
            continue;
        }
        const Entry& entry = diff.oldFile->entries[found->second];
        if (diff.format == FileFormat::Json) {
            if (!first) {
                text.append(",\n");
            }
            text.append(entry.indent);
            text.append(entry.raw);
        } else {
            if (!first) {
                text.push_back('\n');
            }
            text.append(entry.raw);
        }
        first = false;
    }
    return true;
}

bool ApplyTargetSync(const std::wstring& targetPath, const DiffResult& diff,
                     const PastedBlock& pasted, UpdateStats& stats, std::string& err) {
    stats = UpdateStats{};
    if (diff.oldFile == nullptr || diff.newFile == nullptr) {
        err = "比较结果缺少源文件数据，请重新比较。";
        return false;
    }
    if (IsSameFile(targetPath, Utf8ToWide(diff.oldPath)) ||
        IsSameFile(targetPath, Utf8ToWide(diff.newPath))) {
        err = "操作文件不能与旧/新 en_us 文件相同。";
        return false;
    }

    auto target = std::make_unique<LangFile>();
    if (!LoadLangFile(targetPath, diff.format, *target, err)) {
        err = "操作文件：" + err;
        return false;
    }

    KeyDiff diffKeys;
    ComputeKeyDiff(*target, *diff.oldFile, diffKeys);
    if (diffKeys.missing.empty() && diffKeys.extra.empty()) {
        err = "操作文件与旧 en_us 的键已一致，无需同步。";
        return false;
    }

    const std::vector<std::string_view> required(diffKeys.missing.begin(), diffKeys.missing.end());
    std::unordered_map<std::string_view, const PastedEntry*> pastedMap;
    if (!ValidatePastedCoverage(required, pasted, pastedMap, err)) {
        return false;
    }

    std::unordered_set<std::string_view> removedKeys(diffKeys.extra.begin(), diffKeys.extra.end());
    std::unordered_map<std::string_view, uint32_t> oldIndex;
    oldIndex.reserve(diff.oldFile->entries.size() * 2 + 1);
    for (uint32_t i = 0; i < diff.oldFile->entries.size(); ++i) {
        oldIndex.emplace(diff.oldFile->entries[i].key, i);
    }

    std::string content;
    if (diff.format == FileFormat::Json) {
        if (!BuildJsonContent(*target, *diff.oldFile, pastedMap, removedKeys, oldIndex, stats, content)) {
            err = "生成同步后的 JSON 内容失败。";
            return false;
        }
    } else {
        if (!BuildLangContent(*target, *diff.oldFile, pastedMap, removedKeys, oldIndex, stats,
                              content, err)) {
            return false;
        }
    }

    if (!WriteUpdatedTarget(targetPath, diff.format, content, err)) {
        return false;
    }
    return true;
}

bool ApplyTranslationUpdate(const std::wstring& targetPath, const DiffResult& diff,
                            const PastedBlock& pasted, UpdateStats& stats, std::string& err) {
    stats = UpdateStats{};
    if (diff.oldFile == nullptr || diff.newFile == nullptr) {
        err = "比较结果缺少源文件数据，请重新比较。";
        return false;
    }
    if (IsSameFile(targetPath, Utf8ToWide(diff.oldPath)) ||
        IsSameFile(targetPath, Utf8ToWide(diff.newPath))) {
        err = "操作文件不能与旧/新 en_us 文件相同。";
        return false;
    }

    auto target = std::make_unique<LangFile>();
    if (!LoadLangFile(targetPath, diff.format, *target, err)) {
        err = "操作文件：" + err;
        return false;
    }

    KeyDiff vsOld;
    ComputeKeyDiff(*target, *diff.oldFile, vsOld);
    if (!vsOld.missing.empty() || !vsOld.extra.empty()) {
        err = BuildConsistencyMessage(vsOld);
        return false;
    }

    const std::vector<std::string_view> requiredKeys = CollectRequiredKeys(diff);
    std::unordered_map<std::string_view, const PastedEntry*> pastedMap;
    if (!ValidatePastedCoverage(requiredKeys, pasted, pastedMap, err)) {
        return false;
    }

    std::unordered_map<std::string_view, uint32_t> newIndex;
    newIndex.reserve(diff.newFile->entries.size() * 2 + 1);
    for (uint32_t i = 0; i < diff.newFile->entries.size(); ++i) {
        newIndex.emplace(diff.newFile->entries[i].key, i);
    }

    std::unordered_set<std::string_view> removedKeys;
    removedKeys.reserve(diff.removed.size() * 2 + 1);
    for (const uint32_t index : diff.removed) {
        if (index < diff.oldFile->entries.size()) {
            removedKeys.insert(diff.oldFile->entries[index].key);
        }
    }

    if (requiredKeys.empty() && removedKeys.empty()) {
        err = "当前比较结果没有新增、值变更或删除条目，无需更新操作文件。";
        return false;
    }

    std::string content;
    if (diff.format == FileFormat::Json) {
        if (!BuildJsonContent(*target, *diff.newFile, pastedMap, removedKeys, newIndex, stats,
                              content)) {
            err = "生成更新后的 JSON 内容失败。";
            return false;
        }
    } else {
        if (!BuildLangContent(*target, *diff.newFile, pastedMap, removedKeys, newIndex, stats,
                              content, err)) {
            return false;
        }
    }

    if (!WriteUpdatedTarget(targetPath, diff.format, content, err)) {
        return false;
    }
    return true;
}

}  // namespace mcd

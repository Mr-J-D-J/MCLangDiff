#include "diff.h"

#include <chrono>

#include "hash.h"
#include "util.h"

namespace mcd {
namespace {

void AppendRawEntry(std::string& out, const Entry& entry) {
    out.append(entry.indent);
    out.append(entry.raw);
}

std::string BuildAddedOrRemovedBlock(const LangFile& file, const std::vector<uint32_t>& indices) {
    size_t reserve = 0;
    for (const uint32_t index : indices) {
        reserve += file.entries[index].indent.size() + file.entries[index].raw.size() + 2;
    }
    std::string out;
    out.reserve(reserve);

    const bool isJson = file.format == FileFormat::Json;
    bool first = true;
    for (const uint32_t index : indices) {
        if (!first) {
            out.append(isJson ? ",\n" : "\n");
        }
        first = false;
        AppendRawEntry(out, file.entries[index]);
    }
    return out;
}

std::string BuildChangedBlock(const LangFile& oldFile, const LangFile& newFile,
                              const std::vector<ChangePair>& pairs) {
    size_t reserve = 0;
    for (const ChangePair& pair : pairs) {
        reserve += oldFile.entries[pair.oldIndex].raw.size() + newFile.entries[pair.newIndex].raw.size() +
                   oldFile.entries[pair.oldIndex].indent.size() +
                   newFile.entries[pair.newIndex].indent.size() + 32;
    }
    std::string out;
    out.reserve(reserve);

    bool first = true;
    for (const ChangePair& pair : pairs) {
        if (!first) {
            out.append("\n\n");
        }
        first = false;
        out.append("旧 / Old\n");
        AppendRawEntry(out, oldFile.entries[pair.oldIndex]);
        out.append("\n新 / New\n");
        AppendRawEntry(out, newFile.entries[pair.newIndex]);
    }
    return out;
}

std::string CountText(size_t value) {
    return std::to_string(value);
}

std::string ElapsedText(double milliseconds) {
    if (milliseconds < 1.0) {
        return "<1 ms";
    }
    return std::to_string(static_cast<long long>(milliseconds + 0.5)) + " ms";
}

void AppendSection(std::string& report, const char* title, const std::string& stats,
                   const std::string& block) {
    report.append("============================================================\n");
    report.append(title);
    report.append("\n");
    report.append(stats);
    report.append("\n\n");
    if (!block.empty()) {
        report.append(block);
        report.append("\n");
    }
    report.append("\n");
}

}  // namespace

bool CompareFiles(const std::wstring& oldPath, const std::wstring& newPath,
                  std::unique_ptr<DiffResult>& out, std::string& err) {
    const auto started = std::chrono::steady_clock::now();

    if (IsSameFile(oldPath, newPath)) {
        err = "新旧文件为同一文件，请选择两个不同的文件。";
        return false;
    }

    const FileFormat oldFormat = FormatFromPath(oldPath);
    const FileFormat newFormat = FormatFromPath(newPath);
    if (oldFormat == FileFormat::Unknown) {
        err = "旧文件格式不支持：仅支持 .json 与 .lang 文件。";
        return false;
    }
    if (newFormat == FileFormat::Unknown) {
        err = "新文件格式不支持：仅支持 .json 与 .lang 文件。";
        return false;
    }
    if (oldFormat != newFormat) {
        err = std::string("格式不一致：旧文件为 ") + FormatName(oldFormat) + "，新文件为 " +
              FormatName(newFormat) + "。只能比较相同格式的两个文件。";
        return false;
    }

    auto oldFile = std::make_unique<LangFile>();
    if (!LoadLangFile(oldPath, oldFormat, *oldFile, err)) {
        err = "旧文件：" + err;
        return false;
    }
    auto newFile = std::make_unique<LangFile>();
    if (!LoadLangFile(newPath, newFormat, *newFile, err)) {
        err = "新文件：" + err;
        return false;
    }

    auto result = std::make_unique<DiffResult>();
    result->format = oldFormat;
    result->oldPath = oldFile->path;
    result->newPath = newFile->path;
    result->oldBytes = oldFile->byteSize;
    result->newBytes = newFile->byteSize;
    result->oldKeyCount = oldFile->entries.size();
    result->newKeyCount = newFile->entries.size();

    const auto& oldEntries = oldFile->entries;
    const auto& newEntries = newFile->entries;

    result->added.reserve(newEntries.size());
    result->changed.reserve(newEntries.size());
    std::vector<uint8_t> oldMatched(oldEntries.size(), 0);

    // 遍历新文件：同时得到“新增”（新文件顺序）与“值变更”（新文件顺序）。
    for (uint32_t newIndex = 0; newIndex < newEntries.size(); ++newIndex) {
        const Entry& newEntry = newEntries[newIndex];
        const auto found = oldFile->index.find(newEntry.key);
        if (found == oldFile->index.end()) {
            result->added.push_back(newIndex);
            continue;
        }

        const uint32_t oldIndex = found->second;
        oldMatched[oldIndex] = 1;

        // 每个共同键的新旧值各只哈希一次；UTF-8 字节参与运算。
        const Hash128 newHash = HashBytes(newEntry.value);
        const Hash128 oldHash = HashBytes(oldEntries[oldIndex].value);
        if (newHash == oldHash) {
            ++result->unchanged;
        } else {
            result->changed.push_back(ChangePair{oldIndex, newIndex});
        }
    }

    // 未在新文件中出现的旧键即为“删除”，保持旧文件顺序。
    result->removed.reserve(oldEntries.size());
    for (uint32_t oldIndex = 0; oldIndex < oldEntries.size(); ++oldIndex) {
        if (oldMatched[oldIndex] == 0) {
            result->removed.push_back(oldIndex);
        }
    }

    result->blockAdded = BuildAddedOrRemovedBlock(*newFile, result->added);
    result->blockRemoved = BuildAddedOrRemovedBlock(*oldFile, result->removed);
    result->blockChanged = BuildChangedBlock(*oldFile, *newFile, result->changed);

    const std::string addedCount = CountText(result->added.size());
    const std::string removedCount = CountText(result->removed.size());
    const std::string changedCount = CountText(result->changed.size());
    result->statsAdded = "统计：新增键 " + addedCount + " 个 · 条目 " + addedCount + " 条 · 来源：新文件";
    result->statsRemoved =
        "统计：删除键 " + removedCount + " 个 · 条目 " + removedCount + " 条 · 来源：旧文件";
    result->statsChanged = "统计：值变更 " + changedCount + " 个 · 旧条目 " + changedCount +
                           " 条 / 新条目 " + changedCount + " 条";

    const auto finished = std::chrono::steady_clock::now();
    result->elapsedMs = std::chrono::duration<double, std::milli>(finished - started).count();

    std::string report;
    report.reserve(result->blockAdded.size() + result->blockRemoved.size() +
                   result->blockChanged.size() + 2048);
    report.append("MCLangDiff 语言文件差异报告\n");
    report.append("生成时间：").append(FormatTimestamp()).append("\n");
    report.append("文件格式：").append(FormatName(result->format)).append("\n");
    report.append("条目顺序：新增/值变更按新文件出现顺序，删除按旧文件出现顺序\n");
    report.append("旧文件 / Old：").append(result->oldPath).append("（");
    report.append(WideToUtf8(FormatSize(result->oldBytes))).append(" · ");
    report.append(CountText(result->oldKeyCount)).append(" 个键）\n");
    report.append("新文件 / New：").append(result->newPath).append("（");
    report.append(WideToUtf8(FormatSize(result->newBytes))).append(" · ");
    report.append(CountText(result->newKeyCount)).append(" 个键）\n");
    report.append("汇总：新增 ").append(addedCount).append(" · 删除 ").append(removedCount);
    report.append(" · 值变更 ").append(changedCount).append(" · 未变化 ");
    report.append(CountText(result->unchanged)).append(" · 耗时 ");
    report.append(ElapsedText(result->elapsedMs)).append("\n\n");

    AppendSection(report, "[A] 新增键 / Added", result->statsAdded, result->blockAdded);
    AppendSection(report, "[B] 删除键 / Removed", result->statsRemoved, result->blockRemoved);
    AppendSection(report, "[C] 值变更 / Changed", result->statsChanged, result->blockChanged);

    result->fullReport = std::move(report);
    result->oldFile = std::move(oldFile);
    result->newFile = std::move(newFile);
    out = std::move(result);
    return true;
}

}  // namespace mcd

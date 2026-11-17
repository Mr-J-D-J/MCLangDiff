// 核心逻辑测试：直接调用解析与比较 API，不依赖图形界面。
// 构建与运行：tests\run_tests.ps1

#include <windows.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "diff.h"
#include "hash.h"
#include "lang_file.h"
#include "update_file.h"
#include "util.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* expression, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL  line %d: %s\n", line, expression);
    }
}

void CheckContains(const std::string& text, const std::string& needle, const char* expression, int line) {
    ++g_checks;
    if (text.find(needle) == std::string::npos) {
        ++g_failures;
        std::printf("FAIL  line %d: %s\n      missing: %s\n", line, expression, needle.c_str());
    }
}

void CheckNotContains(const std::string& text, const std::string& needle, const char* expression, int line) {
    ++g_checks;
    if (text.find(needle) != std::string::npos) {
        ++g_failures;
        std::printf("FAIL  line %d: %s\n      unexpected: %s\n", line, expression, needle.c_str());
    }
}

void CheckBefore(const std::string& text, const std::string& first, const std::string& second, int line) {
    ++g_checks;
    const size_t firstPos = text.find(first);
    const size_t secondPos = text.find(second);
    if (firstPos == std::string::npos || secondPos == std::string::npos || firstPos >= secondPos) {
        ++g_failures;
        std::printf("FAIL  line %d: expected \"%s\" before \"%s\"\n", line, first.c_str(), second.c_str());
    }
}

void CheckSize(size_t actual, size_t expected, const char* expression, int line) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL  line %d: %s (expected %zu, got %zu)\n", line, expression, expected, actual);
    }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)
#define CHECK_CONTAINS(text, needle) CheckContains((text), (needle), #text " contains " #needle, __LINE__)
#define CHECK_NOT_CONTAINS(text, needle) CheckNotContains((text), (needle), #text " excludes " #needle, __LINE__)
#define CHECK_BEFORE(text, first, second) CheckBefore((text), (first), (second), __LINE__)
#define CHECK_SIZE(actual, expected) CheckSize((actual), (expected), #actual, __LINE__)

std::wstring ExecutableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    std::wstring path(buffer.data(), length);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

// 从测试程序所在目录向上查找包含 samples\old.json 的项目根目录。
std::wstring FindProjectDirectory(std::wstring start) {
    for (int i = 0; i < 5; ++i) {
        const std::wstring probe = start + L"\\samples\\old.json";
        if (GetFileAttributesW(probe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return start;
        }
        const size_t slash = start.find_last_of(L"\\/");
        if (slash == std::wstring::npos || slash < 2) {
            break;
        }
        start.resize(slash);
    }
    return ExecutableDirectory() + L"\\..";
}

bool WriteBytes(const std::wstring& path, std::string_view bytes) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    written == bytes.size();
    CloseHandle(file);
    return ok;
}

std::wstring PathJoin(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) {
        return right;
    }
    return left + L"\\" + right;
}

}  // namespace

int wmain() {
    const std::wstring testsDir = ExecutableDirectory();
    const std::wstring projectDir = FindProjectDirectory(testsDir);
    const std::wstring samples = projectDir + L"\\samples";
    const std::wstring tmpDir = projectDir + L"\\tests\\tmp";
    CreateDirectoryW(tmpDir.c_str(), nullptr);

    const std::wstring oldJsonPath = samples + L"\\old.json";
    const std::wstring newJsonPath = samples + L"\\new.json";
    const std::wstring oldLangPath = samples + L"\\old.lang";
    const std::wstring newLangPath = samples + L"\\new.lang";

    std::printf("== 哈希 ==");
    std::printf("\n");
    CHECK(mcd::HashBytes("abc") == mcd::HashBytes("abc"));
    CHECK(mcd::HashBytes("abc") != mcd::HashBytes("abd"));
    CHECK(mcd::HashBytes("中文") != mcd::HashBytes("中女"));
    CHECK(mcd::HashBytes("") != mcd::HashBytes("a"));
    CHECK(mcd::HashBytes(std::string(4096, 'a')) != mcd::HashBytes(std::string(4096, 'b')));
    CHECK(mcd::HashBytes("a").lo != mcd::HashBytes("a").hi);

    std::printf("== JSON 解析 ==\n");
    std::string err;
    auto oldJson = std::make_unique<mcd::LangFile>();
    CHECK(mcd::LoadLangFile(oldJsonPath, mcd::FileFormat::Unknown, *oldJson, err));
    CHECK(oldJson->format == mcd::FileFormat::Json);
    CHECK_SIZE(oldJson->entries.size(), 16);
    CHECK(oldJson->entries[0].key == "item.demo.sword.name");
    CHECK(oldJson->entries[0].raw == "\"item.demo.sword.name\": \"演示之剑\"");
    CHECK(oldJson->entries[0].indent == "  ");
    CHECK(oldJson->entries[13].value == "他说：\"你好\"");
    CHECK(oldJson->entries[14].value == "中文");

    std::printf("== JSON 比较 ==\n");
    std::unique_ptr<mcd::DiffResult> jsonDiff;
    CHECK(mcd::CompareFiles(oldJsonPath, newJsonPath, jsonDiff, err));
    CHECK(jsonDiff != nullptr);
    if (jsonDiff != nullptr) {
        CHECK_SIZE(jsonDiff->added.size(), 3);
        CHECK_SIZE(jsonDiff->removed.size(), 2);
        CHECK_SIZE(jsonDiff->changed.size(), 2);
        CHECK_SIZE(jsonDiff->unchanged, 12);
        CHECK_SIZE(jsonDiff->oldKeyCount, 16);
        CHECK_SIZE(jsonDiff->newKeyCount, 17);

        CHECK_CONTAINS(jsonDiff->blockAdded, "\"block.demo.new_block.name\": \"新方块\",\n  \"gui.demo.settings\"");
        CHECK_CONTAINS(jsonDiff->blockAdded, "\"item.demo.new_only.name\" : \"仅存在于新文件\"");
        CHECK_CONTAINS(jsonDiff->blockRemoved, "\"gui.demo.legacy\": \"旧版按钮\"");
        CHECK_CONTAINS(jsonDiff->blockRemoved, "\"item.demo.old_only.name\": \"仅存在于旧文件\"");
        CHECK_CONTAINS(jsonDiff->blockChanged, "旧 / Old\n  \"item.demo.pickaxe.name\": \"演示镐\"");
        CHECK_CONTAINS(jsonDiff->blockChanged, "新 / New\n  \"item.demo.pickaxe.name\": \"演示镐（强化）\"");
        CHECK_NOT_CONTAINS(jsonDiff->blockChanged, "item.demo.unicode.name");
        CHECK_NOT_CONTAINS(jsonDiff->blockChanged, "item.demo.quoted.name");
        CHECK_NOT_CONTAINS(jsonDiff->blockAdded, "统计");
        CHECK_NOT_CONTAINS(jsonDiff->blockRemoved, "统计");
        CHECK_NOT_CONTAINS(jsonDiff->blockChanged, "统计");
        CHECK_BEFORE(jsonDiff->blockAdded, "block.demo.new_block.name", "gui.demo.settings");
        CHECK_BEFORE(jsonDiff->blockAdded, "gui.demo.settings", "item.demo.new_only.name");
        CHECK_BEFORE(jsonDiff->blockRemoved, "gui.demo.legacy", "item.demo.old_only.name");
        CHECK_BEFORE(jsonDiff->blockChanged, "item.demo.pickaxe.name", "gui.demo.close");

        CHECK_CONTAINS(jsonDiff->fullReport, "[A] 新增键 / Added");
        CHECK_CONTAINS(jsonDiff->fullReport, "[B] 删除键 / Removed");
        CHECK_CONTAINS(jsonDiff->fullReport, "[C] 值变更 / Changed");
        CHECK_CONTAINS(jsonDiff->fullReport, "未变化 12");
        CHECK_CONTAINS(jsonDiff->fullReport, "统计：新增键 3 个");
        CHECK_NOT_CONTAINS(jsonDiff->statsAdded, "旧 / Old");
    }

    std::printf("== LANG 解析与比较 ==\n");
    auto oldLang = std::make_unique<mcd::LangFile>();
    CHECK(mcd::LoadLangFile(oldLangPath, mcd::FileFormat::Unknown, *oldLang, err));
    CHECK_SIZE(oldLang->entries.size(), 13);
    CHECK(oldLang->entries[0].key == "item.demo.sword.name");
    CHECK(oldLang->entries[0].raw == "item.demo.sword.name=演示之剑");

    std::unique_ptr<mcd::DiffResult> langDiff;
    CHECK(mcd::CompareFiles(oldLangPath, newLangPath, langDiff, err));
    CHECK(langDiff != nullptr);
    if (langDiff != nullptr) {
        CHECK_SIZE(langDiff->added.size(), 3);
        CHECK_SIZE(langDiff->removed.size(), 2);
        CHECK_SIZE(langDiff->changed.size(), 2);
        CHECK_SIZE(langDiff->unchanged, 9);
        CHECK_CONTAINS(langDiff->blockAdded, "block.demo.new_block.name=新方块\ngui.demo.settings=设置");
        CHECK_CONTAINS(langDiff->blockChanged,
                       "旧 / Old\ngui.demo.close=关闭\n新 / New\ngui.demo.close=关闭界面");
        CHECK_NOT_CONTAINS(langDiff->blockAdded, "##");
        CHECK_NOT_CONTAINS(langDiff->blockAdded, "统计");
    }

    std::printf("== 粘贴块解析 ==\n");
    {
        mcd::PastedBlock wrapped;
        CHECK(mcd::ParsePastedBlock("{ \"a\": \"1\", }", mcd::FileFormat::Json, wrapped, err));
        CHECK_SIZE(wrapped.entries.size(), 1);
        CHECK(wrapped.entries[0].key == "a");
        CHECK(wrapped.entries[0].raw == "\"a\": \"1\"");

        mcd::PastedBlock emptyBlock;
        CHECK(mcd::ParsePastedBlock("   ", mcd::FileFormat::Json, emptyBlock, err));
        CHECK_SIZE(emptyBlock.entries.size(), 0);

        mcd::PastedBlock escapedKey;
        CHECK(mcd::ParsePastedBlock("\"a\\u0062\": \"x\"", mcd::FileFormat::Json, escapedKey, err));
        CHECK_SIZE(escapedKey.entries.size(), 1);
        CHECK(escapedKey.entries[0].key == "ab");

        mcd::PastedBlock badJson;
        CHECK(!mcd::ParsePastedBlock("\"a\": 1", mcd::FileFormat::Json, badJson, err));
        CHECK_CONTAINS(err, "值必须是字符串");

        mcd::PastedBlock duplicateJson;
        CHECK(!mcd::ParsePastedBlock("\"a\": \"1\", \"a\": \"2\"", mcd::FileFormat::Json,
                                     duplicateJson, err));
        CHECK_CONTAINS(err, "重复");

        mcd::PastedBlock langBlock;
        CHECK(mcd::ParsePastedBlock("## 注释\n\nb=2\na=1\n", mcd::FileFormat::Lang, langBlock, err));
        CHECK_SIZE(langBlock.entries.size(), 2);
        CHECK(langBlock.entries[0].key == "b");
        CHECK(langBlock.entries[1].raw == "a=1");

        mcd::PastedBlock badLang;
        CHECK(!mcd::ParsePastedBlock("key_without_separator\n", mcd::FileFormat::Lang, badLang, err));
        CHECK_CONTAINS(err, "缺少 '='");
    }

    std::printf("== 更新操作文件（JSON） ==\n");
    const std::wstring sampleJsonTarget = samples + L"\\zh_cn.json";
    const std::wstring targetJson = PathJoin(tmpDir, L"zh_cn.json");
    CHECK(CopyFileW(sampleJsonTarget.c_str(), targetJson.c_str(), FALSE) != FALSE);

    mcd::PastedBlock updateJson;
    const std::string updateJsonText =
        "\"block.demo.new_block.name\": \"新方块\",\n"
        "\"gui.demo.close\": \"关闭界面\",\n"
        "\"gui.demo.settings\": \"设置\",\n"
        "\"item.demo.new_only.name\" : \"仅存在于新文件\",\n"
        "\"item.demo.pickaxe.name\": \"演示镐（强化）\"";
    CHECK(mcd::ParsePastedBlock(updateJsonText, mcd::FileFormat::Json, updateJson, err));
    CHECK_SIZE(updateJson.entries.size(), 5);

    mcd::UpdateStats jsonStats;
    CHECK(mcd::ApplyTranslationUpdate(targetJson, *jsonDiff, updateJson, jsonStats, err));
    CHECK_SIZE(jsonStats.inserted, 4);
    CHECK_SIZE(jsonStats.updated, 1);
    CHECK_SIZE(jsonStats.removed, 2);
    CHECK_SIZE(jsonStats.untouched, 6);
    CHECK_SIZE(jsonStats.totalAfter, 11);

    std::string updatedJsonBytes;
    CHECK(mcd::ReadFileBytes(targetJson, updatedJsonBytes, mcd::kMaxFileBytes, err));
    CHECK(!(updatedJsonBytes.size() >= 3 &&
            static_cast<unsigned char>(updatedJsonBytes[0]) == 0xEF &&
            static_cast<unsigned char>(updatedJsonBytes[1]) == 0xBB &&
            static_cast<unsigned char>(updatedJsonBytes[2]) == 0xBF));
    CHECK(updatedJsonBytes.rfind("{\n  \"item.demo.sword.name\"", 0) == 0);

    auto updatedJson = std::make_unique<mcd::LangFile>();
    CHECK(mcd::LoadLangFile(targetJson, mcd::FileFormat::Json, *updatedJson, err));
    CHECK_SIZE(updatedJson->entries.size(), 11);
    auto jsonEntryIndex = [&updatedJson](std::string_view key) -> int {
        const auto it = updatedJson->index.find(key);
        return it == updatedJson->index.end() ? -1 : static_cast<int>(it->second);
    };
    CHECK(jsonEntryIndex("item.demo.sword.name") == 0);
    CHECK(jsonEntryIndex("item.demo.pickaxe.name") == 1);
    CHECK(jsonEntryIndex("item.demo.gem.name") == 2);
    CHECK(jsonEntryIndex("block.demo.lamp.name") == 3);
    CHECK(jsonEntryIndex("block.demo.new_block.name") == 4);
    CHECK(jsonEntryIndex("gui.demo.title") == 5);
    CHECK(jsonEntryIndex("gui.demo.close") == 6);
    CHECK(jsonEntryIndex("gui.demo.settings") == 7);
    CHECK(jsonEntryIndex("message.demo.welcome") == 8);
    CHECK(jsonEntryIndex("item.demo.new_only.name") == 9);
    CHECK(jsonEntryIndex("item.demo.extra.name") == 10);
    CHECK(jsonEntryIndex("gui.demo.legacy") == -1);
    CHECK(jsonEntryIndex("item.demo.old_only.name") == -1);
    CHECK(updatedJson->entries[6].value == "关闭界面");
    CHECK(updatedJson->entries[7].value == "设置");
    CHECK(updatedJson->entries[10].raw == "\"item.demo.extra.name\": \"中文额外条目\"");
    CHECK(updatedJson->entries[10].lead == "\n  ");
    CHECK(updatedJson->entries[9].lead == "\n  ");
    CHECK(updatedJson->entries[9].raw == "\"item.demo.new_only.name\" : \"仅存在于新文件\"");

    std::printf("== 更新校验与失败保护 ==\n");
    const std::wstring guardJson = PathJoin(tmpDir, L"zh_cn_guard.json");
    CHECK(CopyFileW(sampleJsonTarget.c_str(), guardJson.c_str(), FALSE) != FALSE);
    std::string guardBefore;
    CHECK(mcd::ReadFileBytes(guardJson, guardBefore, mcd::kMaxFileBytes, err));

    mcd::PastedBlock missingBlock;
    CHECK(mcd::ParsePastedBlock(
        "\"block.demo.new_block.name\": \"新方块\",\n"
        "\"gui.demo.close\": \"关闭界面\",\n"
        "\"item.demo.pickaxe.name\": \"演示镐（强化）\"",
        mcd::FileFormat::Json, missingBlock, err));
    mcd::UpdateStats ignoredStats;
    CHECK(!mcd::ApplyTranslationUpdate(guardJson, *jsonDiff, missingBlock, ignoredStats, err));
    CHECK_CONTAINS(err, "缺少");
    CHECK_CONTAINS(err, "item.demo.new_only.name");
    std::string guardAfter;
    CHECK(mcd::ReadFileBytes(guardJson, guardAfter, mcd::kMaxFileBytes, err));
    CHECK(guardBefore == guardAfter);

    mcd::PastedBlock unknownBlock;
    CHECK(mcd::ParsePastedBlock(updateJsonText + ",\n\"item.demo.gem.name\": \"红宝石\"",
                                mcd::FileFormat::Json, unknownBlock, err));
    CHECK(!mcd::ApplyTranslationUpdate(guardJson, *jsonDiff, unknownBlock, ignoredStats, err));
    CHECK_CONTAINS(err, "不在新增/值变更");
    CHECK(mcd::ReadFileBytes(guardJson, guardAfter, mcd::kMaxFileBytes, err));
    CHECK(guardBefore == guardAfter);

    mcd::UpdateStats mismatchStats;
    CHECK(!mcd::ApplyTranslationUpdate(samples + L"\\zh_cn.lang", *jsonDiff, updateJson,
                                       mismatchStats, err));
    CHECK_CONTAINS(err, "格式不一致");

    std::printf("== 更新写入：无 BOM ==\n");
    const std::wstring bomTarget = PathJoin(tmpDir, L"zh_cn_bom.json");
    std::string sampleJsonBytes;
    CHECK(mcd::ReadFileBytes(sampleJsonTarget, sampleJsonBytes, mcd::kMaxFileBytes, err));
    CHECK(WriteBytes(bomTarget, std::string("\xEF\xBB\xBF") + sampleJsonBytes));
    mcd::UpdateStats bomStats;
    CHECK(mcd::ApplyTranslationUpdate(bomTarget, *jsonDiff, updateJson, bomStats, err));
    std::string bomBytes;
    CHECK(mcd::ReadFileBytes(bomTarget, bomBytes, mcd::kMaxFileBytes, err));
    CHECK(!(bomBytes.size() >= 3 && static_cast<unsigned char>(bomBytes[0]) == 0xEF &&
            static_cast<unsigned char>(bomBytes[1]) == 0xBB &&
            static_cast<unsigned char>(bomBytes[2]) == 0xBF));
    CHECK_CONTAINS(bomBytes, "\"item.demo.new_only.name\" : \"仅存在于新文件\"");

    std::printf("== 更新操作文件（LANG，CRLF 与注释保留） ==\n");
    const std::wstring sampleLangTarget = samples + L"\\zh_cn.lang";
    const std::wstring targetLang = PathJoin(tmpDir, L"zh_cn.lang");
    std::string sampleLangBytes;
    CHECK(mcd::ReadFileBytes(sampleLangTarget, sampleLangBytes, mcd::kMaxFileBytes, err));
    std::string crlfBytes;
    crlfBytes.reserve(sampleLangBytes.size() + sampleLangBytes.size() / 8);
    for (size_t i = 0; i < sampleLangBytes.size(); ++i) {
        const char c = sampleLangBytes[i];
        if (c == '\n' && (i == 0 || sampleLangBytes[i - 1] != '\r')) {
            crlfBytes.push_back('\r');
        }
        crlfBytes.push_back(c);
    }
    CHECK(WriteBytes(targetLang, crlfBytes));

    mcd::PastedBlock updateLang;
    const std::string updateLangText =
        "item.demo.pickaxe.name=演示镐（强化）\n"
        "block.demo.new_block.name=新方块\n"
        "gui.demo.close=关闭界面\n"
        "gui.demo.settings=设置\n"
        "item.demo.new_only.name=仅存在于新文件\n";
    CHECK(mcd::ParsePastedBlock(updateLangText, mcd::FileFormat::Lang, updateLang, err));
    CHECK_SIZE(updateLang.entries.size(), 5);

    mcd::UpdateStats langStats;
    CHECK(mcd::ApplyTranslationUpdate(targetLang, *langDiff, updateLang, langStats, err));
    CHECK_SIZE(langStats.inserted, 4);
    CHECK_SIZE(langStats.updated, 1);
    CHECK_SIZE(langStats.removed, 2);
    CHECK_SIZE(langStats.untouched, 6);
    CHECK_SIZE(langStats.totalAfter, 11);

    std::string updatedLangBytes;
    CHECK(mcd::ReadFileBytes(targetLang, updatedLangBytes, mcd::kMaxFileBytes, err));
    size_t loneLf = 0;
    for (size_t i = 0; i < updatedLangBytes.size(); ++i) {
        if (updatedLangBytes[i] == '\n' && (i == 0 || updatedLangBytes[i - 1] != '\r')) {
            ++loneLf;
        }
    }
    CHECK_SIZE(loneLf, 0);
    CHECK_CONTAINS(updatedLangBytes, "## zh_cn 操作文件示例");
    CHECK_CONTAINS(updatedLangBytes, "# 更新时会保留注释与未涉及的条目");
    CHECK_CONTAINS(updatedLangBytes,
                   "item.demo.sword.name=演示之剑\r\nitem.demo.pickaxe.name=演示镐（强化）\r\n"
                   "item.demo.gem.name=红宝石");
    CHECK_CONTAINS(updatedLangBytes,
                   "gui.demo.title=演示界面\r\ngui.demo.close=关闭界面\r\n"
                   "gui.demo.settings=设置\r\nmessage.demo.welcome=欢迎来到演示世界！");
    CHECK_CONTAINS(updatedLangBytes,
                   "message.demo.welcome=欢迎来到演示世界！\r\nitem.demo.new_only.name=仅存在于新文件\r\n"
                   "item.demo.extra.name=中文额外条目");
    CHECK_NOT_CONTAINS(updatedLangBytes, "gui.demo.legacy");
    CHECK_NOT_CONTAINS(updatedLangBytes, "item.demo.old_only.name");

    auto updatedLang = std::make_unique<mcd::LangFile>();
    CHECK(mcd::LoadLangFile(targetLang, mcd::FileFormat::Lang, *updatedLang, err));
    CHECK_SIZE(updatedLang->entries.size(), 11);
    CHECK(updatedLang->entries[1].key == "item.demo.pickaxe.name");
    CHECK(updatedLang->entries[1].value == "演示镐（强化）");

    std::printf("== 更新操作文件（LANG，保留无末尾换行） ==\n");
    const std::wstring noEolLang = PathJoin(tmpDir, L"zh_cn_no_eol.lang");
    std::string noEolBytes = sampleLangBytes;
    while (!noEolBytes.empty() && (noEolBytes.back() == '\n' || noEolBytes.back() == '\r')) {
        noEolBytes.pop_back();
    }
    CHECK(WriteBytes(noEolLang, noEolBytes));
    mcd::UpdateStats noEolStats;
    CHECK(mcd::ApplyTranslationUpdate(noEolLang, *langDiff, updateLang, noEolStats, err));
    std::string noEolAfter;
    CHECK(mcd::ReadFileBytes(noEolLang, noEolAfter, mcd::kMaxFileBytes, err));
    CHECK(!noEolAfter.empty() && noEolAfter.back() != '\n');
    CHECK_CONTAINS(noEolAfter, "item.demo.extra.name=中文额外条目");

    std::printf("== 错误处理 ==\n");
    std::unique_ptr<mcd::DiffResult> mismatch;
    CHECK(!mcd::CompareFiles(oldJsonPath, newLangPath, mismatch, err));
    CHECK_CONTAINS(err, "格式不一致");

    std::unique_ptr<mcd::DiffResult> sameFile;
    CHECK(!mcd::CompareFiles(oldJsonPath, oldJsonPath, sameFile, err));
    CHECK_CONTAINS(err, "同一文件");

    const std::wstring emptyPath = PathJoin(tmpDir, L"empty.json");
    CHECK(WriteBytes(emptyPath, ""));
    {
        mcd::LangFile file;
        CHECK(!mcd::LoadLangFile(emptyPath, mcd::FileFormat::Unknown, file, err));
        CHECK_CONTAINS(err, "文件为空");
    }

    const std::wstring badUtf8Path = PathJoin(tmpDir, L"bad_utf8.lang");
    CHECK(WriteBytes(badUtf8Path, std::string("a=") + "\xFF\xFE" + "\n"));
    {
        mcd::LangFile file;
        CHECK(!mcd::LoadLangFile(badUtf8Path, mcd::FileFormat::Unknown, file, err));
        CHECK_CONTAINS(err, "UTF-8");
    }

    const std::wstring utf16Path = PathJoin(tmpDir, L"utf16.lang");
    CHECK(WriteBytes(utf16Path, std::string("\xFF\xFE\x61\x00", 4)));
    {
        mcd::LangFile file;
        CHECK(!mcd::LoadLangFile(utf16Path, mcd::FileFormat::Unknown, file, err));
        CHECK_CONTAINS(err, "UTF-16");
    }

    const std::wstring brokenJsonPath = PathJoin(tmpDir, L"broken.json");
    CHECK(WriteBytes(brokenJsonPath, "{\n  \"a\": \"b\",\n}\n"));
    {
        mcd::LangFile file;
        CHECK(!mcd::LoadLangFile(brokenJsonPath, mcd::FileFormat::Unknown, file, err));
        CHECK_CONTAINS(err, "JSON 解析失败");
        CHECK_CONTAINS(err, "逗号");
    }

    const std::wstring nonStringJsonPath = PathJoin(tmpDir, L"non_string.json");
    CHECK(WriteBytes(nonStringJsonPath, "{\n  \"a\": 1\n}\n"));
    {
        mcd::LangFile file;
        CHECK(!mcd::LoadLangFile(nonStringJsonPath, mcd::FileFormat::Unknown, file, err));
        CHECK_CONTAINS(err, "值必须是字符串");
    }

    const std::wstring trailingJsonPath = PathJoin(tmpDir, L"trailing.json");
    CHECK(WriteBytes(trailingJsonPath, "{}\nxyz\n"));
    {
        mcd::LangFile file;
        CHECK(!mcd::LoadLangFile(trailingJsonPath, mcd::FileFormat::Unknown, file, err));
        CHECK_CONTAINS(err, "多余内容");
    }

    const std::wstring noEqualsPath = PathJoin(tmpDir, L"no_equals.lang");
    CHECK(WriteBytes(noEqualsPath, "key_without_separator\n"));
    {
        mcd::LangFile file;
        CHECK(!mcd::LoadLangFile(noEqualsPath, mcd::FileFormat::Unknown, file, err));
        CHECK_CONTAINS(err, "缺少 '='");
    }

    const std::wstring duplicatePath = PathJoin(tmpDir, L"duplicate.lang");
    CHECK(WriteBytes(duplicatePath, "a=1\na=2\n"));
    {
        mcd::LangFile file;
        CHECK(!mcd::LoadLangFile(duplicatePath, mcd::FileFormat::Unknown, file, err));
        CHECK_CONTAINS(err, "重复的键");
    }

    const std::wstring unknownPath = PathJoin(tmpDir, L"unknown.txt");
    CHECK(WriteBytes(unknownPath, "a=b\n"));
    {
        mcd::LangFile file;
        CHECK(!mcd::LoadLangFile(unknownPath, mcd::FileFormat::Unknown, file, err));
        CHECK_CONTAINS(err, "格式不支持");
    }

    const std::wstring missingPath = PathJoin(tmpDir, L"missing.json");
    DeleteFileW(missingPath.c_str());
    {
        mcd::LangFile file;
        CHECK(!mcd::LoadLangFile(missingPath, mcd::FileFormat::Unknown, file, err));
        CHECK_CONTAINS(err, "无法打开文件");
    }

    std::printf("== UTF-8 工具 ==\n");
    CHECK(mcd::IsValidUtf8("中文 abc ⛏"));
    CHECK(!mcd::IsValidUtf8("\xC0\xAF"));
    CHECK(!mcd::IsValidUtf8("\xED\xA0\x80"));
    CHECK(mcd::Utf8ToWide("中文") == L"中文");
    CHECK(mcd::WideToUtf8(L"中文") == "中文");
    CHECK(mcd::TrimAscii("  a b \r\n") == "a b");
    CHECK(mcd::ToCrlf("a\nb") == "a\r\nb");
    CHECK(mcd::ToCrlf("a\r\nb") == "a\r\nb");
    CHECK(mcd::ToCrlf(L"a\nb") == L"a\r\nb");

    std::printf("\n检查项：%d，失败：%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

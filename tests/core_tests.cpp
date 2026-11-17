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

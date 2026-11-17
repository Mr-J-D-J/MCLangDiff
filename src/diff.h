#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lang_file.h"

namespace mcd {

struct ChangePair {
    uint32_t oldIndex = 0;
    uint32_t newIndex = 0;
};

// 比较结果：条目索引 + 预生成的报告文本。
struct DiffResult {
    FileFormat format = FileFormat::Unknown;
    std::string oldPath;
    std::string newPath;
    uint64_t oldBytes = 0;
    uint64_t newBytes = 0;
    size_t oldKeyCount = 0;
    size_t newKeyCount = 0;

    std::vector<uint32_t> added;      // 新文件中的索引，保持新文件出现顺序
    std::vector<uint32_t> removed;    // 旧文件中的索引，保持旧文件出现顺序
    std::vector<ChangePair> changed;  // 保持新文件出现顺序
    size_t unchanged = 0;

    // 纯净条目区（不含统计信息、标题或说明文字）。
    std::string blockAdded;
    std::string blockRemoved;
    std::string blockChanged;

    // 统计信息（独立于纯净条目区）。
    std::string statsAdded;
    std::string statsRemoved;
    std::string statsChanged;

    std::string fullReport;
    double elapsedMs = 0.0;

    // 保留解析后的源文件，供“更新 zh_cn 操作文件”按新文件位置注入条目。
    std::unique_ptr<LangFile> oldFile;
    std::unique_ptr<LangFile> newFile;
};

// 读取并比较两个语言文件。成功时 out 指向新结果，失败时 err 为 UTF-8 错误信息。
bool CompareFiles(const std::wstring& oldPath, const std::wstring& newPath,
                  std::unique_ptr<DiffResult>& out, std::string& err);

}  // namespace mcd

#include "lang_file.h"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <vector>

#include "util.h"

namespace mcd {
namespace {

bool IsIndentOnly(std::string_view text) {
    for (const char c : text) {
        if (c != ' ' && c != '\t') {
            return false;
        }
    }
    return true;
}

void EncodeUtf8(uint32_t codePoint, std::string& out) {
    if (codePoint < 0x80) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// 扁平 JSON 对象解析器：仅接受 {"key": "value", ...}，保留每条目的原文片段。
class JsonParser {
public:
    JsonParser(std::string_view text, LangFile& file) : text_(text), file_(file) {}

    bool Parse(std::string& err) {
        if (!ParseDocument()) {
            err = "JSON 解析失败（第 " + std::to_string(line_) + " 行）：" + error_;
            return false;
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
                lineStart_ = pos_;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool ParseDocument() {
        SkipWhitespace();
        if (pos_ >= text_.size() || text_[pos_] != '{') {
            return Fail("顶层必须是对象，并以 '{' 开头");
        }
        ++pos_;
        file_.prefix = text_.substr(0, pos_);
        size_t leadStart = pos_;

        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            file_.suffix = text_.substr(leadStart);
            ++pos_;
            SkipWhitespace();
            return pos_ == text_.size() ? true : Fail("对象结束后存在多余内容");
        }

        size_t lastRawEnd = leadStart;
        while (true) {
            const size_t keyStart = pos_;
            std::string_view key;
            if (!ParseString(key)) {
                return false;
            }
            std::string_view indent = text_.substr(lineStart_, keyStart - lineStart_);
            if (!IsIndentOnly(indent)) {
                indent = std::string_view();
            }

            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                return Fail("键后缺少 ':'");
            }
            ++pos_;
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return Fail("值必须是字符串（语言文件仅支持扁平的字符串键值对）");
            }

            std::string_view value;
            if (!ParseString(value)) {
                return false;
            }
            const size_t rawEnd = pos_;

            Entry entry;
            entry.key = key;
            entry.value = value;
            entry.raw = text_.substr(keyStart, rawEnd - keyStart);
            entry.indent = indent;
            entry.lead = text_.substr(leadStart, keyStart - leadStart);
            entry.line = line_;
            file_.entries.push_back(entry);
            lastRawEnd = rawEnd;

            SkipWhitespace();
            if (pos_ >= text_.size()) {
                return Fail("对象没有正确结束（缺少 '}'）");
            }
            if (text_[pos_] == ',') {
                ++pos_;
                leadStart = pos_;
                SkipWhitespace();
                if (pos_ < text_.size() && text_[pos_] == '}') {
                    return Fail("对象末尾存在多余的逗号");
                }
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                break;
            }
            return Fail("键值对之间缺少 ','，或对象缺少 '}'");
        }

        file_.suffix = text_.substr(lastRawEnd);
        SkipWhitespace();
        if (pos_ != text_.size()) {
            return Fail("对象结束后存在多余内容");
        }
        return true;
    }

    bool ParseString(std::string_view& out) {
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
            out = content;
            return true;
        }

        std::string decoded;
        decoded.reserve(content.size());
        if (!DecodeEscapes(content, decoded)) {
            return false;
        }
        file_.decoded.emplace_back(std::move(decoded));
        out = file_.decoded.back();
        return true;
    }

    bool DecodeEscapes(std::string_view content, std::string& out) {
        std::string decodeError;
        if (!DecodeJsonEscapes(content, out, decodeError)) {
            return Fail(decodeError);
        }
        return true;
    }

    std::string_view text_;
    LangFile& file_;
    size_t pos_ = 0;
    size_t lineStart_ = 0;
    uint32_t line_ = 1;
    std::string error_;
};

bool ParseLangText(std::string_view text, LangFile& file, std::string& err) {
    size_t pos = 0;
    uint32_t lineNumber = 0;
    while (true) {
        const size_t newline = text.find('\n', pos);
        const size_t end = newline == std::string_view::npos ? text.size() : newline;
        std::string_view raw = text.substr(pos, end - pos);
        if (!raw.empty() && raw.back() == '\r') {
            raw.remove_suffix(1);
        }
        ++lineNumber;

        const std::string_view trimmed = TrimAscii(raw);
        const bool isComment =
            !trimmed.empty() &&
            (trimmed.front() == '#' || (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/'));
        if (!trimmed.empty() && !isComment) {
            const size_t equals = raw.find('=');
            if (equals == std::string_view::npos) {
                err = "LANG 解析失败（第 " + std::to_string(lineNumber) + " 行）：缺少 '=' 分隔符";
                return false;
            }
            const std::string_view key = TrimAscii(raw.substr(0, equals));
            if (key.empty()) {
                err = "LANG 解析失败（第 " + std::to_string(lineNumber) + " 行）：键为空";
                return false;
            }
            Entry entry;
            entry.key = key;
            entry.value = raw.substr(equals + 1);
            entry.raw = raw;
            entry.line = lineNumber;
            file.entries.push_back(entry);
        }

        if (newline == std::string_view::npos) {
            break;
        }
        pos = newline + 1;
    }
    return true;
}

bool BuildIndex(LangFile& file, std::string& err) {
    file.index.reserve(file.entries.size() * 2 + 1);
    for (uint32_t i = 0; i < file.entries.size(); ++i) {
        const auto inserted = file.index.emplace(file.entries[i].key, i);
        if (!inserted.second) {
            const Entry& first = file.entries[inserted.first->second];
            err = "第 " + std::to_string(file.entries[i].line) + " 行：重复的键 \"" +
                  std::string(file.entries[i].key) + "\"（首次出现于第 " + std::to_string(first.line) + " 行）";
            return false;
        }
    }
    return true;
}

std::wstring ResolveFinalPath(const std::wstring& path) {
    const HANDLE handle =
        CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        std::vector<wchar_t> buffer(32768);
        const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(),
                                                       static_cast<DWORD>(buffer.size()),
                                                       FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        CloseHandle(handle);
        if (length > 0 && length < buffer.size()) {
            std::wstring resolved(buffer.data(), length);
            if (resolved.rfind(L"\\\\?\\", 0) == 0) {
                resolved.erase(0, 4);
            }
            return resolved;
        }
    }

    std::vector<wchar_t> buffer(32768);
    const DWORD length =
        GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length > 0 && length < buffer.size()) {
        return std::wstring(buffer.data(), length);
    }
    return path;
}

}  // namespace

bool DecodeJsonEscapes(std::string_view content, std::string& out, std::string& err) {
    size_t i = 0;
    while (i < content.size()) {
        const char c = content[i++];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i >= content.size()) {
            err = "字符串以孤立的反斜杠结尾";
            return false;
        }

        const char escape = content[i++];
        switch (escape) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                auto readHex4 = [&](uint32_t& value) -> bool {
                    if (i + 4 > content.size()) {
                        err = "\\u 转义缺少 4 位十六进制数字";
                        return false;
                    }
                    uint32_t result = 0;
                    for (int k = 0; k < 4; ++k) {
                        const int digit = HexDigit(content[i + static_cast<size_t>(k)]);
                        if (digit < 0) {
                            err = "\\u 转义包含非十六进制字符";
                            return false;
                        }
                        result = (result << 4) | static_cast<uint32_t>(digit);
                    }
                    i += 4;
                    value = result;
                    return true;
                };

                uint32_t codePoint = 0;
                if (!readHex4(codePoint)) {
                    return false;
                }
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                    // 高代理项：必须紧跟 \uXXXX 形式的低代理项。
                    if (i + 2 > content.size() || content[i] != '\\' || content[i + 1] != 'u') {
                        err = "无效的 \\u 转义：高代理项缺少配对的低代理项";
                        return false;
                    }
                    i += 2;
                    uint32_t low = 0;
                    if (!readHex4(low)) {
                        return false;
                    }
                    if (low < 0xDC00 || low > 0xDFFF) {
                        err = "无效的 \\u 转义：低代理项超出范围";
                        return false;
                    }
                    codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                    err = "无效的 \\u 转义：孤立的低代理项";
                    return false;
                }
                EncodeUtf8(codePoint, out);
                break;
            }
            default:
                err = std::string("无效的转义序列 \\") + escape;
                return false;
        }
    }
    return true;
}

FileFormat FormatFromPath(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return FileFormat::Unknown;
    }
    std::wstring extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (extension == L".json") {
        return FileFormat::Json;
    }
    if (extension == L".lang") {
        return FileFormat::Lang;
    }
    return FileFormat::Unknown;
}

const char* FormatName(FileFormat format) {
    switch (format) {
        case FileFormat::Json: return "JSON";
        case FileFormat::Lang: return "LANG";
        default: return "未知";
    }
}

bool ReadFileBytes(const std::wstring& path, std::string& out, uint64_t maxBytes, std::string& err) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        err = "无法打开文件：" + WideToUtf8(Win32ErrorText(GetLastError()));
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size)) {
        err = "无法获取文件大小：" + WideToUtf8(Win32ErrorText(GetLastError()));
        CloseHandle(file);
        return false;
    }
    if (size.QuadPart == 0) {
        err = "文件为空（0 字节）";
        CloseHandle(file);
        return false;
    }
    const uint64_t byteSize = static_cast<uint64_t>(size.QuadPart);
    if (byteSize > maxBytes) {
        err = "文件过大：" + WideToUtf8(FormatSize(byteSize)) + "（上限 " +
              WideToUtf8(FormatSize(maxBytes)) + "）";
        CloseHandle(file);
        return false;
    }

    out.clear();
    out.resize(static_cast<size_t>(byteSize));
    size_t offset = 0;
    while (offset < out.size()) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(out.size() - offset, 4u * 1024u * 1024u));
        DWORD read = 0;
        if (!ReadFile(file, out.data() + offset, chunk, &read, nullptr)) {
            err = "读取文件失败：" + WideToUtf8(Win32ErrorText(GetLastError()));
            CloseHandle(file);
            return false;
        }
        if (read == 0) {
            break;
        }
        offset += read;
    }
    CloseHandle(file);

    if (offset != out.size()) {
        out.resize(offset);
    }
    if (out.empty()) {
        err = "文件为空（读取到 0 字节）";
        return false;
    }
    return true;
}

bool LoadLangFile(const std::wstring& path, FileFormat expected, LangFile& out, std::string& err) {
    const FileFormat format = FormatFromPath(path);
    if (format == FileFormat::Unknown) {
        err = "格式不支持：仅支持 .json 与 .lang 文件";
        return false;
    }
    if (expected != FileFormat::Unknown && expected != format) {
        err = std::string("格式不一致：该文件为 ") + FormatName(format) + " 格式";
        return false;
    }

    std::string bytes;
    if (!ReadFileBytes(path, bytes, kMaxFileBytes, err)) {
        return false;
    }

    // 编码识别：先处理 Unicode BOM，再按 UTF-8 严格校验。
    const unsigned char* head = reinterpret_cast<const unsigned char*>(bytes.data());
    const size_t size = bytes.size();
    const bool utf32Le = size >= 4 && head[0] == 0xFF && head[1] == 0xFE && head[2] == 0x00 && head[3] == 0x00;
    const bool utf32Be = size >= 4 && head[0] == 0x00 && head[1] == 0x00 && head[2] == 0xFE && head[3] == 0xFF;
    const bool utf16Le = size >= 2 && head[0] == 0xFF && head[1] == 0xFE;
    const bool utf16Be = size >= 2 && head[0] == 0xFE && head[1] == 0xFF;
    if (utf32Le || utf32Be) {
        err = "编码异常：文件为 UTF-32 编码，请另存为 UTF-8 后重试";
        return false;
    }
    if (utf16Le || utf16Be) {
        err = "编码异常：文件为 UTF-16 编码，请另存为 UTF-8 后重试";
        return false;
    }
    if (size >= 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) {
        bytes.erase(0, 3);
    }
    if (bytes.empty()) {
        err = "文件为空（仅包含 UTF-8 BOM）";
        return false;
    }
    if (bytes.find('\0') != std::string::npos) {
        err = "编码异常：文件包含 NUL 字符，可能不是 UTF-8 文本";
        return false;
    }
    size_t badOffset = 0;
    if (!IsValidUtf8(bytes, &badOffset)) {
        err = "编码异常：偏移 " + std::to_string(badOffset) + " 处不是合法的 UTF-8 序列";
        return false;
    }

    out.format = format;
    out.path = WideToUtf8(path);
    out.buffer = std::move(bytes);
    out.byteSize = out.buffer.size();

    const std::string_view text(out.buffer);
    if (format == FileFormat::Json) {
        JsonParser parser(text, out);
        if (!parser.Parse(err)) {
            return false;
        }
    } else {
        if (!ParseLangText(text, out, err)) {
            return false;
        }
    }

    if (!BuildIndex(out, err)) {
        err = std::string(FormatName(format)) + " 解析失败：" + err;
        return false;
    }
    return true;
}

bool IsSameFile(const std::wstring& first, const std::wstring& second) {
    if (first.empty() || second.empty()) {
        return false;
    }
    return _wcsicmp(ResolveFinalPath(first).c_str(), ResolveFinalPath(second).c_str()) == 0;
}

}  // namespace mcd

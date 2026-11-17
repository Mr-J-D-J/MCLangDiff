#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mcd {

// UTF-8 <-> UTF-16 转换（Windows 内部宽字符为 UTF-16）。
std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);

// 严格校验 UTF-8（拒绝过长编码、代理区码点、越界码点）。
// 失败时通过 badOffset 返回首个非法字节的位置。
bool IsValidUtf8(std::string_view text, size_t* badOffset = nullptr);

// 去除首尾的 ASCII 空白（空格、TAB、CR、LF）。
std::string_view TrimAscii(std::string_view text);

// 人类可读的文件大小，例如 "12.3 KB"。
std::wstring FormatSize(uint64_t bytes);

// 本地时间的 "YYYY-MM-DD HH:MM:SS"（ASCII）。
std::string FormatTimestamp();

// 把 LF 换行转换为 Windows 文本控件/剪贴板更友好的 CRLF（已是 CRLF 的保持不变）。
std::string ToCrlf(std::string_view text);
std::wstring ToCrlf(std::wstring_view text);

// 以 UTF-8 写出文本文件，可选写入 BOM；失败时返回 false 并填充 err。
bool WriteUtf8File(const std::wstring& path, std::string_view text, bool withBom, std::string& err);

// Win32 错误码对应的可读文本。
std::wstring Win32ErrorText(unsigned long code);

}  // namespace mcd

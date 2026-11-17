#include "util.h"

#include <windows.h>

#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

namespace mcd {

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int length = static_cast<int>(text.size() > static_cast<size_t>(INT_MAX) ? INT_MAX : text.size());
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
    if (needed <= 0) {
        return std::wstring();
    }
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), length, result.data(), needed);
    return result;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return std::string();
    }
    const int length = static_cast<int>(text.size() > static_cast<size_t>(INT_MAX) ? INT_MAX : text.size());
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return std::string();
    }
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), needed, nullptr, nullptr);
    return result;
}

bool IsValidUtf8(std::string_view text, size_t* badOffset) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const size_t size = text.size();
    size_t i = 0;
    while (i < size) {
        const unsigned char lead = bytes[i];
        if (lead < 0x80) {
            ++i;
            continue;
        }

        int extra = 0;
        uint32_t codePoint = 0;
        if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            codePoint = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            codePoint = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            codePoint = lead & 0x07u;
        } else {
            if (badOffset != nullptr) {
                *badOffset = i;
            }
            return false;
        }

        if (i + static_cast<size_t>(extra) >= size) {
            if (badOffset != nullptr) {
                *badOffset = i;
            }
            return false;
        }
        for (int k = 1; k <= extra; ++k) {
            const unsigned char continuation = bytes[i + static_cast<size_t>(k)];
            if ((continuation & 0xC0) != 0x80) {
                if (badOffset != nullptr) {
                    *badOffset = i;
                }
                return false;
            }
            codePoint = (codePoint << 6) | (continuation & 0x3Fu);
        }

        const bool overlong = (extra == 1 && codePoint < 0x80) || (extra == 2 && codePoint < 0x800) ||
                              (extra == 3 && codePoint < 0x10000);
        const bool surrogate = codePoint >= 0xD800 && codePoint <= 0xDFFF;
        if (overlong || surrogate || codePoint > 0x10FFFF) {
            if (badOffset != nullptr) {
                *badOffset = i;
            }
            return false;
        }
        i += static_cast<size_t>(extra) + 1;
    }
    return true;
}

std::string_view TrimAscii(std::string_view text) {
    while (!text.empty()) {
        const char c = text.front();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            text.remove_prefix(1);
        } else {
            break;
        }
    }
    while (!text.empty()) {
        const char c = text.back();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            text.remove_suffix(1);
        } else {
            break;
        }
    }
    return text;
}

std::wstring FormatSize(uint64_t bytes) {
    std::wostringstream stream;
    if (bytes < 1024) {
        stream << bytes << L" B";
    } else if (bytes < 1024ull * 1024ull) {
        stream << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / 1024.0) << L" KB";
    } else if (bytes < 1024ull * 1024ull * 1024ull) {
        stream << std::fixed << std::setprecision(1)
               << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << L" MB";
    } else {
        stream << std::fixed << std::setprecision(2)
               << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << L" GB";
    }
    return stream.str();
}

std::string FormatTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return std::string(buffer);
}

std::wstring Win32ErrorText(unsigned long code) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result;
    if (length != 0 && message != nullptr) {
        result.assign(message, length);
        LocalFree(message);
        while (!result.empty() && (result.back() == L'\n' || result.back() == L'\r' || result.back() == L' ')) {
            result.pop_back();
        }
    }
    if (result.empty()) {
        result = L"错误码 " + std::to_wstring(code);
    }
    return result;
}

std::string ToCrlf(std::string_view text) {
    std::string result;
    result.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n' && (i == 0 || text[i - 1] != '\r')) {
            result.push_back('\r');
        }
        result.push_back(c);
    }
    return result;
}

std::wstring ToCrlf(std::wstring_view text) {
    std::wstring result;
    result.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (c == L'\n' && (i == 0 || text[i - 1] != L'\r')) {
            result.push_back(L'\r');
        }
        result.push_back(c);
    }
    return result;
}

bool WriteUtf8File(const std::wstring& path, std::string_view text, bool withBom, std::string& err) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        err = WideToUtf8(L"无法写入文件：" + Win32ErrorText(GetLastError()));
        return false;
    }

    bool ok = true;
    if (withBom) {
        const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
        DWORD written = 0;
        if (!WriteFile(file, bom, sizeof(bom), &written, nullptr) || written != sizeof(bom)) {
            ok = false;
        }
    }

    size_t offset = 0;
    while (ok && offset < text.size()) {
        const size_t remaining = text.size() - offset;
        const DWORD chunk = static_cast<DWORD>(remaining > 1024u * 1024u ? 1024u * 1024u : remaining);
        DWORD written = 0;
        if (!WriteFile(file, text.data() + offset, chunk, &written, nullptr) || written != chunk) {
            ok = false;
            break;
        }
        offset += written;
    }

    if (ok && !FlushFileBuffers(file)) {
        ok = false;
    }
    const DWORD lastError = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        err = WideToUtf8(L"写入报告失败：" + Win32ErrorText(lastError));
        return false;
    }
    return true;
}

}  // namespace mcd

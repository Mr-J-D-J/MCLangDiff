#include <windows.h>

#include <shellapi.h>

#include <cstdio>
#include <cwchar>
#include <memory>
#include <string>

#include "app.h"
#include "diff.h"
#include "util.h"

namespace {

void AttachToParentConsole() {
    if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
        return;
    }
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}

void PrintUsage() {
    std::fwprintf(stdout,
                  L"MCLangDiff 1.0 — Minecraft 语言文件差异比较\n"
                  L"\n"
                  L"用法：\n"
                  L"  MCLangDiff.exe                                启动图形界面\n"
                  L"  MCLangDiff.exe --text <旧文件> <新文件> <输出.txt>\n"
                  L"                                                生成 UTF-8 报告（带 BOM）\n"
                  L"  MCLangDiff.exe --help                         显示本帮助\n"
                  L"\n"
                  L"退出码：0 成功，2 比较失败，3 报告写入失败。\n");
}

int RunCommandLine(int argc, wchar_t** argv) {
    if (argc == 2 && (std::wcscmp(argv[1], L"--help") == 0 || std::wcscmp(argv[1], L"-h") == 0)) {
        PrintUsage();
        return 0;
    }
    if (argc == 5 && std::wcscmp(argv[1], L"--text") == 0) {
        std::unique_ptr<mcd::DiffResult> result;
        std::string error;
        if (!mcd::CompareFiles(argv[2], argv[3], result, error)) {
            std::fwprintf(stderr, L"比较失败：%ls\n", mcd::Utf8ToWide(error).c_str());
            return 2;
        }
        const std::string report = mcd::ToCrlf(result->fullReport);
        if (!mcd::WriteUtf8File(argv[4], report, true, error)) {
            std::fwprintf(stderr, L"导出失败：%ls\n", mcd::Utf8ToWide(error).c_str());
            return 3;
        }
        std::fwprintf(stdout, L"报告已写入：%ls\n", argv[4]);
        return 0;
    }
    PrintUsage();
    return 2;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr && argc > 1) {
        AttachToParentConsole();
        const int exitCode = RunCommandLine(argc, argv);
        LocalFree(argv);
        return exitCode;
    }
    if (argv != nullptr) {
        LocalFree(argv);
    }
    return RunApp(instance, showCommand);
}

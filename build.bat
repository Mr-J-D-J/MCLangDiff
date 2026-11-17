@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
set "SRC=%ROOT%src"
set "OUT=%ROOT%bin"
set "OBJ=%ROOT%build"
set "CXX=g++"
set "WINDRES=windres"

where %CXX% >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ was not found in PATH. Install MinGW-w64 and add its bin folder to PATH.
    exit /b 1
)
where %WINDRES% >nul 2>nul
if errorlevel 1 (
    echo [ERROR] windres was not found in PATH. It ships with MinGW-w64.
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

echo [1/3] Compiling resources...
%WINDRES% -I "%SRC%" -i "%SRC%\app.rc" -o "%OBJ%\app_res.o" -O coff
if errorlevel 1 goto :fail

echo [2/3] Compiling sources...
%CXX% -std=c++17 -O2 -municode -mwindows -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 -finput-charset=UTF-8 -Wall -Wextra ^
    "%SRC%\main.cpp" "%SRC%\app.cpp" "%SRC%\lang_file.cpp" "%SRC%\diff.cpp" "%SRC%\util.cpp" ^
    "%OBJ%\app_res.o" -o "%OUT%\MCLangDiff.exe" ^
    -static -static-libgcc -static-libstdc++ -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luser32
if errorlevel 1 goto :fail

echo [3/3] Done: %OUT%\MCLangDiff.exe
exit /b 0

:fail
echo [ERROR] Build failed.
exit /b 1

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

// 创建并运行主窗口（图形界面）。
int RunApp(HINSTANCE instance, int showCommand);

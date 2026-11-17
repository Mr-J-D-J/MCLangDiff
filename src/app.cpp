#include "app.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "diff.h"
#include "lang_file.h"
#include "update_file.h"
#include "util.h"

namespace {

using mcd::DiffResult;
using mcd::FileFormat;

// ---------------------------------------------------------------- 常量与配色

enum ControlId : int {
    kIdZoneOld = 1001,
    kIdZoneNew = 1002,
    kIdZoneTarget = 1003,
    kIdClearOld = 1010,
    kIdClearNew = 1011,
    kIdSwap = 1012,
    kIdCompare = 1013,
    kIdClearTarget = 1014,
    kIdTab = 1020,
    kIdEditAdded = 1021,
    kIdEditRemoved = 1022,
    kIdEditChanged = 1023,
    kIdPasteEdit = 1024,
    kIdSyncTemplateEdit = 1025,
    kIdSyncPasteEdit = 1026,
    kIdCopyBlock = 1030,
    kIdCopyAll = 1031,
    kIdExport = 1032,
    kIdUpdateFile = 1033,
    kIdClearPaste = 1034,
    kIdCopyTemplate = 1035,
    kIdSyncFile = 1036,
    kIdProgress = 1040
};

constexpr UINT kMsgZoneFile = WM_APP + 1;
constexpr UINT kMsgZoneClick = WM_APP + 2;
constexpr UINT kMsgCompareDone = WM_APP + 3;

const wchar_t kZoneClassName[] = L"MCLangDiff.DropZone";
const wchar_t kMainClassName[] = L"MCLangDiff.Main";

constexpr COLORREF kBg = RGB(245, 246, 248);
constexpr COLORREF kPanel = RGB(255, 255, 255);
constexpr COLORREF kBorder = RGB(219, 224, 232);
constexpr COLORREF kText = RGB(24, 32, 44);
constexpr COLORREF kMuted = RGB(110, 120, 134);
constexpr COLORREF kOldAccent = RGB(217, 119, 6);
constexpr COLORREF kNewAccent = RGB(37, 99, 235);
constexpr COLORREF kGreen = RGB(21, 128, 61);
constexpr COLORREF kGreenHover = RGB(22, 101, 52);
constexpr COLORREF kGreenDisabled = RGB(190, 220, 203);
constexpr COLORREF kRed = RGB(185, 28, 28);
constexpr COLORREF kAmber = RGB(180, 83, 9);
constexpr COLORREF kPrimary = RGB(37, 99, 235);
constexpr COLORREF kPrimaryHover = RGB(29, 78, 216);
constexpr COLORREF kPrimaryDisabled = RGB(191, 208, 245);
constexpr COLORREF kTargetAccent = kGreen;

const wchar_t* const kUiFace = L"Microsoft YaHei UI";
const wchar_t* const kMonoFace = L"Consolas";

// ---------------------------------------------------------------- 数据结构

struct ZoneState {
    std::wstring path;
    std::wstring name;
    FileFormat format = FileFormat::Unknown;
    uint64_t bytes = 0;
    bool valid = false;
};

struct CompareJob {
    bool ok = false;
    std::string error;
    std::unique_ptr<DiffResult> result;
};

struct App;

struct ZoneContext {
    App* app = nullptr;
    int index = 0;
};

struct App {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;

    HWND zoneHwnd[3] = {};
    HWND btnClear[3] = {};
    HWND btnSwap = nullptr;
    HWND btnCompare = nullptr;
    HWND tab = nullptr;
    HWND edits[3] = {};
    HWND pasteEdit = nullptr;
    HWND syncTemplateEdit = nullptr;
    HWND syncPasteEdit = nullptr;
    HWND progress = nullptr;
    HWND btnCopyBlock = nullptr;
    HWND btnCopyAll = nullptr;
    HWND btnExport = nullptr;
    HWND btnUpdate = nullptr;
    HWND btnClearPaste = nullptr;
    HWND btnCopyTemplate = nullptr;
    HWND btnSyncFile = nullptr;

    ZoneState zones[3];
    std::unique_ptr<DiffResult> result;
    mcd::ConsistencyReport consistency;
    std::thread worker;
    bool comparing = false;
    bool compact = false;
    bool updateApplied = false;
    bool syncApplied = false;
    int activeTab = 0;

    RECT workArea{};
    UINT dpi = 96;
    HFONT fTitle = nullptr;
    HFONT fZoneName = nullptr;
    HFONT fZoneHint = nullptr;
    HFONT fUi = nullptr;
    HFONT fUiBold = nullptr;
    HFONT fButton = nullptr;
    HFONT fSmall = nullptr;
    HFONT fSmallBold = nullptr;
    HFONT fMono = nullptr;
    HFONT fPanelHint = nullptr;

    HBRUSH bgBrush = nullptr;
    HBRUSH panelBrush = nullptr;

    std::wstring statText[3];
    std::wstring dUpdateInfo;
    std::wstring syncInfo;
    std::wstring status;
    COLORREF statusColor = kText;

    bool zoneHover[3] = {false, false, false};
    std::unordered_map<HWND, bool> buttonHover;

    RECT rcHeader{};
    RECT rcZones[3]{};
    RECT rcStatus{};
    RECT rcProgress{};
    RECT rcTab{};
    RECT rcCard{};
    RECT rcStats{};
    RECT rcEdit{};
    RECT rcBottom{};
    RECT rcBottomHint{};
    RECT rcUpdate{};
    RECT rcClearPaste{};
    RECT rcSyncTemplateCaption{};
    RECT rcSyncTemplate{};
    RECT rcSyncPasteCaption{};
    RECT rcSyncPaste{};
    RECT rcSync{};
    RECT rcCopyTemplate{};

    ~App() {
        if (worker.joinable()) {
            worker.detach();
        }
        auto dropFont = [](HFONT& font) {
            if (font != nullptr) {
                DeleteObject(font);
                font = nullptr;
            }
        };
        dropFont(fTitle);
        dropFont(fZoneName);
        dropFont(fZoneHint);
        dropFont(fUi);
        dropFont(fUiBold);
        dropFont(fButton);
        dropFont(fSmall);
        dropFont(fSmallBold);
        dropFont(fMono);
        dropFont(fPanelHint);
        if (bgBrush != nullptr) {
            DeleteObject(bgBrush);
        }
        if (panelBrush != nullptr) {
            DeleteObject(panelBrush);
        }
    }
};

App* g_app = nullptr;

struct ZoneUiInfo {
    const wchar_t* badge;
    const wchar_t* title;
    const wchar_t* role;
    const wchar_t* emptyHint;
    COLORREF accent;
};

const ZoneUiInfo kZoneUi[3] = {
    {L"OLD", L"旧文件 / Old", L"en_us", L"拖入旧 en_us 文件", kOldAccent},
    {L"NEW", L"新文件 / New", L"en_us", L"拖入新 en_us 文件", kNewAccent},
    {L"ZH_CN", L"操作文件 / Target", L"zh_cn", L"拖入 zh_cn 操作文件", kTargetAccent},
};

// ---------------------------------------------------------------- 绘制辅助

int S(const App& app, int value) {
    return MulDiv(value, static_cast<int>(app.dpi), 96);
}

COLORREF Mix(COLORREF base, COLORREF overlay, int overlayPercent) {
    const int keep = 100 - overlayPercent;
    return RGB((GetRValue(base) * keep + GetRValue(overlay) * overlayPercent) / 100,
               (GetGValue(base) * keep + GetGValue(overlay) * overlayPercent) / 100,
               (GetBValue(base) * keep + GetBValue(overlay) * overlayPercent) / 100);
}

void FillSolidRect(HDC dc, const RECT& rc, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

void FrameRect1(HDC dc, RECT rc, COLORREF color) {
    const HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void FillRoundRect(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border) {
    const HBRUSH brush = CreateSolidBrush(fill);
    const HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawTextIn(HDC dc, std::wstring_view text, RECT rc, HFONT font, COLORREF color, UINT flags) {
    if (text.empty() || font == nullptr) {
        return;
    }
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rc, flags);
    SelectObject(dc, oldFont);
}

void DrawDot(HDC dc, int centerX, int centerY, int radius, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, centerX - radius, centerY - radius, centerX + radius, centerY + radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

std::wstring FormatLabel(FileFormat format) {
    switch (format) {
        case FileFormat::Json: return L"JSON";
        case FileFormat::Lang: return L"LANG";
        default: return L"未知";
    }
}

COLORREF TabAccent(int tab) {
    switch (tab) {
        case 1: return kRed;
        case 2: return kAmber;
        default: return kGreen;
    }
}

// ---------------------------------------------------------------- 字体

HFONT CreateFontFor(UINT dpi, int tenthPoints, bool bold, const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(tenthPoints, static_cast<int>(dpi), 720);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    return CreateFontIndirectW(&lf);
}

void CreateAppFonts(App& app) {
    const bool compact = app.compact;
    app.fTitle = CreateFontFor(app.dpi, compact ? 140 : 190, true, kUiFace);
    app.fZoneName = CreateFontFor(app.dpi, compact ? 90 : 112, true, kUiFace);
    app.fZoneHint = CreateFontFor(app.dpi, compact ? 90 : 105, true, kUiFace);
    app.fUi = CreateFontFor(app.dpi, compact ? 82 : 95, false, kUiFace);
    app.fUiBold = CreateFontFor(app.dpi, compact ? 82 : 95, true, kUiFace);
    app.fButton = CreateFontFor(app.dpi, compact ? 82 : 95, true, kUiFace);
    app.fSmall = CreateFontFor(app.dpi, compact ? 72 : 85, false, kUiFace);
    app.fSmallBold = CreateFontFor(app.dpi, compact ? 68 : 80, true, kUiFace);
    app.fMono = CreateFontFor(app.dpi, compact ? 88 : 100, false, kMonoFace);
    app.fPanelHint = CreateFontFor(app.dpi, compact ? 105 : 125, true, kUiFace);
}

void DestroyAppFonts(App& app) {
    auto dropFont = [](HFONT& font) {
        if (font != nullptr) {
            DeleteObject(font);
            font = nullptr;
        }
    };
    dropFont(app.fTitle);
    dropFont(app.fZoneName);
    dropFont(app.fZoneHint);
    dropFont(app.fUi);
    dropFont(app.fUiBold);
    dropFont(app.fButton);
    dropFont(app.fSmall);
    dropFont(app.fSmallBold);
    dropFont(app.fMono);
    dropFont(app.fPanelHint);
}

void ApplyControlFonts(App& app) {
    HWND buttons[] = {app.btnClear[0], app.btnClear[1], app.btnClear[2], app.btnSwap,
                      app.btnCompare,  app.btnCopyBlock, app.btnCopyAll, app.btnExport,
                      app.btnUpdate,   app.btnClearPaste, app.btnCopyTemplate, app.btnSyncFile};
    for (HWND button : buttons) {
        if (button != nullptr) {
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(app.fUiBold), TRUE);
        }
    }
    if (app.tab != nullptr) {
        SendMessageW(app.tab, WM_SETFONT, reinterpret_cast<WPARAM>(app.fUiBold), TRUE);
    }
    for (HWND edit : app.edits) {
        if (edit != nullptr) {
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(app.fMono), TRUE);
        }
    }
    if (app.pasteEdit != nullptr) {
        SendMessageW(app.pasteEdit, WM_SETFONT, reinterpret_cast<WPARAM>(app.fMono), TRUE);
    }
    if (app.syncTemplateEdit != nullptr) {
        SendMessageW(app.syncTemplateEdit, WM_SETFONT, reinterpret_cast<WPARAM>(app.fMono), TRUE);
    }
    if (app.syncPasteEdit != nullptr) {
        SendMessageW(app.syncPasteEdit, WM_SETFONT, reinterpret_cast<WPARAM>(app.fMono), TRUE);
    }
}

// ---------------------------------------------------------------- 状态与提示

void SetStatus(App& app, std::wstring text, COLORREF color = kText) {
    app.status = std::move(text);
    app.statusColor = color;
    if (app.hwnd != nullptr) {
        InvalidateRect(app.hwnd, &app.rcStatus, FALSE);
    }
}

void ShowMessage(HWND owner, const std::wstring& text, UINT icon) {
    MessageBoxW(owner, text.c_str(), L"MCLangDiff", MB_OK | icon | MB_SETFOREGROUND);
}

void DropFailed(App& app, const std::wstring& message) {
    SetStatus(app, L"文件校验失败，请查看弹出的错误提示。", kRed);
    ShowMessage(app.hwnd, message, MB_ICONERROR);
}

// 前向声明
void UpdateTabLabels(App& app);
void UpdateUpdateInfo(App& app);
void UpdateSyncInfo(App& app);
void RefreshTargetConsistency(App& app);
void UpdateActionButtons(App& app);
void LayoutBottomButtons(App& app);
void ShowActiveBlock(App& app);
void ResetResult(App& app);
void UpdateCompareEnabled(App& app);
void PaintMain(HWND hwnd, const App& app);
void PaintZone(const App& app, int index, HDC dc);
void Layout(App& app);
void OnZoneFile(App& app, int index, std::wstring path);
void OnZoneClick(App& app, int index);
void StartCompare(App& app);
void ExportReport(App& app);
void OnUpdateFile(App& app);
void OnSyncFile(App& app);
void ClearPasteBox(App& app);
void CopySyncTemplate(App& app);

// ---------------------------------------------------------------- 标签与结果

void UpdateTabLabels(App& app) {
    if (app.tab == nullptr) {
        return;
    }
    size_t counts[3] = {0, 0, 0};
    if (app.result != nullptr) {
        counts[0] = app.result->added.size();
        counts[1] = app.result->removed.size();
        counts[2] = app.result->changed.size();
    }
    const wchar_t* names[3] = {L"A. 新增键", L"B. 删除键", L"C. 值变更"};
    if (app.compact) {
        names[0] = L"A. 新增";
        names[1] = L"B. 删除";
        names[2] = L"C. 变更";
    }
    for (int i = 0; i < 3; ++i) {
        wchar_t buffer[64] = {};
        std::swprintf(buffer, 64, L"%ls (%zu)", names[i], counts[i]);
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = buffer;
        TabCtrl_SetItem(app.tab, i, &item);
    }
    TCITEMW updateItem{};
    updateItem.mask = TCIF_TEXT;
    updateItem.pszText = const_cast<wchar_t*>(app.compact ? L"D. 更新" : L"D. 更新 zh_cn");
    TabCtrl_SetItem(app.tab, 3, &updateItem);

    TCITEMW syncItem{};
    syncItem.mask = TCIF_TEXT;
    syncItem.pszText = const_cast<wchar_t*>(app.compact ? L"E. 同步" : L"E. 同步 zh_cn");
    TabCtrl_SetItem(app.tab, 4, &syncItem);
}

void UpdateUpdateInfo(App& app) {
    if (app.updateApplied) {
        return;
    }
    app.dUpdateInfo.clear();
    if (app.result == nullptr) {
        return;
    }
    if (!app.zones[2].valid) {
        app.dUpdateInfo = L"更新 · 请先选择 zh_cn 操作文件";
    } else if (app.consistency.state == mcd::TargetConsistency::Error) {
        app.dUpdateInfo = L"更新 · 操作文件读取失败，点击可查看详情";
    } else if (app.consistency.state == mcd::TargetConsistency::MatchesNew) {
        app.dUpdateInfo = L"更新 · 操作文件已与新 en_us 一致，无需更新";
    } else if (app.consistency.state == mcd::TargetConsistency::Mismatch) {
        app.dUpdateInfo = L"更新 · 与旧 en_us 不一致（缺 " +
                          std::to_wstring(app.consistency.missing.size()) + L" / 多 " +
                          std::to_wstring(app.consistency.extra.size()) +
                          L"）· 请先到 E 页同步";
    } else {
        const size_t added = app.result->added.size();
        const size_t changed = app.result->changed.size();
        const size_t required = added + changed;
        const size_t removed = app.result->removed.size();
        if (required == 0 && removed == 0) {
            app.dUpdateInfo = L"更新 · 当前报告没有需要更新的条目";
        } else {
            app.dUpdateInfo = L"更新 · 需粘贴 " + std::to_wstring(required) + L" 条（新增 " +
                              std::to_wstring(added) + L" / 变更 " + std::to_wstring(changed) +
                              L"）· 删除 " + std::to_wstring(removed) + L" 条";
        }
    }
    InvalidateRect(app.hwnd, &app.rcStats, FALSE);
}

void UpdateSyncInfo(App& app) {
    if (app.syncApplied) {
        return;
    }
    app.syncInfo.clear();
    if (app.result == nullptr) {
        return;
    }
    if (!app.zones[2].valid) {
        app.syncInfo = L"同步 · 请先选择 zh_cn 操作文件";
    } else if (app.consistency.state == mcd::TargetConsistency::Error) {
        app.syncInfo = L"同步 · 操作文件读取失败，点击同步可查看详情";
    } else if (app.consistency.state == mcd::TargetConsistency::MatchesOld) {
        app.syncInfo = L"同步 · 键已与旧 en_us 一致，无需同步";
    } else if (app.consistency.state == mcd::TargetConsistency::MatchesNew) {
        app.syncInfo = L"同步 · 键已与新 en_us 一致，无需同步";
    } else if (app.consistency.state == mcd::TargetConsistency::Mismatch) {
        app.syncInfo = L"同步 · 缺 " + std::to_wstring(app.consistency.missing.size()) +
                       L" 个键 / 多 " + std::to_wstring(app.consistency.extra.size()) +
                       L" 个键 · 粘贴翻译后同步";
    } else {
        app.syncInfo = L"同步 · 请先完成 en_us 比较";
    }
    InvalidateRect(app.hwnd, &app.rcStats, FALSE);
}

void RefreshTargetConsistency(App& app) {
    app.consistency = mcd::ConsistencyReport{};
    std::string templateText;
    if (app.result != nullptr && app.zones[2].valid) {
        if (mcd::CheckTargetConsistency(app.zones[2].path, *app.result, app.consistency) &&
            app.consistency.state == mcd::TargetConsistency::Mismatch) {
            mcd::BuildMissingEntriesTemplate(*app.result, app.consistency, templateText);
        }
    }
    if (app.syncTemplateEdit != nullptr) {
        const std::wstring wide = mcd::ToCrlf(mcd::Utf8ToWide(templateText));
        SetWindowTextW(app.syncTemplateEdit, wide.c_str());
    }
    UpdateUpdateInfo(app);
    UpdateSyncInfo(app);
    UpdateActionButtons(app);
    if (app.syncTemplateEdit != nullptr) {
        InvalidateRect(app.syncTemplateEdit, nullptr, TRUE);
    }
}

void LayoutBottomButtons(App& app) {
    if (app.btnCopyBlock == nullptr || app.btnCopyAll == nullptr || app.btnExport == nullptr) {
        return;
    }
    const bool onUpdateTab = app.result != nullptr && (app.activeTab == 3 || app.activeTab == 4);
    ShowWindow(app.btnCopyBlock, onUpdateTab ? SW_HIDE : SW_SHOW);
    ShowWindow(app.btnCopyAll, SW_SHOW);
    ShowWindow(app.btnExport, SW_SHOW);

    const int margin = S(app, app.compact ? 12 : 24);
    const int gap = S(app, app.compact ? 6 : 8);
    const int height = S(app, app.compact ? 30 : 36);
    const int top = app.rcBottom.top + ((app.rcBottom.bottom - app.rcBottom.top) - height) / 2;
    int x = margin;

    if (!onUpdateTab) {
        const int width = S(app, app.compact ? 88 : 120);
        MoveWindow(app.btnCopyBlock, x, top, width, height, TRUE);
        x += width + gap;
    }
    const int copyAllWidth = S(app, app.compact ? 104 : 140);
    MoveWindow(app.btnCopyAll, x, top, copyAllWidth, height, TRUE);
    x += copyAllWidth + gap;
    const int exportWidth = S(app, app.compact ? 88 : 120);
    MoveWindow(app.btnExport, x, top, exportWidth, height, TRUE);
}

void UpdateActionButtons(App& app) {
    const bool onUpdateTab = app.result != nullptr && app.activeTab == 3;
    const bool onSyncTab = app.result != nullptr && app.activeTab == 4;
    if (app.btnUpdate != nullptr) {
        ShowWindow(app.btnUpdate, onUpdateTab ? SW_SHOW : SW_HIDE);
        EnableWindow(app.btnUpdate, onUpdateTab && !app.updateApplied);
    }
    if (app.btnClearPaste != nullptr) {
        ShowWindow(app.btnClearPaste, onUpdateTab ? SW_SHOW : SW_HIDE);
        EnableWindow(app.btnClearPaste, onUpdateTab);
    }
    if (app.btnSyncFile != nullptr) {
        ShowWindow(app.btnSyncFile, onSyncTab ? SW_SHOW : SW_HIDE);
        const bool canSync = onSyncTab && !app.syncApplied &&
                             (app.consistency.state == mcd::TargetConsistency::Mismatch ||
                              app.consistency.state == mcd::TargetConsistency::Error);
        EnableWindow(app.btnSyncFile, canSync);
    }
    if (app.btnCopyTemplate != nullptr) {
        ShowWindow(app.btnCopyTemplate, onSyncTab ? SW_SHOW : SW_HIDE);
        const bool hasTemplate =
            app.syncTemplateEdit != nullptr && GetWindowTextLengthW(app.syncTemplateEdit) > 0;
        EnableWindow(app.btnCopyTemplate, onSyncTab && hasTemplate);
    }
    LayoutBottomButtons(app);
}

void ShowActiveBlock(App& app) {
    const bool onUpdateTab = app.result != nullptr && app.activeTab == 3;
    const bool onSyncTab = app.result != nullptr && app.activeTab == 4;
    for (int i = 0; i < 3; ++i) {
        if (app.edits[i] == nullptr) {
            continue;
        }
        const bool show = app.result != nullptr && i == app.activeTab;
        ShowWindow(app.edits[i], show ? SW_SHOW : SW_HIDE);
        if (show) {
            SendMessageW(app.edits[i], EM_SETSEL, 0, 0);
            SendMessageW(app.edits[i], EM_SCROLLCARET, 0, 0);
        }
    }
    if (app.pasteEdit != nullptr) {
        ShowWindow(app.pasteEdit, onUpdateTab ? SW_SHOW : SW_HIDE);
    }
    if (app.syncTemplateEdit != nullptr) {
        ShowWindow(app.syncTemplateEdit, onSyncTab ? SW_SHOW : SW_HIDE);
    }
    if (app.syncPasteEdit != nullptr) {
        ShowWindow(app.syncPasteEdit, onSyncTab ? SW_SHOW : SW_HIDE);
    }
    UpdateActionButtons(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void ResetResult(App& app) {
    app.result.reset();
    app.updateApplied = false;
    app.syncApplied = false;
    app.consistency = mcd::ConsistencyReport{};
    app.dUpdateInfo.clear();
    app.syncInfo.clear();
    for (auto& text : app.statText) {
        text.clear();
    }
    for (HWND edit : app.edits) {
        if (edit != nullptr) {
            SetWindowTextW(edit, L"");
        }
    }
    if (app.pasteEdit != nullptr) {
        SetWindowTextW(app.pasteEdit, L"");
        ShowWindow(app.pasteEdit, SW_HIDE);
    }
    if (app.syncTemplateEdit != nullptr) {
        SetWindowTextW(app.syncTemplateEdit, L"");
        ShowWindow(app.syncTemplateEdit, SW_HIDE);
    }
    if (app.syncPasteEdit != nullptr) {
        SetWindowTextW(app.syncPasteEdit, L"");
        ShowWindow(app.syncPasteEdit, SW_HIDE);
    }
    UpdateTabLabels(app);
    if (app.tab != nullptr) {
        ShowWindow(app.tab, SW_HIDE);
    }
    for (HWND edit : app.edits) {
        if (edit != nullptr) {
            ShowWindow(edit, SW_HIDE);
        }
    }
    EnableWindow(app.btnCopyBlock, FALSE);
    EnableWindow(app.btnCopyAll, FALSE);
    EnableWindow(app.btnExport, FALSE);
    UpdateActionButtons(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void UpdateCompareEnabled(App& app) {
    const bool ready = app.zones[0].valid && app.zones[1].valid && !app.comparing;
    if (app.btnCompare != nullptr) {
        EnableWindow(app.btnCompare, ready);
    }
}

void ApplyResult(App& app, std::unique_ptr<DiffResult> result) {
    app.result = std::move(result);
    app.updateApplied = false;
    app.statText[0] = mcd::Utf8ToWide(app.result->statsAdded);
    app.statText[1] = mcd::Utf8ToWide(app.result->statsRemoved);
    app.statText[2] = mcd::Utf8ToWide(app.result->statsChanged);

    // Edit 控件只把 CRLF 识别为换行，因此在显示边界做一次转换。
    SetWindowTextW(app.edits[0], mcd::ToCrlf(mcd::Utf8ToWide(app.result->blockAdded)).c_str());
    SetWindowTextW(app.edits[1], mcd::ToCrlf(mcd::Utf8ToWide(app.result->blockRemoved)).c_str());
    SetWindowTextW(app.edits[2], mcd::ToCrlf(mcd::Utf8ToWide(app.result->blockChanged)).c_str());
    if (app.pasteEdit != nullptr) {
        SetWindowTextW(app.pasteEdit, L"");
    }
    if (app.syncPasteEdit != nullptr) {
        SetWindowTextW(app.syncPasteEdit, L"");
    }
    if (app.syncTemplateEdit != nullptr) {
        SetWindowTextW(app.syncTemplateEdit, L"");
    }
    app.syncApplied = false;
    app.syncInfo.clear();

    app.activeTab = 0;
    UpdateTabLabels(app);
    if (app.tab != nullptr) {
        TabCtrl_SetCurSel(app.tab, 0);
        ShowWindow(app.tab, SW_SHOW);
    }
    RefreshTargetConsistency(app);
    ShowActiveBlock(app);

    EnableWindow(app.btnCopyBlock, TRUE);
    EnableWindow(app.btnCopyAll, TRUE);
    EnableWindow(app.btnExport, TRUE);
    UpdateActionButtons(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------- 布局

void Layout(App& app) {
    RECT client{};
    GetClientRect(app.hwnd, &client);
    const int width = client.right;
    const int height = client.bottom;
    if (width <= 0 || height <= 0) {
        return;
    }

    // 按逻辑像素（DIP）判断是否需要紧凑布局，兼容 200% 缩放的小屏幕。
    const int dipWidth = MulDiv(width, 96, static_cast<int>(app.dpi));
    const int dipHeight = MulDiv(height, 96, static_cast<int>(app.dpi));
    const bool compact = dipWidth < 1000 || dipHeight < 640;
    if (compact != app.compact) {
        app.compact = compact;
        DestroyAppFonts(app);
        CreateAppFonts(app);
        ApplyControlFonts(app);
        UpdateTabLabels(app);
    }

    const int margin = S(app, compact ? 12 : 24);
    const int gap = S(app, compact ? 10 : 16);
    app.rcHeader = {margin, S(app, compact ? 6 : 12), width - margin, S(app, compact ? 44 : 78)};

    const int zoneTop = S(app, compact ? 48 : 84);
    const int zoneHeight = S(app, compact ? 82 : 148);
    const int zoneWidth = (width - 2 * margin - 2 * gap) / 3;
    for (int i = 0; i < 3; ++i) {
        const int left = margin + i * (zoneWidth + gap);
        const int right = (i == 2) ? (width - margin) : (left + zoneWidth);
        app.rcZones[i] = {left, zoneTop, right, zoneTop + zoneHeight};
        MoveWindow(app.zoneHwnd[i], left, zoneTop, right - left, zoneHeight, TRUE);
    }

    const int buttonHeight = S(app, compact ? 28 : 38);
    const int actionTop = zoneTop + zoneHeight + S(app, compact ? 8 : 16);
    const int clearWidth = S(app, compact ? 88 : 112);
    const int clearTargetWidth = S(app, compact ? 96 : 124);
    const int smallGap = S(app, compact ? 6 : 8);
    const int swapWidth = S(app, compact ? 96 : 120);
    const int compareWidth = S(app, compact ? 120 : 152);
    int x = margin;
    MoveWindow(app.btnClear[0], x, actionTop, clearWidth, buttonHeight, TRUE);
    x += clearWidth + smallGap;
    MoveWindow(app.btnClear[1], x, actionTop, clearWidth, buttonHeight, TRUE);
    x += clearWidth + smallGap;
    MoveWindow(app.btnClear[2], x, actionTop, clearTargetWidth, buttonHeight, TRUE);
    x += clearTargetWidth + smallGap;
    MoveWindow(app.btnSwap, x, actionTop, swapWidth, buttonHeight, TRUE);
    MoveWindow(app.btnCompare, width - margin - compareWidth, actionTop, compareWidth, buttonHeight,
               TRUE);

    const int statusTop = actionTop + buttonHeight + S(app, compact ? 6 : 12);
    const int statusHeight = S(app, compact ? 16 : 24);
    app.rcStatus = {margin, statusTop, width - margin, statusTop + statusHeight};
    const int progressWidth = S(app, compact ? 120 : 170);
    app.rcProgress = {width - margin - progressWidth, statusTop + statusHeight / 2 - S(app, 3),
                      width - margin, statusTop + statusHeight / 2 + S(app, 3)};
    MoveWindow(app.progress, app.rcProgress.left, app.rcProgress.top,
               app.rcProgress.right - app.rcProgress.left, app.rcProgress.bottom - app.rcProgress.top,
               TRUE);

    const int tabTop = statusTop + S(app, compact ? 22 : 32);
    app.rcTab = {margin, tabTop, width - margin, tabTop + S(app, compact ? 24 : 32)};
    MoveWindow(app.tab, app.rcTab.left, app.rcTab.top, app.rcTab.right - app.rcTab.left,
               app.rcTab.bottom - app.rcTab.top, TRUE);

    const int bottomHeight = S(app, compact ? 42 : 64);
    app.rcBottom = {0, height - bottomHeight, width, height};
    const int cardBottom = height - bottomHeight - S(app, compact ? 6 : 12);
    app.rcCard = {margin, app.rcTab.bottom, width - margin, cardBottom};
    app.rcStats = {app.rcCard.left, app.rcCard.top + 1, app.rcCard.right,
                   app.rcCard.top + S(app, compact ? 26 : 38)};
    const int editTop = app.rcStats.bottom + S(app, compact ? 4 : 6);
    int editBottom = cardBottom - S(app, compact ? 8 : 12);
    if (editBottom < editTop + S(app, 30)) {
        editBottom = editTop + S(app, 30);
    }
    app.rcEdit = {app.rcCard.left + S(app, compact ? 8 : 12), editTop,
                  app.rcCard.right - S(app, compact ? 8 : 12), editBottom};
    for (HWND edit : app.edits) {
        MoveWindow(edit, app.rcEdit.left, app.rcEdit.top, app.rcEdit.right - app.rcEdit.left,
                   app.rcEdit.bottom - app.rcEdit.top, TRUE);
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(S(app, compact ? 6 : 10), S(app, compact ? 6 : 10)));
    }
    if (app.pasteEdit != nullptr) {
        MoveWindow(app.pasteEdit, app.rcEdit.left, app.rcEdit.top,
                   app.rcEdit.right - app.rcEdit.left, app.rcEdit.bottom - app.rcEdit.top, TRUE);
        SendMessageW(app.pasteEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(S(app, compact ? 6 : 10), S(app, compact ? 6 : 10)));
    }

    const int bandHeight = app.rcStats.bottom - app.rcStats.top;
    const int smallButtonHeight = S(app, compact ? 20 : 28);
    const int smallButtonTop = app.rcStats.top + (bandHeight - smallButtonHeight) / 2;
    const int rightEdge = app.rcStats.right - S(app, compact ? 8 : 12);
    const int clearPasteWidth = S(app, compact ? 76 : 96);
    const int updateWidth = S(app, compact ? 88 : 108);
    app.rcClearPaste = {rightEdge - clearPasteWidth, smallButtonTop, rightEdge,
                        smallButtonTop + smallButtonHeight};
    app.rcUpdate = {app.rcClearPaste.left - S(app, 6) - updateWidth, smallButtonTop,
                    app.rcClearPaste.left - S(app, 6), smallButtonTop + smallButtonHeight};
    MoveWindow(app.btnUpdate, app.rcUpdate.left, app.rcUpdate.top,
               app.rcUpdate.right - app.rcUpdate.left, app.rcUpdate.bottom - app.rcUpdate.top, TRUE);
    MoveWindow(app.btnClearPaste, app.rcClearPaste.left, app.rcClearPaste.top,
               app.rcClearPaste.right - app.rcClearPaste.left,
               app.rcClearPaste.bottom - app.rcClearPaste.top, TRUE);
    app.rcSync = app.rcUpdate;
    app.rcCopyTemplate = app.rcClearPaste;
    MoveWindow(app.btnSyncFile, app.rcSync.left, app.rcSync.top,
               app.rcSync.right - app.rcSync.left, app.rcSync.bottom - app.rcSync.top, TRUE);
    MoveWindow(app.btnCopyTemplate, app.rcCopyTemplate.left, app.rcCopyTemplate.top,
               app.rcCopyTemplate.right - app.rcCopyTemplate.left,
               app.rcCopyTemplate.bottom - app.rcCopyTemplate.top, TRUE);

    const int syncContentTop = app.rcStats.bottom + S(app, compact ? 4 : 6);
    const int syncContentBottom = cardBottom - S(app, compact ? 8 : 12);
    const int syncCaptionHeight = S(app, compact ? 14 : 20);
    const int syncGap = S(app, compact ? 4 : 8);
    int syncUsable =
        syncContentBottom - syncContentTop - syncCaptionHeight * 2 - syncGap;
    if (syncUsable < S(app, 28)) {
        syncUsable = S(app, 28);
    }
    int templateHeight = syncUsable * 42 / 100;
    int pasteHeight = syncUsable - templateHeight;
    if (templateHeight < S(app, 20)) {
        templateHeight = S(app, 20);
        pasteHeight = syncUsable - templateHeight;
    }
    if (pasteHeight < S(app, 20)) {
        pasteHeight = S(app, 20);
        templateHeight = syncUsable - pasteHeight;
    }
    app.rcSyncTemplateCaption = {app.rcEdit.left, syncContentTop, app.rcEdit.right,
                                 syncContentTop + syncCaptionHeight};
    app.rcSyncTemplate = {app.rcEdit.left, app.rcSyncTemplateCaption.bottom, app.rcEdit.right,
                          app.rcSyncTemplateCaption.bottom + templateHeight};
    app.rcSyncPasteCaption = {app.rcEdit.left, app.rcSyncTemplate.bottom + syncGap,
                              app.rcEdit.right,
                              app.rcSyncTemplate.bottom + syncGap + syncCaptionHeight};
    app.rcSyncPaste = {app.rcEdit.left, app.rcSyncPasteCaption.bottom, app.rcEdit.right,
                       syncContentBottom};
    if (app.syncTemplateEdit != nullptr) {
        MoveWindow(app.syncTemplateEdit, app.rcSyncTemplate.left, app.rcSyncTemplate.top,
                   app.rcSyncTemplate.right - app.rcSyncTemplate.left,
                   app.rcSyncTemplate.bottom - app.rcSyncTemplate.top, TRUE);
        SendMessageW(app.syncTemplateEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(S(app, compact ? 6 : 10), S(app, compact ? 6 : 10)));
    }
    if (app.syncPasteEdit != nullptr) {
        MoveWindow(app.syncPasteEdit, app.rcSyncPaste.left, app.rcSyncPaste.top,
                   app.rcSyncPaste.right - app.rcSyncPaste.left,
                   app.rcSyncPaste.bottom - app.rcSyncPaste.top, TRUE);
        SendMessageW(app.syncPasteEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(S(app, compact ? 6 : 10), S(app, compact ? 6 : 10)));
    }

    const int barButtonHeight = S(app, compact ? 30 : 36);
    const int barTop = app.rcBottom.top + (bottomHeight - barButtonHeight) / 2;
    app.rcBottomHint = {width - margin - S(app, 280), barTop, width - margin,
                        barTop + barButtonHeight};
    LayoutBottomButtons(app);
}

// ---------------------------------------------------------------- 绘制

void PaintHeader(HDC dc, const App& app) {
    const int titleHeight = S(app, app.compact ? 24 : 34);
    RECT title = {app.rcHeader.left, app.rcHeader.top, app.rcHeader.right,
                  app.rcHeader.top + titleHeight};
    DrawTextIn(dc, L"MCLangDiff", title, app.fTitle, kText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT subtitle = {app.rcHeader.left, title.bottom, app.rcHeader.right, app.rcHeader.bottom};
    DrawTextIn(dc, L"Minecraft 语言文件差异比较 · JSON / LANG · UTF-8", subtitle, app.fSmall,
               kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (!app.compact) {
        RECT version = {app.rcHeader.left, title.top, app.rcHeader.right, title.bottom};
        DrawTextIn(dc, L"v1.1 · 单文件绿色版", version, app.fSmall, kMuted,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void PaintZone(const App& app, int index, HDC dc) {
    RECT rc{};
    GetClientRect(app.zoneHwnd[index], &rc);
    const ZoneState& zone = app.zones[index];
    const ZoneUiInfo& ui = kZoneUi[index];
    const COLORREF accent = ui.accent;
    const bool filled = zone.valid;
    const bool hover = app.zoneHover[index];
    const bool compact = app.compact;
    const int pad = S(app, compact ? 10 : 16);

    FillSolidRect(dc, rc, filled ? kPanel : Mix(kPanel, accent, 7));

    if (filled) {
        RECT stripe = {rc.left, rc.top, rc.left + S(app, 4), rc.bottom};
        FillSolidRect(dc, stripe, accent);
        RECT border = {rc.left, rc.top, rc.right, rc.bottom};
        FrameRect1(dc, border, hover ? accent : kBorder);
    } else {
        const HPEN pen = CreatePen(PS_DASH, 1, hover ? accent : Mix(kPanel, accent, 45));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    const bool wideBadge = index == 2;
    const int badgeWidth =
        S(app, wideBadge ? (compact ? 46 : 56) : (compact ? 38 : 46));
    RECT badge = {rc.left + pad, rc.top + pad, rc.left + pad + badgeWidth,
                  rc.top + pad + S(app, compact ? 16 : 20)};
    FillRoundRect(dc, badge, S(app, 4), accent, accent);
    DrawTextIn(dc, ui.badge, badge, app.fSmallBold, RGB(255, 255, 255),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const int chipWidth = S(app, compact ? 42 : 52);
    RECT title = {badge.right + S(app, compact ? 8 : 10), badge.top - S(app, 2),
                  rc.right - pad - (filled ? chipWidth + S(app, 6) : 0), badge.bottom + S(app, 2)};
    DrawTextIn(dc, ui.title, title, app.fUiBold, kText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (filled) {
        const std::wstring format = FormatLabel(zone.format);
        RECT chip = {rc.right - pad - chipWidth, badge.top, rc.right - pad, badge.bottom};
        FillRoundRect(dc, chip, S(app, 10), Mix(kPanel, accent, 12), Mix(kPanel, accent, 30));
        DrawTextIn(dc, format, chip, app.fSmallBold, accent,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        int y = badge.bottom + S(app, compact ? 4 : 12);
        RECT nameRect = {rc.left + pad, y, rc.right - pad, y + S(app, compact ? 16 : 26)};
        DrawTextIn(dc, zone.name, nameRect, app.fZoneName, kText,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        y = nameRect.bottom;
        RECT pathRect = {rc.left + pad, y, rc.right - pad, y + S(app, compact ? 14 : 20)};
        DrawTextIn(dc, zone.path, pathRect, app.fSmall, kMuted,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);

        y = pathRect.bottom + S(app, compact ? 0 : 4);
        RECT infoRect = {rc.left + pad, y, rc.right - pad - (compact ? 0 : S(app, 72)),
                         y + S(app, compact ? 14 : 20)};
        const std::wstring info = std::wstring(ui.role) + L" · " + FormatLabel(zone.format) +
                                  L" · " + mcd::FormatSize(zone.bytes);
        DrawTextIn(dc, info, infoRect, app.fSmall, kMuted,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (!compact) {
            RECT readyRect = {rc.right - pad - S(app, 72), y, rc.right - pad, y + S(app, 20)};
            DrawTextIn(dc, L"已就绪", readyRect, app.fSmallBold, accent,
                       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    } else {
        const int centerY = (badge.bottom + rc.bottom) / 2;
        RECT line1 = {rc.left + pad, centerY - S(app, compact ? 18 : 24), rc.right - pad, centerY};
        RECT line2 = {rc.left + pad, centerY + S(app, 2), rc.right - pad,
                      centerY + S(app, compact ? 18 : 22)};
        DrawTextIn(dc, ui.emptyHint, line1, app.fZoneHint, hover ? accent : kText,
                   DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawTextIn(dc, L"或点击选择", line2, app.fSmall, kMuted,
                   DT_CENTER | DT_TOP | DT_SINGLELINE);
    }
}

void PaintResults(HDC dc, const App& app) {
    if (app.result == nullptr) {
        RECT card = {app.rcTab.left, app.rcTab.top, app.rcTab.right, app.rcCard.bottom};
        FillSolidRect(dc, card, kPanel);
        FrameRect1(dc, card, kBorder);

        const int inset = S(app, app.compact ? 12 : 24);
        const int centerY = card.top + (card.bottom - card.top) / 2;
        RECT line1 = {card.left + inset, centerY - S(app, app.compact ? 26 : 48),
                      card.right - inset, centerY + S(app, app.compact ? 2 : 6)};
        RECT line2 = {card.left + inset, centerY + S(app, app.compact ? 6 : 12),
                      card.right - inset, centerY + S(app, app.compact ? 24 : 36)};
        DrawTextIn(dc, L"比较结果将在此显示", line1, app.fPanelHint, kText,
                   DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
        DrawTextIn(dc, L"A/B/C 差异报告 · D 更新到新 en_us · E 同步到旧 en_us",
                   line2, app.fSmall, kMuted, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    FillSolidRect(dc, app.rcCard, kPanel);
    FrameRect1(dc, app.rcCard, kBorder);

    const RECT band = app.rcStats;
    FillSolidRect(dc, band, RGB(249, 250, 251));
    RECT divider = {band.left, band.bottom, band.right, band.bottom + 1};
    FillSolidRect(dc, divider, kBorder);

    const bool compact = app.compact;
    const bool updateTab = app.activeTab == 3;
    const bool syncTab = app.activeTab == 4;
    const COLORREF accent = (updateTab || syncTab) ? kTargetAccent : TabAccent(app.activeTab);
    DrawDot(dc, band.left + S(app, compact ? 12 : 18), (band.top + band.bottom) / 2,
            S(app, compact ? 3 : 4), accent);

    int statsRight = band.right - S(app, compact ? 12 : 210);
    if (updateTab) {
        statsRight = app.rcUpdate.left - S(app, 10);
    } else if (syncTab) {
        statsRight = app.rcSync.left - S(app, 10);
    }
    RECT statsRect = {band.left + S(app, compact ? 22 : 32), band.top, statsRight, band.bottom};
    const std::wstring& statsText =
        updateTab ? app.dUpdateInfo : (syncTab ? app.syncInfo : app.statText[app.activeTab]);
    DrawTextIn(dc, statsText, statsRect, app.fUiBold, kText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (!compact && !updateTab && !syncTab) {
        RECT hintRect = {band.right - S(app, 200), band.top, band.right - S(app, 16), band.bottom};
        DrawTextIn(dc, L"纯条目区 · 可直接复制", hintRect, app.fSmall, kMuted,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    if (syncTab) {
        DrawTextIn(dc, compact ? L"模板（可复制）" : L"缺失条目模板（纯条目，可复制后翻译）",
                   app.rcSyncTemplateCaption, app.fSmallBold, kMuted,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawTextIn(dc, compact ? L"粘贴翻译" : L"粘贴已翻译的缺失条目",
                   app.rcSyncPasteCaption, app.fSmallBold, kMuted,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void PaintStatus(HDC dc, const App& app) {
    RECT statusRect = app.rcStatus;
    if (app.comparing) {
        statusRect.right -= S(app, app.compact ? 130 : 190);
    }
    DrawTextIn(dc, app.status, statusRect, app.fUi, app.statusColor,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintBottom(HDC dc, const App& app) {
    RECT line = {app.rcBottom.left, app.rcBottom.top, app.rcBottom.right, app.rcBottom.top + 1};
    FillSolidRect(dc, line, kBorder);
    if (!app.compact) {
        const bool targetTab = app.result != nullptr && (app.activeTab == 3 || app.activeTab == 4);
        DrawTextIn(dc,
                   targetTab ? L"写回不带 BOM · 校验通过后才修改文件"
                             : L"条目顺序：按原文件出现顺序",
                   app.rcBottomHint, app.fSmall, kMuted,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void PaintMain(HWND hwnd, const App& app) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);

    FillSolidRect(mem, client, kBg);
    PaintHeader(mem, app);
    PaintStatus(mem, app);
    PaintResults(mem, app);
    PaintBottom(mem, app);

    BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void DrawOwnerButton(const DRAWITEMSTRUCT& item, const App& app) {
    const int id = static_cast<int>(item.CtlID);
    const bool primary = id == kIdCompare;
    const bool updatePrimary = id == kIdUpdateFile;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const auto hoverIt = app.buttonHover.find(item.hwndItem);
    const bool hover = hoverIt != app.buttonHover.end() && hoverIt->second;

    RECT rc = item.rcItem;
    FillSolidRect(item.hDC, rc, kBg);

    COLORREF background = kPanel;
    COLORREF border = kBorder;
    COLORREF foreground = kText;
    if (updatePrimary) {
        background = disabled ? kGreenDisabled : (pressed || hover ? kGreenHover : kGreen);
        border = background;
        foreground = RGB(255, 255, 255);
    } else if (primary) {
        background = disabled ? kPrimaryDisabled : (pressed || hover ? kPrimaryHover : kPrimary);
        border = background;
        foreground = RGB(255, 255, 255);
    } else if (disabled) {
        background = RGB(243, 244, 246);
        border = RGB(229, 231, 235);
        foreground = RGB(156, 163, 175);
    } else {
        background = pressed ? RGB(238, 242, 247) : (hover ? RGB(249, 250, 251) : kPanel);
        border = hover ? kPrimary : RGB(213, 219, 227);
        foreground = RGB(31, 41, 55);
    }

    FillRoundRect(item.hDC, rc, S(app, 6), background, border);

    wchar_t text[64] = {};
    GetWindowTextW(item.hwndItem, text, 64);
    RECT textRect = rc;
    if (pressed) {
        OffsetRect(&textRect, 0, 1);
    }
    const HFONT font =
        (id == kIdUpdateFile || id == kIdClearPaste) ? app.fSmallBold : app.fButton;
    DrawTextIn(item.hDC, text, textRect, font, foreground,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if ((item.itemState & ODS_FOCUS) != 0 && !disabled) {
        RECT focus = rc;
        InflateRect(&focus, -S(app, 4), -S(app, 4));
        DrawFocusRect(item.hDC, &focus);
    }
}

// ---------------------------------------------------------------- 选择文件

void OnZoneClick(App& app, int index) {
    if (app.comparing) {
        ShowMessage(app.hwnd, L"正在比较，请稍候再更换文件。", MB_ICONINFORMATION);
        return;
    }
    wchar_t file[32768] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.hwnd;
    dialog.lpstrFilter =
        L"Minecraft 语言文件 (*.json;*.lang)\0*.json;*.lang\0JSON 文件 (*.json)\0*.json\0"
        L"LANG 文件 (*.lang)\0*.lang\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = 32768;
    if (index == 0) {
        dialog.lpstrTitle = L"选择旧文件 / Old (en_us)";
    } else if (index == 1) {
        dialog.lpstrTitle = L"选择新文件 / New (en_us)";
    } else {
        dialog.lpstrTitle = L"选择操作文件 / Target (zh_cn)";
    }
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) != FALSE) {
        OnZoneFile(app, index, file);
    }
}

const wchar_t* ZoneRole(int index) {
    switch (index) {
        case 0: return L"旧文件";
        case 1: return L"新文件";
        default: return L"操作文件";
    }
}

void OnZoneFile(App& app, int index, std::wstring path) {
    if (app.comparing) {
        ShowMessage(app.hwnd, L"正在比较，请稍候再更换文件。", MB_ICONINFORMATION);
        return;
    }
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') {
        path = path.substr(1, path.size() - 2);
    }

    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        DropFailed(app, L"文件不存在或无法访问：\n" + path);
        return;
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        DropFailed(app, L"这是文件夹，请拖入 .json 或 .lang 文件：\n" + path);
        return;
    }

    const FileFormat format = mcd::FormatFromPath(path);
    if (format == FileFormat::Unknown) {
        DropFailed(app, L"格式不符：仅支持 .json 或 .lang 文件。\n" + path);
        return;
    }

    const uint64_t size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (size == 0) {
        DropFailed(app, L"文件为空（0 字节），无法使用：\n" + path);
        return;
    }
    if (size > mcd::kMaxFileBytes) {
        DropFailed(app, L"文件过大（超过 512 MB）：\n" + path);
        return;
    }

    for (int i = 0; i < 3; ++i) {
        if (i == index || !app.zones[i].valid) {
            continue;
        }
        if (app.zones[i].format != format) {
            DropFailed(app, std::wstring(L"格式不一致：") + ZoneRole(i) + L"为 " +
                                 FormatLabel(app.zones[i].format) + L"，本次为 " +
                                 FormatLabel(format) +
                                 L"。\n旧文件、新文件与操作文件必须同为 .json 或同为 .lang。");
            return;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (i == index || !app.zones[i].valid) {
            continue;
        }
        if (mcd::IsSameFile(path, app.zones[i].path)) {
            DropFailed(app, std::wstring(L"该文件已被“") + ZoneRole(i) +
                                 L"”区域使用；三个区域不能选择同一个文件：\n" + path);
            return;
        }
    }

    ZoneState& zone = app.zones[index];
    zone.path = path;
    const size_t slash = path.find_last_of(L"\\/");
    zone.name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    zone.format = format;
    zone.bytes = size;
    zone.valid = true;

    InvalidateRect(app.zoneHwnd[index], nullptr, TRUE);

    if (index == 2) {
        app.updateApplied = false;
        app.syncApplied = false;
        if (app.syncPasteEdit != nullptr) {
            SetWindowTextW(app.syncPasteEdit, L"");
        }
        RefreshTargetConsistency(app);
        UpdateActionButtons(app);
        InvalidateRect(app.hwnd, &app.rcStats, FALSE);
        if (app.result != nullptr) {
            SetStatus(app, L"已选择操作文件；若与旧 en_us 不一致，请先在 E 页同步。");
        } else {
            SetStatus(app, L"已选择 zh_cn 操作文件；完成 en_us 比较后即可更新。");
        }
        return;
    }

    ResetResult(app);
    UpdateCompareEnabled(app);
    if (app.zones[0].valid && app.zones[1].valid) {
        SetStatus(app, L"两个文件已就绪：点击“开始比较”。");
    } else if (app.zones[0].valid) {
        SetStatus(app, L"已选择旧文件，请继续选择新文件。");
    } else {
        SetStatus(app, L"已选择新文件，请继续选择旧文件。");
    }
}

void ClearZone(App& app, int index) {
    if (app.comparing) {
        return;
    }
    app.zones[index] = ZoneState{};
    InvalidateRect(app.zoneHwnd[index], nullptr, TRUE);
    if (index == 2) {
        app.updateApplied = false;
        app.syncApplied = false;
        if (app.syncPasteEdit != nullptr) {
            SetWindowTextW(app.syncPasteEdit, L"");
        }
        RefreshTargetConsistency(app);
        UpdateActionButtons(app);
        SetStatus(app, L"已清空操作文件选择。");
        return;
    }
    ResetResult(app);
    UpdateCompareEnabled(app);
    SetStatus(app, index == 0 ? L"已清空旧文件选择。" : L"已清空新文件选择。");
}

void SwapZones(App& app) {
    if (app.comparing) {
        return;
    }
    std::swap(app.zones[0], app.zones[1]);
    InvalidateRect(app.zoneHwnd[0], nullptr, TRUE);
    InvalidateRect(app.zoneHwnd[1], nullptr, TRUE);
    ResetResult(app);
    UpdateCompareEnabled(app);
    SetStatus(app, L"已交换新旧文件，请重新比较。");
}

// ---------------------------------------------------------------- 比较

void StartCompare(App& app) {
    if (app.comparing) {
        return;
    }
    if (!app.zones[0].valid || !app.zones[1].valid) {
        ShowMessage(app.hwnd, L"请先选择旧文件和新文件。", MB_ICONINFORMATION);
        return;
    }
    if (app.zones[0].format != app.zones[1].format) {
        ShowMessage(app.hwnd, L"格式不一致：两个文件必须同为 .json 或同为 .lang。", MB_ICONERROR);
        return;
    }
    if (mcd::IsSameFile(app.zones[0].path, app.zones[1].path)) {
        ShowMessage(app.hwnd, L"新旧文件不能是同一个文件。", MB_ICONERROR);
        return;
    }

    app.comparing = true;
    SetStatus(app, L"正在比较…", kPrimary);
    UpdateCompareEnabled(app);
    EnableWindow(app.btnClear[0], FALSE);
    EnableWindow(app.btnClear[1], FALSE);
    EnableWindow(app.btnSwap, FALSE);
    ShowWindow(app.progress, SW_SHOW);
    SendMessageW(app.progress, PBM_SETMARQUEE, TRUE, 30);
    InvalidateRect(app.hwnd, &app.rcStatus, FALSE);

    const std::wstring oldPath = app.zones[0].path;
    const std::wstring newPath = app.zones[1].path;
    if (app.worker.joinable()) {
        app.worker.join();
    }
    const HWND hwnd = app.hwnd;
    app.worker = std::thread([hwnd, oldPath, newPath]() {
        auto job = std::make_unique<CompareJob>();
        job->ok = mcd::CompareFiles(oldPath, newPath, job->result, job->error);
        if (IsWindow(hwnd)) {
            PostMessageW(hwnd, kMsgCompareDone, 0, reinterpret_cast<LPARAM>(job.release()));
        }
    });
}

void OnCompareDone(App& app, std::unique_ptr<CompareJob> job) {
    if (app.worker.joinable()) {
        app.worker.join();
    }
    app.comparing = false;
    EnableWindow(app.btnClear[0], TRUE);
    EnableWindow(app.btnClear[1], TRUE);
    EnableWindow(app.btnSwap, TRUE);
    ShowWindow(app.progress, SW_HIDE);
    SendMessageW(app.progress, PBM_SETMARQUEE, FALSE, 0);
    UpdateCompareEnabled(app);

    if (job == nullptr) {
        SetStatus(app, L"比较失败：未收到有效结果。", kRed);
        return;
    }
    if (!job->ok) {
        SetStatus(app, L"比较失败，请查看弹出的错误提示。", kRed);
        ShowMessage(app.hwnd, mcd::Utf8ToWide(job->error), MB_ICONERROR);
        return;
    }

    DiffResult* raw = job->result.get();
    ApplyResult(app, std::move(job->result));

    wchar_t summary[256] = {};
    std::swprintf(summary, 256, L"比较完成：新增 %zu · 删除 %zu · 值变更 %zu · 未变化 %zu · 耗时 %.0f ms",
                  raw->added.size(), raw->removed.size(), raw->changed.size(), raw->unchanged,
                  raw->elapsedMs);
    SetStatus(app, summary);
}

// ---------------------------------------------------------------- 复制与导出

bool CopyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) {
        return false;
    }
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) {
        CloseClipboard();
        return false;
    }
    void* destination = GlobalLock(memory);
    if (destination == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    std::memcpy(destination, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

void CopyCurrentBlock(App& app) {
    if (app.result == nullptr) {
        ShowMessage(app.hwnd, L"暂无可复制的内容，请先进行比较。", MB_ICONINFORMATION);
        return;
    }
    const std::string* block = &app.result->blockAdded;
    if (app.activeTab == 1) {
        block = &app.result->blockRemoved;
    } else if (app.activeTab == 2) {
        block = &app.result->blockChanged;
    }
    if (block->empty()) {
        ShowMessage(app.hwnd, L"当前块为空，没有可复制的内容。", MB_ICONINFORMATION);
        return;
    }
    if (!CopyToClipboard(app.hwnd, mcd::ToCrlf(mcd::Utf8ToWide(*block)))) {
        ShowMessage(app.hwnd, L"复制到剪贴板失败。", MB_ICONERROR);
        return;
    }
    SetStatus(app, L"已复制当前块的纯净条目到剪贴板。");
}

void CopyAllReport(App& app) {
    if (app.result == nullptr) {
        ShowMessage(app.hwnd, L"暂无可复制的内容，请先进行比较。", MB_ICONINFORMATION);
        return;
    }
    if (!CopyToClipboard(app.hwnd, mcd::ToCrlf(mcd::Utf8ToWide(app.result->fullReport)))) {
        ShowMessage(app.hwnd, L"复制到剪贴板失败。", MB_ICONERROR);
        return;
    }
    SetStatus(app, L"已复制完整报告到剪贴板。");
}

std::wstring TimestampCompact() {
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);
    wchar_t buffer[32] = {};
    wcsftime(buffer, 32, L"%Y%m%d_%H%M%S", &local);
    return buffer;
}

void ExportReport(App& app) {
    if (app.result == nullptr) {
        ShowMessage(app.hwnd, L"暂无可导出的报告，请先进行比较。", MB_ICONINFORMATION);
        return;
    }
    wchar_t file[32768] = {};
    const std::wstring defaultName = L"MCLangDiff_report_" + TimestampCompact() + L".txt";
    lstrcpynW(file, defaultName.c_str(), 32768);

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.hwnd;
    dialog.lpstrFilter = L"UTF-8 文本报告 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = 32768;
    dialog.lpstrDefExt = L"txt";
    dialog.lpstrTitle = L"导出差异报告";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog) == FALSE) {
        return;
    }

    std::string error;
    const std::string report = mcd::ToCrlf(app.result->fullReport);
    if (!mcd::WriteUtf8File(file, report, true, error)) {
        ShowMessage(app.hwnd, mcd::Utf8ToWide(error), MB_ICONERROR);
        return;
    }
    SetStatus(app, std::wstring(L"已导出报告：") + file);
}

std::wstring GetEditTextW(HWND edit) {
    if (edit == nullptr) {
        return std::wstring();
    }
    const int length = GetWindowTextLengthW(edit);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(edit, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
    return text;
}

std::wstring ConsistencyWarning(const mcd::ConsistencyReport& report) {
    auto join = [](const std::vector<std::string>& keys, size_t maxCount) -> std::wstring {
        std::wstring text;
        const size_t shown = std::min(keys.size(), maxCount);
        for (size_t i = 0; i < shown; ++i) {
            if (i != 0) {
                text += L"、";
            }
            text += mcd::Utf8ToWide(keys[i]);
        }
        if (keys.size() > shown) {
            text += L" 等";
        }
        return text;
    };

    std::wstring message = L"操作文件与旧 en_us 的键集合不一致，无法更新到新版本。\n\n";
    if (!report.missing.empty()) {
        message += L"缺少 " + std::to_wstring(report.missing.size()) + L" 个键：\n" +
                   join(report.missing, 15) + L"\n\n";
    }
    if (!report.extra.empty()) {
        message += L"多出 " + std::to_wstring(report.extra.size()) + L" 个键：\n" +
                   join(report.extra, 15) + L"\n\n";
    }
    message +=
        L"请先切换到“E. 同步 zh_cn”，补齐缺失键的翻译并删除多余键；\n"
        L"同步完成后再回到 D 页更新到新版本。";
    return message;
}

void ClearPasteBox(App& app) {
    if (app.pasteEdit == nullptr) {
        return;
    }
    SetWindowTextW(app.pasteEdit, L"");
    app.updateApplied = false;
    UpdateUpdateInfo(app);
    UpdateActionButtons(app);
    SetFocus(app.pasteEdit);
}

void OnUpdateFile(App& app) {
    if (app.comparing) {
        return;
    }
    if (app.result == nullptr) {
        ShowMessage(app.hwnd, L"请先完成旧/新 en_us 比较，再更新操作文件。", MB_ICONINFORMATION);
        return;
    }
    if (!app.zones[2].valid) {
        SetStatus(app, L"更新失败：未选择操作文件。", kRed);
        ShowMessage(app.hwnd,
                    L"请先在上方“操作文件 / Target”区域选择 zh_cn 语言文件。",
                    MB_ICONINFORMATION);
        return;
    }
    if (app.updateApplied) {
        ShowMessage(app.hwnd, L"本次粘贴内容已经应用；如需再次更新，请修改粘贴内容。",
                    MB_ICONINFORMATION);
        return;
    }

    RefreshTargetConsistency(app);
    if (app.consistency.state == mcd::TargetConsistency::Error) {
        SetStatus(app, L"更新失败：操作文件读取失败。", kRed);
        ShowMessage(app.hwnd, mcd::Utf8ToWide(app.consistency.error), MB_ICONERROR);
        return;
    }
    if (app.consistency.state == mcd::TargetConsistency::MatchesNew) {
        SetStatus(app, L"操作文件的键集合已与新 en_us 一致，无需再次更新。");
        ShowMessage(app.hwnd, L"操作文件的键集合已与新 en_us 一致，无需再次更新。",
                    MB_ICONINFORMATION);
        return;
    }
    if (app.consistency.state == mcd::TargetConsistency::Mismatch) {
        SetStatus(app, L"更新失败：操作文件与旧 en_us 版本不一致，请先到 E 页同步。", kRed);
        ShowMessage(app.hwnd, ConsistencyWarning(app.consistency), MB_ICONWARNING);
        return;
    }

    const std::wstring pastedWide = GetEditTextW(app.pasteEdit);
    const std::string pastedUtf8 = mcd::WideToUtf8(pastedWide);

    mcd::PastedBlock pasted;
    std::string error;
    if (!mcd::ParsePastedBlock(pastedUtf8, app.result->format, pasted, error)) {
        SetStatus(app, L"更新失败：粘贴内容格式不正确，未修改文件。", kRed);
        ShowMessage(app.hwnd, mcd::Utf8ToWide(error), MB_ICONERROR);
        return;
    }

    mcd::UpdateStats stats;
    if (!mcd::ApplyTranslationUpdate(app.zones[2].path, *app.result, pasted, stats, error)) {
        SetStatus(app, L"更新失败：未修改操作文件。", kRed);
        ShowMessage(app.hwnd, mcd::Utf8ToWide(error), MB_ICONERROR);
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(app.zones[2].path.c_str(), GetFileExInfoStandard, &info)) {
        app.zones[2].bytes = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    }
    InvalidateRect(app.zoneHwnd[2], nullptr, TRUE);

    // 清空粘贴框后写入成功状态，避免 EN_CHANGE 把提示覆盖回操作说明。
    SetWindowTextW(app.pasteEdit, L"");
    app.dUpdateInfo = L"已更新 · 新增 " + std::to_wstring(stats.inserted) + L" / 更新 " +
                      std::to_wstring(stats.updated) + L" / 删除 " +
                      std::to_wstring(stats.removed) + L" · 当前 " +
                      std::to_wstring(stats.totalAfter) + L" 条";
    app.updateApplied = true;

    const std::wstring summary = L"已更新操作文件：新增 " + std::to_wstring(stats.inserted) +
                                 L" 条、更新 " + std::to_wstring(stats.updated) + L" 条、删除 " +
                                 std::to_wstring(stats.removed) + L" 条，当前共 " +
                                 std::to_wstring(stats.totalAfter) + L" 条。";
    SetStatus(app, summary, kGreen);
    RefreshTargetConsistency(app);
    UpdateActionButtons(app);
    InvalidateRect(app.hwnd, &app.rcStats, FALSE);
}

void CopySyncTemplate(App& app) {
    const std::wstring text = GetEditTextW(app.syncTemplateEdit);
    if (text.empty()) {
        ShowMessage(app.hwnd, L"当前没有需要新增的缺失条目。", MB_ICONINFORMATION);
        return;
    }
    if (!CopyToClipboard(app.hwnd, text)) {
        ShowMessage(app.hwnd, L"复制到剪贴板失败。", MB_ICONERROR);
        return;
    }
    SetStatus(app, L"已复制缺失条目模板，请翻译值后粘贴到下方并同步。");
}

void OnSyncFile(App& app) {
    if (app.comparing) {
        return;
    }
    if (app.result == nullptr) {
        ShowMessage(app.hwnd, L"请先完成旧/新 en_us 比较，再同步操作文件。", MB_ICONINFORMATION);
        return;
    }
    if (!app.zones[2].valid) {
        SetStatus(app, L"同步失败：未选择操作文件。", kRed);
        ShowMessage(app.hwnd,
                    L"请先在上方“操作文件 / Target”区域选择 zh_cn 语言文件。",
                    MB_ICONINFORMATION);
        return;
    }
    if (app.syncApplied) {
        ShowMessage(app.hwnd, L"本次同步已经完成；如需再次同步，请修改粘贴内容。",
                    MB_ICONINFORMATION);
        return;
    }

    RefreshTargetConsistency(app);
    if (app.consistency.state == mcd::TargetConsistency::Error) {
        SetStatus(app, L"同步失败：操作文件读取失败。", kRed);
        ShowMessage(app.hwnd, mcd::Utf8ToWide(app.consistency.error), MB_ICONERROR);
        return;
    }
    if (app.consistency.state != mcd::TargetConsistency::Mismatch) {
        ShowMessage(app.hwnd, L"操作文件的键集合已与 en_us 一致，无需同步。",
                    MB_ICONINFORMATION);
        return;
    }

    const std::wstring pastedWide = GetEditTextW(app.syncPasteEdit);
    const std::string pastedUtf8 = mcd::WideToUtf8(pastedWide);

    mcd::PastedBlock pasted;
    std::string error;
    if (!mcd::ParsePastedBlock(pastedUtf8, app.result->format, pasted, error)) {
        SetStatus(app, L"同步失败：粘贴内容格式不正确，未修改文件。", kRed);
        ShowMessage(app.hwnd, mcd::Utf8ToWide(error), MB_ICONERROR);
        return;
    }

    mcd::UpdateStats stats;
    if (!mcd::ApplyTargetSync(app.zones[2].path, *app.result, pasted, stats, error)) {
        SetStatus(app, L"同步失败：未修改操作文件。", kRed);
        ShowMessage(app.hwnd, mcd::Utf8ToWide(error), MB_ICONERROR);
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(app.zones[2].path.c_str(), GetFileExInfoStandard, &info)) {
        app.zones[2].bytes = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    }
    InvalidateRect(app.zoneHwnd[2], nullptr, TRUE);

    SetWindowTextW(app.syncPasteEdit, L"");
    app.syncInfo = L"已同步 · 新增 " + std::to_wstring(stats.inserted) + L" / 删除 " +
                   std::to_wstring(stats.removed) + L" · 当前 " +
                   std::to_wstring(stats.totalAfter) + L" 条";
    app.syncApplied = true;
    app.updateApplied = false;

    const std::wstring summary = L"已同步操作文件到旧 en_us：新增 " +
                                 std::to_wstring(stats.inserted) + L" 条、删除 " +
                                 std::to_wstring(stats.removed) + L" 条，当前共 " +
                                 std::to_wstring(stats.totalAfter) + L" 条。";
    SetStatus(app, summary, kGreen);
    RefreshTargetConsistency(app);
    UpdateActionButtons(app);
    InvalidateRect(app.hwnd, &app.rcStats, FALSE);
}

// ---------------------------------------------------------------- 窗口过程

LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR, DWORD_PTR) {
    if (g_app != nullptr) {
        if (message == WM_MOUSEMOVE) {
            bool& hovered = g_app->buttonHover[hwnd];
            if (!hovered) {
                hovered = true;
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } else if (message == WM_MOUSELEAVE) {
            g_app->buttonHover[hwnd] = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void HandleZoneDrop(ZoneContext* context, HDROP drop) {
    if (context == nullptr || context->app == nullptr) {
        DragFinish(drop);
        return;
    }
    App& app = *context->app;
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (count != 1) {
        DragFinish(drop);
        DropFailed(app, L"一次只能拖入一个文件，请分别拖入旧文件与新文件。");
        return;
    }
    const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
    std::wstring path(static_cast<size_t>(length) + 1, L'\0');
    DragQueryFileW(drop, 0, path.data(), length + 1);
    DragFinish(drop);
    path.resize(length);
    PostMessageW(GetParent(context->app->zoneHwnd[context->index]), kMsgZoneFile,
                 static_cast<WPARAM>(context->index),
                 reinterpret_cast<LPARAM>(new std::wstring(std::move(path))));
}

LRESULT CALLBACK ZoneWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<ZoneContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            if (context != nullptr && context->app != nullptr) {
                PaintZone(*context->app, context->index, dc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (context != nullptr && context->app != nullptr &&
                !context->app->zoneHover[context->index]) {
                context->app->zoneHover[context->index] = true;
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (context != nullptr && context->app != nullptr) {
                context->app->zoneHover[context->index] = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        case WM_LBUTTONUP:
            if (context != nullptr && context->app != nullptr) {
                OnZoneClick(*context->app, context->index);
            }
            return 0;
        case WM_DROPFILES:
            HandleZoneDrop(context, reinterpret_cast<HDROP>(wParam));
            return 0;
        case WM_NCDESTROY:
            delete context;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND CreateOwnerButton(HWND parent, int id, const wchar_t* text) {
    HWND button = CreateWindowExW(0, L"BUTTON", text,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                                  parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleW(nullptr), nullptr);
    if (button != nullptr) {
        SetWindowSubclass(button, ButtonSubclassProc, 1, 0);
    }
    return button;
}

void BuildUi(App& app, HWND hwnd) {
    app.hwnd = hwnd;
    const UINT dpi = GetDpiForWindow(hwnd);
    app.dpi = dpi == 0 ? 96 : dpi;
    CreateAppFonts(app);
    app.bgBrush = CreateSolidBrush(kBg);
    app.panelBrush = CreateSolidBrush(kPanel);

    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));

    const int zoneIds[3] = {kIdZoneOld, kIdZoneNew, kIdZoneTarget};
    for (int i = 0; i < 3; ++i) {
        app.zoneHwnd[i] = CreateWindowExW(
            0, kZoneClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(zoneIds[i])), instance, nullptr);
        SetWindowLongPtrW(app.zoneHwnd[i], GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(new ZoneContext{&app, i}));
        DragAcceptFiles(app.zoneHwnd[i], TRUE);
    }

    app.btnClear[0] = CreateOwnerButton(hwnd, kIdClearOld, L"清空旧文件");
    app.btnClear[1] = CreateOwnerButton(hwnd, kIdClearNew, L"清空新文件");
    app.btnClear[2] = CreateOwnerButton(hwnd, kIdClearTarget, L"清空操作文件");
    app.btnSwap = CreateOwnerButton(hwnd, kIdSwap, L"交换新旧");
    app.btnCompare = CreateOwnerButton(hwnd, kIdCompare, L"开始比较");
    app.btnCopyBlock = CreateOwnerButton(hwnd, kIdCopyBlock, L"复制当前块");
    app.btnCopyAll = CreateOwnerButton(hwnd, kIdCopyAll, L"复制全部报告");
    app.btnExport = CreateOwnerButton(hwnd, kIdExport, L"导出报告");
    app.btnUpdate = CreateOwnerButton(hwnd, kIdUpdateFile, L"更新文件");
    app.btnClearPaste = CreateOwnerButton(hwnd, kIdClearPaste, L"清空粘贴");
    app.btnCopyTemplate = CreateOwnerButton(hwnd, kIdCopyTemplate, L"复制模板");
    app.btnSyncFile = CreateOwnerButton(hwnd, kIdSyncFile, L"同步文件");

    app.tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_TABSTOP | TCS_FOCUSNEVER, 0, 0,
                              10, 10, hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTab)), instance,
                              nullptr);
    const wchar_t* titles[5] = {L"A. 新增键", L"B. 删除键", L"C. 值变更", L"D. 更新 zh_cn",
                                L"E. 同步 zh_cn"};
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    for (int i = 0; i < 5; ++i) {
        item.pszText = const_cast<wchar_t*>(titles[i]);
        TabCtrl_InsertItem(app.tab, i, &item);
    }

    const DWORD editStyle = WS_CHILD | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                            ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL;
    const int editIds[3] = {kIdEditAdded, kIdEditRemoved, kIdEditChanged};
    for (int i = 0; i < 3; ++i) {
        app.edits[i] = CreateWindowExW(0, L"EDIT", L"", editStyle, 0, 0, 10, 10, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(editIds[i])),
                                       instance, nullptr);
    }

    const DWORD pasteStyle = WS_CHILD | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                             ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | ES_NOHIDESEL;
    app.pasteEdit = CreateWindowExW(0, L"EDIT", L"", pasteStyle, 0, 0, 10, 10, hwnd,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPasteEdit)),
                                    instance, nullptr);
    if (app.pasteEdit != nullptr) {
        SendMessageW(app.pasteEdit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"在此粘贴新增/值变更键的新翻译条目…"));
    }

    app.syncTemplateEdit = CreateWindowExW(
        0, L"EDIT", L"", editStyle, 0, 0, 10, 10, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSyncTemplateEdit)), instance, nullptr);
    app.syncPasteEdit = CreateWindowExW(
        0, L"EDIT", L"", pasteStyle, 0, 0, 10, 10, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSyncPasteEdit)), instance, nullptr);
    if (app.syncPasteEdit != nullptr) {
        SendMessageW(app.syncPasteEdit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"粘贴缺失键的翻译条目…"));
    }

    app.progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_MARQUEE, 0, 0, 10, 10,
                                   hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdProgress)),
                                   instance, nullptr);
    SendMessageW(app.progress, PBM_SETMARQUEE, FALSE, 30);

    ApplyControlFonts(app);

    ShowWindow(app.tab, SW_HIDE);
    for (HWND edit : app.edits) {
        ShowWindow(edit, SW_HIDE);
    }
    ShowWindow(app.pasteEdit, SW_HIDE);
    ShowWindow(app.syncTemplateEdit, SW_HIDE);
    ShowWindow(app.syncPasteEdit, SW_HIDE);
    ShowWindow(app.btnUpdate, SW_HIDE);
    ShowWindow(app.btnClearPaste, SW_HIDE);
    ShowWindow(app.btnCopyTemplate, SW_HIDE);
    ShowWindow(app.btnSyncFile, SW_HIDE);
    ShowWindow(app.progress, SW_HIDE);

    EnableWindow(app.btnCopyBlock, FALSE);
    EnableWindow(app.btnCopyAll, FALSE);
    EnableWindow(app.btnExport, FALSE);
    EnableWindow(app.btnCompare, FALSE);
    EnableWindow(app.btnUpdate, FALSE);
    EnableWindow(app.btnClearPaste, FALSE);
    EnableWindow(app.btnCopyTemplate, FALSE);
    EnableWindow(app.btnSyncFile, FALSE);

    app.status = L"就绪：拖入旧/新 en_us 与 zh_cn 操作文件，然后比较。";
    DragAcceptFiles(hwnd, TRUE);

    Layout(app);
}

void UpdateDpi(App& app, WPARAM wParam, LPARAM lParam) {
    const UINT newDpi = LOWORD(wParam);
    if (newDpi != 0) {
        app.dpi = newDpi;
    }
    DestroyAppFonts(app);
    CreateAppFonts(app);
    ApplyControlFonts(app);

    const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
    if (suggested != nullptr) {
        SetWindowPos(app.hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    Layout(app);
    InvalidateRect(app.hwnd, nullptr, TRUE);
    for (HWND zone : app.zoneHwnd) {
        InvalidateRect(zone, nullptr, TRUE);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = g_app;
    switch (message) {
        case WM_CREATE:
            if (app != nullptr) {
                BuildUi(*app, hwnd);
            }
            return 0;
        case WM_SIZE:
            if (app != nullptr && app->hwnd != nullptr) {
                Layout(*app);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_PAINT:
            if (app != nullptr) {
                PaintMain(hwnd, *app);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = app != nullptr ? app->dpi : 96;
            int minWidth = MulDiv(620, static_cast<int>(dpi), 96);
            int minHeight = MulDiv(360, static_cast<int>(dpi), 96);
            if (app != nullptr && app->workArea.right > app->workArea.left &&
                app->workArea.bottom > app->workArea.top) {
                const int workWidth = app->workArea.right - app->workArea.left;
                const int workHeight = app->workArea.bottom - app->workArea.top;
                minWidth = std::min(minWidth, workWidth - 16);
                minHeight = std::min(minHeight, workHeight - 16);
            }
            info->ptMinTrackSize.x = std::max(minWidth, 320);
            info->ptMinTrackSize.y = std::max(minHeight, 240);
            return 0;
        }
        case WM_DPICHANGED:
            if (app != nullptr) {
                UpdateDpi(*app, wParam, lParam);
            }
            return 0;
        case WM_COMMAND:
            if (app != nullptr) {
                if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == kIdPasteEdit) {
                    app->updateApplied = false;
                    UpdateUpdateInfo(*app);
                    UpdateActionButtons(*app);
                    return 0;
                }
                if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == kIdSyncPasteEdit) {
                    app->syncApplied = false;
                    UpdateSyncInfo(*app);
                    UpdateActionButtons(*app);
                    return 0;
                }
                switch (LOWORD(wParam)) {
                    case kIdClearOld: ClearZone(*app, 0); return 0;
                    case kIdClearNew: ClearZone(*app, 1); return 0;
                    case kIdClearTarget: ClearZone(*app, 2); return 0;
                    case kIdSwap: SwapZones(*app); return 0;
                    case kIdCompare: StartCompare(*app); return 0;
                    case kIdCopyBlock: CopyCurrentBlock(*app); return 0;
                    case kIdCopyAll: CopyAllReport(*app); return 0;
                    case kIdExport: ExportReport(*app); return 0;
                    case kIdUpdateFile: OnUpdateFile(*app); return 0;
                    case kIdClearPaste: ClearPasteBox(*app); return 0;
                    case kIdCopyTemplate: CopySyncTemplate(*app); return 0;
                    case kIdSyncFile: OnSyncFile(*app); return 0;
                    default: break;
                }
            }
            break;
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (app != nullptr && header != nullptr && header->hwndFrom == app->tab &&
                header->code == TCN_SELCHANGE) {
                const int selected = TabCtrl_GetCurSel(app->tab);
                app->activeTab = (selected >= 0 && selected <= 4) ? selected : 0;
                ShowActiveBlock(*app);
            }
            return 0;
        }
        case WM_DRAWITEM: {
            auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (app != nullptr && item != nullptr && item->CtlType == ODT_BUTTON) {
                DrawOwnerButton(*item, *app);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            if (app == nullptr) {
                break;
            }
            const HWND child = reinterpret_cast<HWND>(lParam);
            HDC dc = reinterpret_cast<HDC>(wParam);
            if (child == app->tab) {
                SetBkColor(dc, kBg);
                SetTextColor(dc, kText);
                return reinterpret_cast<LRESULT>(app->bgBrush);
            }
            SetBkColor(dc, kPanel);
            SetTextColor(dc, kText);
            return reinterpret_cast<LRESULT>(app->panelBrush);
        }
        case WM_DROPFILES:
            DragFinish(reinterpret_cast<HDROP>(wParam));
            if (app != nullptr) {
                ShowMessage(hwnd,
                            L"请将文件拖入上方标注为“旧文件 / Old”“新文件 / New”"
                            L"或“操作文件 / Target”的区域。",
                            MB_ICONINFORMATION);
            }
            return 0;
        case kMsgZoneFile: {
            std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lParam));
            if (app != nullptr && path != nullptr) {
                OnZoneFile(*app, static_cast<int>(wParam), std::move(*path));
            }
            return 0;
        }
        case kMsgZoneClick:
            if (app != nullptr) {
                OnZoneClick(*app, static_cast<int>(wParam));
            }
            return 0;
        case kMsgCompareDone: {
            std::unique_ptr<CompareJob> job(reinterpret_cast<CompareJob*>(lParam));
            if (app != nullptr) {
                OnCompareDone(*app, std::move(job));
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (app != nullptr && app->worker.joinable()) {
                app->worker.detach();
            }
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

int RunApp(HINSTANCE instance, int showCommand) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_TAB_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW zoneClass{};
    zoneClass.cbSize = sizeof(zoneClass);
    zoneClass.style = CS_HREDRAW | CS_VREDRAW;
    zoneClass.lpfnWndProc = ZoneWndProc;
    zoneClass.hInstance = instance;
    zoneClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    zoneClass.lpszClassName = kZoneClassName;
    if (RegisterClassExW(&zoneClass) == 0) {
        return 1;
    }

    WNDCLASSEXW mainClass{};
    mainClass.cbSize = sizeof(mainClass);
    mainClass.style = CS_HREDRAW | CS_VREDRAW;
    mainClass.lpfnWndProc = MainWndProc;
    mainClass.hInstance = instance;
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mainClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    mainClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    mainClass.lpszClassName = kMainClassName;
    if (RegisterClassExW(&mainClass) == 0) {
        return 1;
    }

    g_app = new App();
    g_app->instance = instance;

    const UINT systemDpi = GetDpiForSystem();
    const int preferredWidth = MulDiv(1200, static_cast<int>(systemDpi), 96);
    const int preferredHeight = MulDiv(880, static_cast<int>(systemDpi), 96);

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    g_app->workArea = workArea;
    const int availableWidth = workArea.right - workArea.left;
    const int availableHeight = workArea.bottom - workArea.top;
    const int workDipWidth = MulDiv(availableWidth, 96, static_cast<int>(systemDpi));
    const int workDipHeight = MulDiv(availableHeight, 96, static_cast<int>(systemDpi));
    const bool smallScreen = workDipWidth < 1100 || workDipHeight < 720;
    const int outerWidth = std::min(preferredWidth, availableWidth - MulDiv(40, systemDpi, 96));
    const int outerHeight = std::min(preferredHeight, availableHeight - MulDiv(40, systemDpi, 96));
    const int x = workArea.left + (availableWidth - outerWidth) / 2;
    const int y = workArea.top + (availableHeight - outerHeight) / 2;

    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    HWND hwnd = CreateWindowExW(0, kMainClassName, L"MCLangDiff — Minecraft 语言文件差异比较",
                                style, x, y, outerWidth, outerHeight, nullptr, nullptr, instance,
                                nullptr);
    if (hwnd == nullptr) {
        delete g_app;
        g_app = nullptr;
        return 1;
    }

    ShowWindow(hwnd, smallScreen ? SW_MAXIMIZE : showCommand);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    delete g_app;
    g_app = nullptr;
    return static_cast<int>(message.wParam);
}

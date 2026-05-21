// colorpicker.c

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib") 
#pragma comment(lib, "advapi32.lib")
#include <windows.h>
#include <shellapi.h> 
#include "resource.h" 

#define MCP_ZERO(ptr) ZeroMemory((ptr), sizeof(*(ptr)))

// CONFIGURATION 
#define LOUPE_SIZE 192
#define ZOOM_FACTOR 6
int gPixelRadius = 1;  // 0 = 1x1, 1 = 3x3, 2 = 5x5 averaging grid

// Global handles
HWND hOverlay = NULL;

// Minimize to taskbar
HWND hMsg = NULL;

HWND hToast = NULL;
HDC hdcScreen = NULL;
HINSTANCE gInstance = NULL;

ULONGLONG gToastStartTick = 0;


// Cached GDI pens/brushes/fonts
HPEN gPenOuter = NULL;
HPEN gPenShadow = NULL;
HPEN gPenTarget = NULL;
HPEN gPenPanel = NULL;

HBRUSH gBrushPanel = NULL;
HBRUSH gBrushDot = NULL;
HBRUSH gBrushToastBg = NULL;

HFONT gFontLoupe = NULL;
HFONT gFontToast = NULL;


// Desktop snapshot 
HDC gPickDC = NULL;
HBITMAP gPickBmp = NULL;
void* gPickBits = NULL;

int gVirtX = 0;
int gVirtY = 0;
int gVirtW = 0;
int gVirtH = 0;

int gLoupeX = 0;
int gLoupeY = 0;


// App messages / hotkey

#define WM_APP_PICK     (WM_APP + 1)
#define WM_APP_TRAYMSG  (WM_APP + 2) 
#define IDTRAYEXIT      1002
#define ID_TRAY_PICK    1003
#define ID_TRAY_SIZE_1X1 1004
#define ID_TRAY_SIZE_3X3 1005
#define ID_TRAY_SIZE_5X5 1006
#define ID_TRAY_SIZE_15X15 1007
#define ID_TRAY_STARTUP  1008





#define TOAST_W         92
#define TOAST_H         36
#define TOAST_TIMER_ID  7
#define TOAST_DURATION  1500
#define ENSURE(h, e) do { if (!(h)) (h) = (e); } while (0)
#define DEL_OBJ(o) do { if (o) { DeleteObject(o); (o) = NULL; } } while (0)
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define MAKE_FONT(px) CreateFontA(-(px),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI")

#define REG_CLASS(proc, name, cursor, style_) \
    wc.style = style_; \
    wc.lpfnWndProc = proc; \
    wc.hCursor = cursor; \
    wc.lpszClassName = name; \
    RegisterClassA(&wc)


static void GetVirtualMetrics(int* x, int* y, int* w, int* h) {
    *x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    *y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    *w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    *h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (*w <= 0 || *h <= 0) {
        *x = *y = 0;
        *w = GetSystemMetrics(SM_CXSCREEN);
        *h = GetSystemMetrics(SM_CYSCREEN);
    }
}

static void PositionLoupe(int screenX, int screenY) {
    int cx = screenX - gVirtX, cy = screenY - gVirtY;
    int lx = cx + 15, ly = cy + 15;
    int maxX = gVirtW > LOUPE_SIZE ? gVirtW - LOUPE_SIZE : 0;
    int maxY = gVirtH > LOUPE_SIZE ? gVirtH - LOUPE_SIZE : 0;

    if (lx + LOUPE_SIZE > gVirtW) lx = cx - LOUPE_SIZE - 15;
    if (ly + LOUPE_SIZE > gVirtH) ly = cy - LOUPE_SIZE - 15;

    gLoupeX = CLAMP(lx, 0, maxX);
    gLoupeY = CLAMP(ly, 0, maxY);
}

typedef struct AppHotkey {
    int id;
    UINT modifiers;
    UINT vk;
} AppHotkey;

AppHotkey gHotkey = { 1, MOD_CONTROL | MOD_SHIFT, 'D' };

int gHotkeyRegistered = 0;
int gExiting = 0;
char gIniPath[MAX_PATH] = { 0 };

static void SaveSettings(void);
static void LoadSettings(void);
static void InitSettingsPath(void);

// App icon 
HICON gAppIcon = NULL;
HICON gAppIconSm = NULL;

// Forward declarations
void StartPicker(HINSTANCE hInstance);
void StopPicker(void);
void ExitApp(void);
void ShowCopiedToast(int x, int y);
LRESULT CALLBACK HiddenProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ToastProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void InitCachedGdiObjects(void) {
    ENSURE(gFontLoupe, MAKE_FONT(13));
    ENSURE(gFontToast, MAKE_FONT(15));

    ENSURE(gPenOuter, CreatePen(PS_SOLID, 2, RGB(25, 25, 25)));
    ENSURE(gPenShadow, CreatePen(PS_SOLID, 3, RGB(0, 0, 0)));
    ENSURE(gPenTarget, CreatePen(PS_SOLID, 2, RGB(255, 255, 255)));
    ENSURE(gPenPanel, CreatePen(PS_SOLID, 1, RGB(70, 70, 70)));

    ENSURE(gBrushPanel, CreateSolidBrush(RGB(18, 18, 18)));
    ENSURE(gBrushDot, CreateSolidBrush(RGB(255, 255, 255)));
    ENSURE(gBrushToastBg, CreateSolidBrush(RGB(20, 20, 20)));
}

static void DestroyCachedGdiObjects(void) {
    DEL_OBJ(gFontLoupe);
    DEL_OBJ(gFontToast);

    DEL_OBJ(gPenOuter);
    DEL_OBJ(gPenShadow);
    DEL_OBJ(gPenTarget);
    DEL_OBJ(gPenPanel);

    DEL_OBJ(gBrushPanel);
    DEL_OBJ(gBrushDot);
    DEL_OBJ(gBrushToastBg);
}

static void LoadAppIcons(void) {
    // Load icon
    gAppIcon = (HICON)LoadImageA(
        gInstance,
        MAKEINTRESOURCEA(IDI_ICON1), // ID from resource.h
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR
    );

    gAppIconSm = (HICON)LoadImageA(
        gInstance,
        MAKEINTRESOURCEA(IDI_ICON1),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR
    );

    // Fallback 
    if (!gAppIcon) {
        gAppIcon = LoadIconA(NULL, MAKEINTRESOURCEA(32512));
    }

    if (!gAppIconSm) {
        gAppIconSm = LoadIconA(NULL, MAKEINTRESOURCEA(32512));
    }
}

static void DestroyAppIcons(void) {
    HICON defaultIcon = LoadIconA(NULL, MAKEINTRESOURCEA(32512)); 

    if (gAppIcon && gAppIcon != defaultIcon) {
        DestroyIcon(gAppIcon);
    }

    if (gAppIconSm && gAppIconSm != defaultIcon) {
        DestroyIcon(gAppIconSm);
    }

    gAppIcon = NULL;
    gAppIconSm = NULL;
}

static void FreeDesktopSnapshot(void) {
    DEL_OBJ(gPickBmp);
    DeleteDC(gPickDC);
    gPickDC = NULL;
    gPickBits = NULL;
}

static int CaptureDesktopSnapshot(void) {
    int oldW = gVirtW, oldH = gVirtH;
    GetVirtualMetrics(&gVirtX, &gVirtY, &gVirtW, &gVirtH);

    // Reuse bitmap memory if same screen dimensions
    if (gPickDC && gPickBmp && gVirtW == oldW && gVirtH == oldH) {
        if (BitBlt(gPickDC, 0, 0, gVirtW, gVirtH, hdcScreen, gVirtX, gVirtY, SRCCOPY)) {
            return 1;
        }
    }

    FreeDesktopSnapshot();

    gPickDC = CreateCompatibleDC(hdcScreen);
    if (!gPickDC) return 0;

    BITMAPINFO bmi;
    MCP_ZERO(&bmi);

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = gVirtW;
    bmi.bmiHeader.biHeight = -gVirtH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    gPickBmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &gPickBits, NULL, 0);
    if (!gPickBmp || !gPickBits) {
        FreeDesktopSnapshot();
        return 0;
    }

    SelectObject(gPickDC, gPickBmp);

    if (!BitBlt(gPickDC, 0, 0, gVirtW, gVirtH, hdcScreen, gVirtX, gVirtY, SRCCOPY)) {
        FreeDesktopSnapshot();
        return 0;
    }

    return 1;
}

static void MoveMergedLoupe(HWND hwnd, int screenX, int screenY) {
    RECT oldRect = { gLoupeX, gLoupeY, gLoupeX + LOUPE_SIZE, gLoupeY + LOUPE_SIZE };

    PositionLoupe(screenX, screenY);

    RECT newRect = { gLoupeX, gLoupeY, gLoupeX + LOUPE_SIZE, gLoupeY + LOUPE_SIZE };

    RedrawWindow(hwnd, &oldRect, NULL, RDW_INVALIDATE | RDW_NOERASE);
    RedrawWindow(hwnd, &newRect, NULL, RDW_INVALIDATE | RDW_NOERASE);
}

static COLORREF SampleSnapshotColor(int screenX, int screenY) {
    int lx = screenX - gVirtX, ly = screenY - gVirtY, r = gPixelRadius;
    int x0 = CLAMP(lx - r, 0, gVirtW - 1), x1 = CLAMP(lx + r, 0, gVirtW - 1);
    int y0 = CLAMP(ly - r, 0, gVirtH - 1), y1 = CLAMP(ly + r, 0, gVirtH - 1);
    int sumR = 0, sumG = 0, sumB = 0;
    DWORD* px = (DWORD*)gPickBits;

    for (int y = y0; y <= y1; y++) {
        DWORD* row = px + y * gVirtW + x0;

        for (int x = x0; x <= x1; x++, row++) {
            DWORD c = *row;
            sumB += c & 0xFF;
            sumG += (c >> 8) & 0xFF;
            sumR += (c >> 16) & 0xFF;
        }
    }

    int count = (x1 - x0 + 1) * (y1 - y0 + 1);
    return count ? RGB(sumR / count, sumG / count, sumB / count) : RGB(0, 0, 0);
}

// Fading "copied" toast
LRESULT CALLBACK ToastProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        gToastStartTick = GetTickCount64();
        SetTimer(hwnd, TOAST_TIMER_ID, 16, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == TOAST_TIMER_ID) {
            ULONGLONG elapsed = GetTickCount64() - gToastStartTick;

            if (elapsed >= TOAST_DURATION) {
                KillTimer(hwnd, TOAST_TIMER_ID);
                DestroyWindow(hwnd);
                return 0;
            }

            BYTE alpha = (BYTE)(255 - ((elapsed * 255) / TOAST_DURATION));
            SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        HPEN hOldPen = (HPEN)SelectObject(hdc, gPenPanel);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, gBrushToastBg);

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);

        SelectObject(hdc, hOldPen);
        SelectObject(hdc, hOldBrush);

        HFONT hOldFont = NULL;

        if (gFontToast) {
            hOldFont = (HFONT)SelectObject(hdc, gFontToast);
        }

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(245, 245, 245));

        DrawTextA(
            hdc,
            "Copied",
            6,
            &rc,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );

        if (hOldFont) {
            SelectObject(hdc, hOldFont);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, TOAST_TIMER_ID);

        if (hwnd == hToast) {
            hToast = NULL;
        }
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void ShowCopiedToast(int x, int y) {
    if (hToast) {
        DestroyWindow(hToast);
        hToast = NULL;
    }

    GetVirtualMetrics(&gVirtX, &gVirtY, &gVirtW, &gVirtH);

    int toastX = x + 14;
    int toastY = y + 18;

    if (toastX + TOAST_W > gVirtX + gVirtW) toastX = x - TOAST_W - 14;
    if (toastY + TOAST_H > gVirtY + gVirtH) toastY = y - TOAST_H - 18;

    toastX = CLAMP(toastX, gVirtX, gVirtX + gVirtW - TOAST_W);
    toastY = CLAMP(toastY, gVirtY, gVirtY + gVirtH - TOAST_H);

    hToast = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        "ColorPickerToast", "",
        WS_POPUP,
        toastX, toastY, TOAST_W, TOAST_H,
        NULL, NULL, gInstance, NULL
    );

    if (hToast) {
        SetLayeredWindowAttributes(hToast, 0, 255, LWA_ALPHA);
        ShowWindow(hToast, SW_SHOWNOACTIVATE);
        UpdateWindow(hToast);
    }
}



// Fullscreen invisible overlay
static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        if (gPickDC) {
            RECT rcPaint = ps.rcPaint;

            // Paint snapshot as fullscreen background
            BitBlt(
                hdc,
                rcPaint.left,
                rcPaint.top,
                rcPaint.right - rcPaint.left,
                rcPaint.bottom - rcPaint.top,
                gPickDC,
                rcPaint.left,
                rcPaint.top,
                SRCCOPY
            );

            // Draw loupe and add to overlay as merged bitmap
            HDC hMemDC = CreateCompatibleDC(hdc);
            HBITMAP hBmp = CreateCompatibleBitmap(hdc, LOUPE_SIZE, LOUPE_SIZE);
            HBITMAP hOld = (HBITMAP)SelectObject(hMemDC, hBmp);

            POINT pt;
            GetCursorPos(&pt);

            int srcSize = LOUPE_SIZE / ZOOM_FACTOR;
            int srcX = pt.x - gVirtX - srcSize / 2;
            int srcY = pt.y - gVirtY - srcSize / 2;

            SetStretchBltMode(hMemDC, COLORONCOLOR);

            StretchBlt(
                hMemDC,
                0,
                0,
                LOUPE_SIZE,
                LOUPE_SIZE,
                gPickDC,
                srcX,
                srcY,
                srcSize,
                srcSize,
                SRCCOPY
            );

            // Border
            HPEN hOldPen = (HPEN)SelectObject(hMemDC, gPenOuter);
            HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hMemDC, hNullBrush);

            Rectangle(hMemDC, 0, 0, LOUPE_SIZE, LOUPE_SIZE);

            SelectObject(hMemDC, hOldPen);
            SelectObject(hMemDC, hOldBrush);
           

            // Center target pixel
            int centerBoxSize = (2 * gPixelRadius + 1) * ZOOM_FACTOR;
            int centerBoxX = (LOUPE_SIZE - centerBoxSize) / 2;
            int centerBoxY = (LOUPE_SIZE - centerBoxSize) / 2;

            RECT rBox = {
                centerBoxX,
                centerBoxY,
                centerBoxX + centerBoxSize,
                centerBoxY + centerBoxSize
            };

            // Shadow around target
            hOldPen = (HPEN)SelectObject(hMemDC, gPenShadow);
            hOldBrush = (HBRUSH)SelectObject(hMemDC, hNullBrush);

            Rectangle(hMemDC, rBox.left - 1, rBox.top - 1, rBox.right + 1, rBox.bottom + 1);

            SelectObject(hMemDC, hOldPen);
            SelectObject(hMemDC, hOldBrush);

            // Target outline
            hOldPen = (HPEN)SelectObject(hMemDC, gPenTarget);
            hOldBrush = (HBRUSH)SelectObject(hMemDC, hNullBrush);

            Rectangle(hMemDC, rBox.left, rBox.top, rBox.right, rBox.bottom);

            SelectObject(hMemDC, hOldPen);
            SelectObject(hMemDC, hOldBrush);

            // Small center dot
            RECT rDot = {
                LOUPE_SIZE / 2 - 1,
                LOUPE_SIZE / 2 - 1,
                LOUPE_SIZE / 2 + 2,
                LOUPE_SIZE / 2 + 2
            };

            FillRect(hMemDC, &rDot, gBrushDot);

            // Instruction panel
            RECT panel = { 8, 8, 118, 49 };

            hOldPen = (HPEN)SelectObject(hMemDC, gPenPanel);
            hOldBrush = (HBRUSH)SelectObject(hMemDC, gBrushPanel);

            RoundRect(hMemDC, panel.left, panel.top, panel.right, panel.bottom, 10, 10);

            SelectObject(hMemDC, hOldPen);
            SelectObject(hMemDC, hOldBrush);

            HFONT hOldFont = NULL;

            if (gFontLoupe) {
                hOldFont = (HFONT)SelectObject(hMemDC, gFontLoupe);
            }

            SetBkMode(hMemDC, TRANSPARENT);

            SetTextColor(hMemDC, RGB(245, 245, 245));
            TextOutA(hMemDC, 18, 14, "Click to copy", 13);

            SetTextColor(hMemDC, RGB(180, 180, 180));
            TextOutA(hMemDC, 18, 31, "Esc to cancel", 13);

            if (hOldFont) {
                SelectObject(hMemDC, hOldFont);
            }

            BitBlt(
                hdc,
                gLoupeX,
                gLoupeY,
                LOUPE_SIZE,
                LOUPE_SIZE,
                hMemDC,
                0,
                0,
                SRCCOPY
            );

            SelectObject(hMemDC, hOld);
            DeleteObject(hBmp);
            DeleteDC(hMemDC);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt;
        GetCursorPos(&pt);
        MoveMergedLoupe(hwnd, pt.x, pt.y);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt;
        GetCursorPos(&pt);

        COLORREF avg = SampleSnapshotColor(pt.x, pt.y);

        static const char hex[] = "0123456789ABCDEF";

        char hexStr[8];
        hexStr[0] = '#';

        BYTE r8 = (BYTE)GetRValue(avg);
        BYTE g8 = (BYTE)GetGValue(avg);
        BYTE b8 = (BYTE)GetBValue(avg);

        hexStr[1] = hex[r8 >> 4];
        hexStr[2] = hex[r8 & 0xF];
        hexStr[3] = hex[g8 >> 4];
        hexStr[4] = hex[g8 & 0xF];
        hexStr[5] = hex[b8 >> 4];
        hexStr[6] = hex[b8 & 0xF];
        hexStr[7] = '\0';

        if (OpenClipboard(hwnd)) {
            EmptyClipboard();

            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(hexStr));

            if (hMem) {
                char* ptr = GlobalLock(hMem);
                if (ptr) {
                    lstrcpyA(ptr, hexStr);
                    GlobalUnlock(hMem);

                    if (!SetClipboardData(CF_TEXT, hMem)) {
                        GlobalFree(hMem);
                    }
                }
                else {
                    GlobalFree(hMem);
                }
            }

            CloseClipboard();
        }

        ShowCopiedToast(pt.x, pt.y);

        StopPicker();
        return 0;
    }

    case WM_RBUTTONDOWN:
        StopPicker();
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            StopPicker();
            return 0;
        }
        break;

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}




static void RegisterAppHotkey(void) {
    if (!gHotkeyRegistered && hMsg) {
        if (RegisterHotKey(hMsg, gHotkey.id, gHotkey.modifiers, gHotkey.vk)) {
            gHotkeyRegistered = 1;
        }
    }
}

static void UnregisterAppHotkey(void) {
    if (gHotkeyRegistered && hMsg) {
        UnregisterHotKey(hMsg, gHotkey.id);
        gHotkeyRegistered = 0;
    }
}

void StartPicker(HINSTANCE hInstance) {
    if (hOverlay) return;

    if (!CaptureDesktopSnapshot()) {
        RegisterAppHotkey();
        return;
    }

    POINT pt;
    GetCursorPos(&pt);
    PositionLoupe(pt.x, pt.y);

    hOverlay = CreateWindowExA(
        WS_EX_TOPMOST,
        "ColorPickerOverlay", "Minimal Color Picker",
        WS_POPUP,
        gVirtX, gVirtY, gVirtW, gVirtH,
        NULL, NULL, hInstance, NULL
    );

    if (!hOverlay) {
        FreeDesktopSnapshot();
        RegisterAppHotkey();
        return;
    }

    SendMessageA(hOverlay, WM_SETICON, ICON_BIG, (LPARAM)gAppIcon);
    SendMessageA(hOverlay, WM_SETICON, ICON_SMALL, (LPARAM)gAppIconSm);

    ShowWindow(hOverlay, SW_SHOW);
    SetForegroundWindow(hOverlay);
    SetFocus(hOverlay);
    UpdateWindow(hOverlay);
}

static void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYMSG;
    nid.hIcon = gAppIconSm;
    lstrcpyA(nid.szTip, "Minimal Color Picker");
    Shell_NotifyIconA(NIM_ADD, &nid);
}

static void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

void StopPicker(void) {
    if (hOverlay) {
        DestroyWindow(hOverlay);
        hOverlay = NULL;
    }

    FreeDesktopSnapshot();

    if (!gExiting) {
        RegisterAppHotkey();
    }
}

void ExitApp(void) {
    gExiting = 1;

    StopPicker();
    UnregisterAppHotkey();
    RemoveTrayIcon(hMsg); 

    PostQuitMessage(0);
}

static int IsStartupEnabled(void) {
    HKEY hKey;
    int enabled = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char path[MAX_PATH];
        DWORD size = sizeof(path);
        if (RegQueryValueExA(hKey, "MinimalColorPicker", NULL, NULL, (LPBYTE)path, &size) == ERROR_SUCCESS) {
            enabled = 1;
        }
        RegCloseKey(hKey);
    }
    return enabled;
}

static void ToggleStartup(void) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
        if (IsStartupEnabled()) {
            RegDeleteValueA(hKey, "MinimalColorPicker");
        }
        else {
            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            RegSetValueExA(hKey, "MinimalColorPicker", 0, REG_SZ, (const BYTE*)exePath, lstrlenA(exePath) + 1);
        }
        RegCloseKey(hKey);
    }
}

LRESULT CALLBACK HiddenProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)gAppIcon);
        SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)gAppIconSm);
        AddTrayIcon(hwnd);
        return 0;

    case WM_APP_PICK:
        StartPicker(gInstance);
        return 0;

    case WM_APP_TRAYMSG:
        if (lParam == WM_LBUTTONUP) {
            // Left click tray icon = Pick color
            PostMessageA(hwnd, WM_APP_PICK, 0, 0);
        }
        else if (lParam == WM_RBUTTONUP) {
            // Right click tray icon = Context Menu
            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();
            InsertMenuA(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_PICK, "Pick Color");
            InsertMenuA(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

            // Submenu for size
            HMENU hSubMenu = CreatePopupMenu();
            InsertMenuA(hSubMenu, 0, MF_BYPOSITION | MF_STRING | (gPixelRadius == 0 ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_SIZE_1X1, "1x1 Pixel");
            InsertMenuA(hSubMenu, 1, MF_BYPOSITION | MF_STRING | (gPixelRadius == 1 ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_SIZE_3X3, "3x3 Average");
            InsertMenuA(hSubMenu, 2, MF_BYPOSITION | MF_STRING | (gPixelRadius == 2 ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_SIZE_5X5, "5x5 Average");
            InsertMenuA(hSubMenu, 3, MF_BYPOSITION | MF_STRING | (gPixelRadius == 7 ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_SIZE_15X15, "15x15 Average");
            InsertMenuA(hMenu, 2, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hSubMenu, "Sample Size");

            InsertMenuA(hMenu, 3, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

            UINT startupFlags = MF_BYPOSITION | MF_STRING;
            if (IsStartupEnabled()) startupFlags |= MF_CHECKED;
            InsertMenuA(hMenu, 4, startupFlags, ID_TRAY_STARTUP, "Run on Startup");

            InsertMenuA(hMenu, 5, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
            InsertMenuA(hMenu, 6, MF_BYPOSITION | MF_STRING, IDTRAYEXIT, "Exit");

            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu); 

            if (cmd == ID_TRAY_PICK) {
                PostMessageA(hwnd, WM_APP_PICK, 0, 0);
            }
            else if (cmd >= ID_TRAY_SIZE_1X1 && cmd <= ID_TRAY_SIZE_15X15) {
                if (cmd == ID_TRAY_SIZE_1X1) gPixelRadius = 0;
                else if (cmd == ID_TRAY_SIZE_3X3) gPixelRadius = 1;
                else if (cmd == ID_TRAY_SIZE_5X5) gPixelRadius = 2;
                else if (cmd == ID_TRAY_SIZE_15X15) gPixelRadius = 7;
                SaveSettings(); // Write to .ini file
            }
            else if (cmd == ID_TRAY_STARTUP) {
                ToggleStartup();
            }
            else if (cmd == IDTRAYEXIT) {
                ExitApp();
            }
        }
        return 0;

    case WM_HOTKEY:
        if ((int)wParam == gHotkey.id) {
            StartPicker(gInstance);
            return 0;
        }
        break;

    case WM_DESTROY:
        if (!gExiting) {
            ExitApp();
        }
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void InitSettingsPath(void) {
    GetModuleFileNameA(NULL, gIniPath, MAX_PATH);
    int len = lstrlenA(gIniPath);
    while (len > 0 && gIniPath[len] != '\\' && gIniPath[len] != '.') len--;
    if (len > 0 && gIniPath[len] == '.') lstrcpyA(&gIniPath[len], ".ini");
    else lstrcatA(gIniPath, ".ini");
}

static void LoadSettings(void) {
    gPixelRadius = GetPrivateProfileIntA("Settings", "PixelRadius", 1, gIniPath);
    gHotkey.modifiers = GetPrivateProfileIntA("Settings", "HotkeyMods", MOD_CONTROL | MOD_SHIFT, gIniPath);
    gHotkey.vk = GetPrivateProfileIntA("Settings", "HotkeyVK", 'D', gIniPath);
}

static void SaveSettings(void) {
    char buf[32];
    wsprintfA(buf, "%d", gPixelRadius);
    WritePrivateProfileStringA("Settings", "PixelRadius", buf, gIniPath);
    wsprintfA(buf, "%d", gHotkey.modifiers);
    WritePrivateProfileStringA("Settings", "HotkeyMods", buf, gIniPath);
    wsprintfA(buf, "%d", gHotkey.vk);
    WritePrivateProfileStringA("Settings", "HotkeyVK", buf, gIniPath);

    // Write instructions in INI file
    WritePrivateProfileStringA("Settings", "Help_Mods", "1=ALT, 2=CTRL, 4=SHIFT, 8=WIN (Add them together, e.g., 6 is CTRL+SHIFT)", gIniPath);
    WritePrivateProfileStringA("Settings", "Help_VK", "Use Decimal Virtual-Key Codes. Link below.", gIniPath);
    WritePrivateProfileStringA("Settings", "Help_URL", "https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes", gIniPath);
}

// Entry Point
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    InitSettingsPath();
    LoadSettings();
    // 1:1 pixel mapping on high-dpi
    SetProcessDPIAware();

    HWND hExisting = FindWindowA("ColorPickerHidden", NULL);
    if (hExisting) {
        PostMessageA(hExisting, WM_APP_PICK, 0, 0);
        return 0;
    }

    gInstance = hInstance;
    hdcScreen = GetDC(NULL);

    SaveSettings(); // Create settings file

    InitCachedGdiObjects();
    LoadAppIcons();

    WNDCLASSA wc;
    MCP_ZERO(&wc);

    wc.hInstance = hInstance;
    wc.hIcon = gAppIcon;

    REG_CLASS(HiddenProc, "ColorPickerHidden", NULL, 0);
    REG_CLASS(OverlayProc, "ColorPickerOverlay", LoadCursor(NULL, IDC_CROSS), CS_HREDRAW | CS_VREDRAW);
    REG_CLASS(ToastProc, "ColorPickerToast", LoadCursor(NULL, IDC_ARROW), 0);

    hMsg = CreateWindowExA(
        WS_EX_TOOLWINDOW,
        "ColorPickerHidden",
        "Minimal Color Picker",
        WS_POPUP,
        0, 0, 0, 0,
        NULL,
        NULL,
        hInstance,
        NULL);

    if (!hMsg) {
        DestroyAppIcons();
        DestroyCachedGdiObjects();

        if (hdcScreen) {
            ReleaseDC(NULL, hdcScreen);
            hdcScreen = NULL;
        }

        return 0;
    }

    RegisterAppHotkey();

    // Start on app launch
    StartPicker(hInstance);

    // Keep app alive
    MSG msg;

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    DestroyAppIcons();
    DestroyCachedGdiObjects();

    if (hdcScreen) {
        ReleaseDC(NULL, hdcScreen);
        hdcScreen = NULL;
    }

    return (int)msg.wParam;
}

#ifdef GEODE_IS_WINDOWS

#include "GDWindow.hpp"
#include <Geode/Geode.hpp>
#include <cmath>

using namespace geode::prelude;

namespace browser::win {

namespace {
    HWND g_cachedWindow = nullptr;
    BOOL CALLBACK findOwnTopLevelWindow(HWND hwnd, LPARAM lParam) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return TRUE; // keep looking

        if (!IsWindowVisible(hwnd)) return TRUE;

        wchar_t title[256]{};
        GetWindowTextW(hwnd, title, 255);
        if (wcscmp(title, L"Geometry Dash") == 0) {
            *reinterpret_cast<HWND*>(lParam) = hwnd;
            return FALSE; // found it, stop enumerating
        }
        return TRUE;
    }
}

HWND getGDWindow() {
    if (g_cachedWindow && IsWindow(g_cachedWindow)) {
        return g_cachedWindow;
    }

    g_cachedWindow = nullptr;
    EnumWindows(findOwnTopLevelWindow, reinterpret_cast<LPARAM>(&g_cachedWindow));

    if (!g_cachedWindow) {
        log::warn("[Browser] Could not locate the GD window via EnumWindows (title == \"Geometry Dash\")");
    }
    return g_cachedWindow;
}

void invalidateGDWindowCache() {
    g_cachedWindow = nullptr;
}

void syncChildToRect(HWND child, const Rect& rect) {
    if (!child || !IsWindow(child)) return;

    if (rect.w <= 0.f || rect.h <= 0.f) {
        hideChild(child);
        return;
    }

    const int x = static_cast<int>(std::lround(rect.x));
    const int y = static_cast<int>(std::lround(rect.y));
    const int w = static_cast<int>(std::lround(rect.w));
    const int h = static_cast<int>(std::lround(rect.h));
    SetWindowPos(
        child, HWND_TOP,
        x, y, w, h,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
}

void hideChild(HWND child) {
    if (!child || !IsWindow(child)) return;
    ShowWindow(child, SW_HIDE);
}

} // namespace browser::win

#endif // GEODE_IS_WINDOWS

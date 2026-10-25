#pragma once

#ifdef GEODE_IS_WINDOWS

#include <Windows.h>

namespace browser::win {
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    [[nodiscard]] Rect inset(float px) const {
        return Rect{x + px, y + px, w - px * 2.f, h - px * 2.f};
    }
};
HWND getGDWindow();

void invalidateGDWindowCache();

void syncChildToRect(HWND child, const Rect& rect);

void hideChild(HWND child);

} // namespace browser::win

#endif // GEODE_IS_WINDOWS

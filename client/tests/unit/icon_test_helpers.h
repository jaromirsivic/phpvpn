#pragma once
#include <windows.h>
#include <vector>
#include <cstring>
#include "../../apps/tray/resource.h"

namespace phpvpn::tray::test {
inline std::vector<unsigned char> iconPixels(HICON icon, int width, int height) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    const auto dc = CreateCompatibleDC(nullptr);
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    std::vector<unsigned char> result;
    if (dc && bitmap && pixels) {
        const auto old = SelectObject(dc, bitmap);
        const auto size = static_cast<std::size_t>(width * height * 4);
        std::memset(pixels, 0x73, size);
        if (DrawIconEx(dc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL)) {
            auto* begin = static_cast<unsigned char*>(pixels);
            result.assign(begin, begin + size);
        }
        SelectObject(dc, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    return result;
}
inline bool applicationIconsMatch(HWND window) {
    if (!window) return false;
    const auto dpi = GetDpiForWindow(window);
    for (const auto kind : {ICON_SMALL, ICON_BIG}) {
        const auto icon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, kind, 0));
        if (!icon) return false;
        const auto width = GetSystemMetricsForDpi(kind == ICON_SMALL ? SM_CXSMICON : SM_CXICON, dpi);
        const auto height = GetSystemMetricsForDpi(kind == ICON_SMALL ? SM_CYSMICON : SM_CYICON, dpi);
        const auto expected = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_PHPVPN_APP), IMAGE_ICON, width, height, 0));
        if (!expected) return false;
        const auto actualPixels = iconPixels(icon, width, height);
        const auto expectedPixels = iconPixels(expected, width, height);
        DestroyIcon(expected);
        if (actualPixels.empty() || actualPixels != expectedPixels) return false;
    }
    return true;
}
}

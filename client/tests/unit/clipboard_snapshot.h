#pragma once
#include <windows.h>
#include <ole2.h>
#include <vector>
#include <iostream>
namespace phpvpn::tray::test {
inline bool clipboardRestoreSucceeded = true;
inline void recordClipboardCheck(bool value) { clipboardRestoreSucceeded = clipboardRestoreSucceeded && value; }
class ClipboardSnapshot {
    std::vector<std::pair<UINT, HANDLE>> data_;
    HWND owner_;
    bool captured_ = false;
public:
    explicit ClipboardSnapshot(HWND owner) : owner_(owner) {}
    bool capture() {
        bool opened = false;
        for (int retry = 0; retry < 100 && !opened; ++retry) {
            opened = OpenClipboard(owner_) != FALSE;
            if (!opened) Sleep(10);
        }
        if (!opened) { std::cout << "[ EVIDENCE ] clipboard open unavailable: " << GetLastError() << '\n'; return false; }
        bool ok = true;
        for (UINT format = EnumClipboardFormats(0); format; format = EnumClipboardFormats(format)) {
            auto source = GetClipboardData(format);
            auto copy = source ? OleDuplicateData(source, static_cast<CLIPFORMAT>(format), 0) : nullptr;
            // Registered zero-byte marker formats have valid discarded HGLOBALs.
            // OleDuplicateData cannot lock those, but their exact empty value is
            // reproducible without reading or dropping any clipboard format.
            if (!copy && source && format >= 0xc000 && GlobalFlags(source) != GMEM_INVALID_HANDLE) {
                SetLastError(ERROR_SUCCESS);
                const auto bytes = GlobalSize(source);
                if (!bytes && GetLastError() == ERROR_SUCCESS) copy = GlobalAlloc(GMEM_MOVEABLE, 0);
            }
            if (!copy) {
                wchar_t name[256]{}; GetClipboardFormatNameW(format, name, 256);
                std::wcout << L"[ EVIDENCE ] clipboard format cannot be duplicated: " << format << L" " << name << L", source=" << (source != nullptr) << L"\n";
                ok = false; break;
            }
            data_.emplace_back(format, copy);
        }
        CloseClipboard();
        captured_ = ok;
        return ok;
    }
    ~ClipboardSnapshot() {
        if (captured_) {
            bool opened = false;
            for (int retry = 0; retry < 100 && !opened; ++retry) {
                opened = OpenClipboard(owner_) != FALSE;
                if (!opened) Sleep(10);
            }
            recordClipboardCheck(opened);
            if (opened) {
                recordClipboardCheck(EmptyClipboard());
                for (auto& [format, handle] : data_) {
                    if (SetClipboardData(format, handle)) handle = nullptr;
                    else recordClipboardCheck(false);
                }
                CloseClipboard();
            }
        }
        for (auto [format, handle] : data_) if (handle) {
            if (format == CF_BITMAP || format == CF_PALETTE || format == CF_DSPBITMAP) DeleteObject(handle);
            else if (format == CF_ENHMETAFILE || format == CF_DSPENHMETAFILE) DeleteEnhMetaFile(static_cast<HENHMETAFILE>(handle));
            else {
                if (format == CF_METAFILEPICT || format == CF_DSPMETAFILEPICT) {
                    auto picture = static_cast<METAFILEPICT*>(GlobalLock(handle));
                    if (picture) { DeleteMetaFile(picture->hMF); GlobalUnlock(handle); }
                }
                GlobalFree(handle);
            }
        }
    }
};

}

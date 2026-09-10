#include "window_utils.h"
#include <initguid.h>
#include "contracts_window.h"
#include "system_dialog.h"
#include "ui_controls.h"
#include <windowsx.h>

#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
namespace phpvpn::tray {
namespace {
constexpr wchar_t rootClass[] = L"PhpVpnContracts", paneClass[] = L"PhpVpnContractsPane";
constexpr UINT resultMessage = WM_APP + 80;
constexpr UINT pageMessage = WM_APP + 81;
constexpr UINT_PTR timer = 80;
constexpr int firstControl = 1000, checkId = 800, repeatId = 801;
constexpr int pageSize = 20, firstPageControl = 900;
}
ContractsWindow::~ContractsWindow() { endSession(); }
bool ContractsWindow::open(HINSTANCE instance, HWND owner, std::string_view document, std::string path, std::string revision) {
    if (window_) { activate(); return true; }
    if (ending_) return false;
    if (!cryptoFactory_ || !createWindow_) return false;
    auto crypto = cryptoFactory_();
    if (!crypto) return false;
    crypto_ = std::move(crypto).value();
    if (!controller_.load(*crypto_, document, std::move(path), std::move(revision))) { crypto_.reset(); return false; }
    instance_ = instance;
    x_ = y_ = width_ = rowHeight_ = 0;
    bodyScroll_ = 0;
    page_ = 0;
    saveText_ = L"Save";
    validationNotice_ = false;
    comInitialised_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&accessibility_)))) { endSession(); ending_ = false; return false; }
    for (const auto* name : {rootClass, paneClass}) {
        WNDCLASSEXW type{sizeof(type)}; type.hInstance = instance; type.lpfnWndProc = procedure;
        type.lpszClassName = name; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { endSession(); ending_ = false; return false; }
    }
    window_ = createWindow_(WS_EX_COMPOSITED | WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, rootClass, L"Communication contracts",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 1, 1, owner, nullptr, instance, this);
    if (!window_) { endSession(); ending_ = false; return false; }
    body_ = createWindow_(WS_EX_CONTROLPARENT,paneClass,L"Contracts content",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,
        0,0,1,1,window_,nullptr,instance,this);
    if (!body_) { endSession(); ending_ = false; return false; }
    auto control = [&](HWND parent, const wchar_t* kind, const wchar_t* text, DWORD style, int id = 0) {
        return createWindow_(0, kind, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    };
    heading_ = control(body_, L"STATIC", L"Communication contracts", SS_NOPREFIX);
    description_ = control(body_, L"STATIC", L"Manage secure communication between this computer and other virtual IP addresses using end-to-end encryption.", SS_NOPREFIX);
    localLabel_ = control(body_, L"STATIC", L"Your virtual IP", SS_NOPREFIX);
    local_ = control(body_, L"STATIC", widen(controller_.model()->localIp().toString()).c_str(), SS_NOPREFIX);
    notice_ = control(body_, L"STATIC", L"", SS_NOPREFIX);
    table_ = createWindow_(WS_EX_CONTROLPARENT, paneClass, L"Contracts table", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 1, 1, body_, nullptr, instance, this);
    viewport_ = createWindow_(WS_EX_CONTROLPARENT, paneClass, L"Communication contracts table",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL, 0, 0, 1, 1, table_, nullptr, instance, this);
    // Only the current page owns native row controls. The controller's model
    // retains every draft by remote IP independently of these HWNDs.
    content_ = createWindow_(WS_EX_CONTROLPARENT, paneClass, L"Contracts rows", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 1, 1, viewport_, nullptr, instance, this);
    pager_ = createWindow_(WS_EX_CONTROLPARENT, paneClass, L"Contract pages", WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 1, 1, body_, nullptr, instance, this);
    error_ = control(window_, L"STATIC", L"", SS_NOPREFIX | SS_NOTIFY, 802);
    check_ = control(window_, L"BUTTON", L"Check current configuration", WS_TABSTOP | BS_OWNERDRAW, checkId);
    repeat_ = control(window_, L"BUTTON", L"Save draft again", WS_TABSTOP | BS_OWNERDRAW, repeatId);
    cancel_ = control(window_, L"BUTTON", L"Cancel", WS_TABSTOP | BS_OWNERDRAW, IDCANCEL);
    ok_ = control(window_, L"BUTTON", L"Save", WS_TABSTOP | BS_OWNERDRAW, IDOK);
    bool complete = heading_ && description_ && local_ && localLabel_ && notice_ && table_ && pager_ && viewport_ && content_ && error_ && check_ && repeat_ && cancel_ && ok_;
    complete = attachModernButton(check_, resources_) && complete;
    complete = attachModernButton(repeat_, resources_) && complete;
    complete = attachModernButton(cancel_, resources_) && complete;
    complete = attachModernButton(ok_, resources_, ButtonRole::primary) && complete;
    if (!complete || !createRows() || !createPager()) { endSession(); ending_ = false; return false; }
    fonts(); sizeAndCenter(window_, ui::contractsClient); layout(); refresh();
    SetTimer(window_, timer, 50, nullptr);
    activate(); if (!rows_.empty()) SetFocus(rows_.front().controls[0]);
    return true;
}
bool ContractsWindow::createRows() {
    auto control = [&](HWND parent, const wchar_t* kind, const wchar_t* text, DWORD style, int id) {
        const bool modernText = std::wstring_view(kind) == L"EDIT";
        auto child = createWindow_(0, kind, text, WS_CHILD | (modernText ? 0 : WS_VISIBLE) | style, 0, 0, 1, 1, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (modernText && child) {
            if (!attachModernTextBox(child, resources_)) { DestroyWindow(child); return HWND{}; }
            ShowWindow(child, SW_SHOWNA);
        }
        return child;
    };
    bool complete = true;
    rows_.reserve(pageSize);
    changing_ = true;
    const auto sources = controller_.model()->rows();
    const auto start = static_cast<std::size_t>(page_) * pageSize;
    for (const auto& source : sources.subspan(start, (std::min)(sources.size() - start, static_cast<std::size_t>(pageSize)))) {
        rows_.emplace_back(); auto& row = rows_.back(); row.remote = source.remoteIp();
        row.identity = widen(controller_.model()->target(row.remote)->contractId());
        const auto base = firstControl + static_cast<int>(rows_.size() - 1) * 4;
        row.controls[0] = control(content_, L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW, base);
        complete = attachToggleSwitch(row.controls[0], resources_) && complete;
        row.controls[1] = control(content_, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, base + 1);
        setTextBoxInvalid(row.controls[1], source.keyErrorVisible());
        row.controls[2] = control(content_, L"BUTTON", L"Change", WS_TABSTOP | BS_OWNERDRAW, base + 2);
        complete = attachModernButton(row.controls[2], resources_, ButtonRole::neutral, ui::Color::surface) && complete;
        const wchar_t* names[]{L"Enabled", L"Key", L"Change"};
        for (std::size_t c = 0; c < 3; ++c) {
            complete = complete && row.controls[c];
            if (!row.controls[c]) continue;
            complete = SetWindowSubclass(row.controls[c], fieldProcedure, 2, reinterpret_cast<DWORD_PTR>(this)) && complete;
            const auto name = row.identity + L" " + names[c];
            complete = SUCCEEDED(accessibility_->SetHwndPropStr(row.controls[c], static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME, name.c_str())) && complete;
        }
        complete = row.edit.attach(row.controls[1], 128,
            [this, remote = row.remote](std::wstring_view text) { return text.size() <= 128 && acceptKey(remote, text); }) && complete;
        SendMessageW(row.controls[1], EM_SETLIMITTEXT, 128, 0);
        syncRow(row);
    }
    changing_ = false;
    return complete;
}
void ContractsWindow::activate() const { if (window_) reactivateWindow(window_); }
int ContractsWindow::pageCount() const {
    return (std::max)(1, (static_cast<int>(controller_.model()->rows().size()) + pageSize - 1) / pageSize);
}
void ContractsWindow::destroyRows() {
    const bool wasChanging = changing_;
    changing_ = true;
    for (auto& row : rows_) {
        row.edit.clear();
        for (auto field : row.controls) if (field) {
            const MSAAPROPID props[]{PROPID_ACC_NAME, PROPID_ACC_DESCRIPTION};
            if (accessibility_) accessibility_->ClearHwndProps(field, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, props, 2);
            DestroyWindow(field);
        }
    }
    rows_.clear();
    changing_ = wasChanging;
}
bool ContractsWindow::createPager() {
    for (const auto& button : pageButtons_) {
        const MSAAPROPID props[]{PROPID_ACC_NAME};
        if (accessibility_) accessibility_->ClearHwndProps(button.window, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, props, 1);
        DestroyWindow(button.window);
    }
    pageButtons_.clear();
    const int count = pageCount();
    ShowWindow(pager_, count > 1 ? SW_SHOW : SW_HIDE);
    if (count <= 1) return true;
    bool complete = true;
    auto add = [&](std::wstring label, int page, std::wstring name, bool active = false) {
        const int id = firstPageControl + static_cast<int>(pageButtons_.size());
        const bool ellipsis = page < 0;
        auto button = createWindow_(0, ellipsis ? L"STATIC" : L"BUTTON", label.c_str(),
            WS_CHILD | WS_VISIBLE | (ellipsis ? SS_CENTER | SS_CENTERIMAGE : WS_TABSTOP | BS_OWNERDRAW),
            0, 0, 1, 1, pager_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        pageButtons_.push_back({button, page});
        if (!button) { complete = false; return; }
        if (!ellipsis) {
            complete = attachModernButton(button, resources_, active ? ButtonRole::primary : ButtonRole::neutral) && complete;
            complete = SetWindowSubclass(button, fieldProcedure, 2, reinterpret_cast<DWORD_PTR>(this)) && complete;
            // Only Save is the dialog's default action, even for a blue page button.
            SendMessageW(button, BM_SETSTYLE, BS_PUSHBUTTON, FALSE);
            complete = SUCCEEDED(accessibility_->SetHwndPropStr(button, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF,
                PROPID_ACC_NAME, name.c_str())) && complete;
        }
        EnableWindow(button, !ellipsis && page != page_);
    };
    add(L"\u00ab", 0, L"First page");
    add(L"\u2039", (std::max)(0, page_ - 1), L"Previous page");
    std::vector<int> pages{0};
    const int start = count <= 7 ? 1 : std::clamp(page_ - 2, 1, count - 6);
    const int end = count <= 7 ? count - 2 : start + 4;
    for (int page = start; page <= end; ++page) pages.push_back(page);
    pages.push_back(count - 1);
    int previous = -1;
    for (int page : pages) {
        if (page > previous + 1) add(L"\u2026", -1, L"");
        add(std::to_wstring(page + 1), page,
            L"Page " + std::to_wstring(page + 1) + L" of " + std::to_wstring(count) + (page == page_ ? L", current page" : L""), page == page_);
        // The current number remains focusable and uses the active color.
        if (page == page_) EnableWindow(pageButtons_.back().window, TRUE);
        previous = page;
    }
    add(L"\u203a", (std::min)(count - 1, page_ + 1), L"Next page");
    add(L"\u00bb", count - 1, L"Last page");
    return complete;
}
void ContractsWindow::changePage(int page) {
    if (!window_ || ending_ || !controller_.model() || controller_.model()->frozen() || page < 0 || page >= pageCount() || page == page_) return;
    LayoutFrame frame(window_);
    const auto focused = GetFocus();
    const bool pagerFocus = focused && IsChild(pager_, focused);
    const int previous = page_;
    SetFocus(ok_);
    destroyRows();
    page_ = page;
    if (!createRows()) {
        destroyRows(); page_ = previous;
        if (!createRows()) destroyRows();
        localError_ = L"The page could not be displayed. Your draft has been kept. Try another page.";
    }
    if (!createPager()) localError_ = L"Page navigation could not be displayed. Your draft has been kept.";
    y_ = 0;
    fonts(); layout(); rowPositions(); refresh();
    HWND next = rows_.empty() ? ok_ : rows_.front().controls[0];
    if (pagerFocus) for (const auto& button : pageButtons_)
        if (button.page == page_ && IsWindowEnabled(button.window)) { next = button.window; break; }
    SetFocus(next);
}
void ContractsWindow::focusContract(Ipv4Address remote, int column) {
    const auto sources = controller_.model()->rows();
    for (std::size_t i = 0; i < sources.size(); ++i) if (sources[i].remoteIp() == remote) {
        changePage(static_cast<int>(i / pageSize));
        for (const auto& row : rows_) if (row.remote == remote) SetFocus(row.controls[column]);
        break;
    }
}
void ContractsWindow::syncRow(Row& row) {
    const auto* source = controller_.model()->row(row.remote); if (!source) return;
    const bool wasChanging = changing_;
    changing_ = true; auto key = widen(source->keyText()); row.edit.set(key);
    secureWipe(key.data(), key.size() * sizeof(wchar_t));
    SendMessageW(row.controls[0], BM_SETCHECK, source->enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    changing_ = wasChanging; InvalidateRect(row.controls[1], nullptr, TRUE);
}
bool ContractsWindow::acceptKey(Ipv4Address remote, std::wstring_view text) {
    if (changing_) return true;
    if (!controller_.model() || controller_.model()->frozen()) return false;
    const auto* source = controller_.model()->row(remote);
    if (!source || !source->enabled()) return false;
    std::wstring wide(text); auto utf8 = narrow(wide); secureWipe(wide.data(), wide.size() * sizeof(wchar_t));
    if ((!text.empty() && utf8.empty()) || text.find(L'\0') != std::wstring_view::npos || text.find_first_of(L"\r\n") != std::wstring_view::npos) {
        localError_ = L"Key contains unsupported text."; refresh(); return false;
    }
    auto accepted = controller_.model()->setKey(remote, utf8); secureWipe(utf8.data(), utf8.size());
    if (!accepted) { localError_ = L"The combined Key text exceeds 524288 bytes. The previous value has been kept."; refresh(); return false; }
    localError_.clear(); return true;
}
void ContractsWindow::command(int id, int code) {
    if (ending_ || !controller_.model()) return;
    if (id == IDCANCEL) { close(); return; }
    if (id == IDOK) { save(false); return; }
    if (id == checkId) { controller_.check(window_, resultMessage); refresh(); return; }
    if (id == repeatId) { save(true); return; }
    if (id >= firstPageControl && id < firstPageControl + static_cast<int>(pageButtons_.size())) {
        if (code == BN_CLICKED) PostMessageW(window_, pageMessage, pageButtons_[id - firstPageControl].page, 0);
        return;
    }
    if (id == 802) {
        if (auto focus = controller_.model()->firstBlockingFocus()) focusContract(focus->remoteIp, 1);
        return;
    }
    const int index = (id - firstControl) / 4, column = (id - firstControl) % 4;
    if (id < firstControl || index < 0 || static_cast<std::size_t>(index) >= rows_.size() || changing_ || controller_.model()->frozen()) return;
    auto& row = rows_[index];
    if (column == 1 && code == EN_CHANGE) {
        const int n = GetWindowTextLengthW(row.controls[1]); std::wstring text(n + 1, L'\0');
        GetWindowTextW(row.controls[1], text.data(), n + 1); text.resize(n);
        acceptKey(row.remote, text); secureWipe(text.data(), text.size() * sizeof(wchar_t));
        controller_.changed(); refresh(); return;
    }
    if (code != BN_CLICKED) return;
    if (column == 0) {
        (void)controller_.model()->setEnabled(row.remote, SendMessageW(row.controls[0], BM_GETCHECK, 0, 0) == BST_CHECKED);
        controller_.changed(); refresh(); return;
    }
    const auto target = controller_.model()->target(row.remote); if (!target) return;
    if (column == 2) {
        if (!controller_.model()->row(row.remote)->enabled()) return;
        std::erase_if(children_, [](const auto& child) { return !child->window(); });
        auto child = std::make_unique<PasswordShaWindow>(*crypto_, *target, [this](const ContractTarget& captured, std::string_view key) {
            if (ending_ || !controller_.model()) return false;
            const auto* source = controller_.model()->row(captured.remoteIp());
            if (!source || !source->enabled()) return false;
            auto applied = controller_.model()->applyPasswordKey(captured, key); if (!applied) return false;
            controller_.changed();
            for (auto& targetRow : rows_) if (targetRow.remote == captured.remoteIp()) syncRow(targetRow);
            localError_.clear(); refresh(); return true;
        }, [this](const ContractTarget& target) {
            if (ending_ || !window_ || !controller_.model() || controller_.model()->frozen()) return;
            focusContract(target.remoteIp(), controller_.model()->row(target.remoteIp())->enabled() ? 2 : 1);
        });
        if (child->open(instance_, window_)) children_.push_back(std::move(child));
        else { localError_ = L"Password SHA could not be opened."; refresh(); }
    }
}
void ContractsWindow::save(bool repeat) {
    if (!window_ || !controller_.model() || lifecycleSnapshot().saving) return;
    if (!repeat && !controller_.model()->dirty()) return;
    if (repeat) {
        if (!controller_.canRepeat()) return;
        const auto before = lifecycleSnapshot();
        if (!confirmSystemAction(window_, L"Replace the current whole configuration with your draft?", L"Save draft again") || !window_ || lifecycleSnapshot() != before) return;
    }
    auto prepared = controller_.prepare(repeat);
    if (!prepared) { refresh(); return; }
    notifyAfterSave_ = connectionActive_ && controller_.model()->dirty();
    closeChildren(); controller_.dispatch(window_, resultMessage); refresh();
}
void ContractsWindow::poll() {
    const auto event = controller_.poll();
    if (event == ContractsEvent::saved) {
        const auto before = lifecycleSnapshot();
        if (notifyAfterSave_)
            showSystemMessage(window_, L"Your changes have been saved. Disconnect and connect again to apply them.",
                L"Configuration saved", SystemDialogIcon::information, L"Configuration saved");
        if (window_ && lifecycleSnapshot() == before) { endSession(); ending_ = false; }
        return;
    }
    if (event == ContractsEvent::matched) {
        const auto before = lifecycleSnapshot();
        showSystemMessage(window_, L"The current configuration matches your changes.", L"Communication contracts", SystemDialogIcon::information);
        if (window_ && lifecycleSnapshot() == before) { endSession(); ending_ = false; }
        return;
    }
    if (event != ContractsEvent::none) refresh();
}
void ContractsWindow::refresh() {
    if (!window_ || !ok_ || !controller_.model()) return;
    const auto sources = controller_.model()->rows();
    const auto changes = std::count_if(sources.begin(), sources.end(), [](const auto& row) { return row.dirty(); });
    const auto saveText = changes == 0 ? std::wstring(L"Save") :
        L"Save (" + std::to_wstring(changes) + (changes == 1 ? L" change)" : L" changes)");
    const bool saveTextChanged = saveText != saveText_;
    if (saveTextChanged) { saveText_ = saveText; SetWindowTextW(ok_, saveText_.c_str()); }
    const auto snapshot = lifecycleSnapshot();
    EnableWindow(viewport_, !snapshot.saving && !snapshot.unknown);
    EnableWindow(pager_, !snapshot.saving && !snapshot.unknown);
    EnableWindow(ok_, changes > 0 && controller_.model()->canPrepareSave());
    ShowWindow(ok_, snapshot.unknown ? SW_HIDE : SW_SHOW);
    ShowWindow(check_, snapshot.unknown ? SW_SHOW : SW_HIDE);
    ShowWindow(repeat_, controller_.canRepeat() ? SW_SHOW : SW_HIDE);
    EnableWindow(check_, !snapshot.checking);
    const auto blockingFocus = controller_.model()->firstBlockingFocus();
    const bool validationNotice = localError_.empty() && blockingFocus.has_value();
    const bool noticeStyleChanged = validationNotice != validationNotice_;
    validationNotice_ = validationNotice;
    if (noticeStyleChanged) SendMessageW(error_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(validationNotice_ ? ui::Font::strongValue : ui::Font::label)), TRUE);
    std::wstring message = controller_.notice();
    if (!localError_.empty()) message = localError_;
    else if (blockingFocus) message = widen(kContractKeyError) + L"\nFirst invalid peer: " + widen(blockingFocus->remoteIp.toString());
    const int length = GetWindowTextLengthW(error_);
    std::wstring previous(static_cast<std::size_t>(length)+1,L'\0'); GetWindowTextW(error_,previous.data(),length+1); previous.resize(length);
    if (message != previous) SetWindowTextW(error_, message.c_str());
    ShowWindow(error_,message.empty()?SW_HIDE:SW_SHOW);
    SetWindowTextW(notice_, connectionActive_ ? L"Changes take effect on the next connection." : L"");
    ShowWindow(notice_, connectionActive_ ? SW_SHOW : SW_HIDE);
    for (auto& row : rows_) {
        // Disable the native checkbox itself so its visual and accessible
        // unavailable state agree with the frozen table, including repaint.
        EnableWindow(row.controls[0], !snapshot.saving && !snapshot.unknown);
        EnableWindow(row.controls[1], !snapshot.saving && !snapshot.unknown);
        const auto* source = controller_.model()->row(row.remote);
        const bool readOnly = !source->enabled();
        if (((GetWindowLongPtrW(row.controls[1], GWL_STYLE) & ES_READONLY) != 0) != readOnly)
            SendMessageW(row.controls[1], EM_SETREADONLY, readOnly, 0);
        EnableWindow(row.controls[2], source->enabled() && !snapshot.saving && !snapshot.unknown);
        const bool dirty = source->dirty();
        const auto background = dirty ? ui::Color::contractsChanged : ui::Color::surface;
        setModernControlBackground(row.controls[1], background);
        setModernControlBackground(row.controls[2], background);
        if (dirty != row.lastDirty) {
            row.lastDirty = dirty;
            const int top = static_cast<int>(&row - rows_.data()) * rowHeight_;
            RECT area{0, top, width_, top + rowHeight_};
            InvalidateRect(content_, &area, FALSE);
            InvalidateRect(row.controls[0], nullptr, FALSE);
        }
        const bool valid = !source->keyErrorVisible();
        setTextBoxInvalid(row.controls[1], !valid);
        if (valid != row.lastValid) {
            row.lastValid = valid; InvalidateRect(row.controls[1], nullptr, FALSE);
            if (accessibility_) accessibility_->SetHwndPropStr(row.controls[1],static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,
                PROPID_ACC_DESCRIPTION, valid ? L"" : L"Invalid: Key must contain exactly 64 lowercase hexadecimal characters.");
            NotifyWinEvent(EVENT_OBJECT_DESCRIPTIONCHANGE,row.controls[1],OBJID_CLIENT,CHILDID_SELF);
        }
    }
    for (const auto& child : children_) if (child->window()) {
        const auto* source = controller_.model()->row(child->remoteIp());
        child->setTargetEnabled(source && source->enabled() && !snapshot.saving && !snapshot.unknown);
    }
    if (message != previous || saveTextChanged || noticeStyleChanged) { layout(); reveal(GetFocus()); }
}
void ContractsWindow::setConnectionActive(bool active) { if (connectionActive_ != active) { connectionActive_ = active; refresh(); layout(); } }
void ContractsWindow::closeChildren() { for (auto& child : children_) child->close(); children_.clear(); }
void ContractsWindow::close() {
    if (!window_) return;
    const auto before = lifecycleSnapshot();
    if (before.saving) return;
    if (before.unknown || before.dirty) {
        const auto* message = before.unknown ? L"The file may already have changed. Close without checking the save result?" : L"Discard unsaved changes?";
        if (!confirmSystemAction(window_, message, L"Communication contracts")) return;
    }
    approvedDiscard(before);
}
bool ContractsWindow::approvedDiscard(const ContractsLifecycleSnapshot& approved) {
    if (!window_ || approved.saving || lifecycleSnapshot() != approved) return false;
    endSession(); ending_ = false; return true;
}
void ContractsWindow::endSession() {
    ending_ = true; closeChildren();
    if (window_) DestroyWindow(window_);
    controller_.close(); rows_.clear(); pageButtons_.clear(); crypto_.reset();
    if (accessibility_) { accessibility_->Release(); accessibility_ = nullptr; }
    if (comInitialised_) { CoUninitialize(); comInitialised_ = false; }
}
bool ContractsWindow::translate(MSG& message) const {
    for (const auto& child : children_) if (child->translate(message)) return true;
    if (!window_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd))) return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { SendMessageW(window_, WM_CLOSE, 0, 0); return true; }
    return IsDialogMessageW(window_, &message) != FALSE;
}
void ContractsWindow::fonts() {
    if (!resources_.update(instance_, static_cast<int>(GetDpiForWindow(window_)), false)) return;
    for (auto h : {description_, localLabel_, notice_, error_, cancel_, ok_, check_, repeat_}) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(h == error_ && validationNotice_ ? ui::Font::strongValue : ui::Font::label)), TRUE);
    SendMessageW(local_,WM_SETFONT,reinterpret_cast<WPARAM>(resources_.font(ui::Font::strongValue)),TRUE);
    SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::heading)), TRUE);
    for (auto& row : rows_) for (std::size_t c = 0; c < 3; ++c) SendMessageW(row.controls[c], WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(c == 1 ? ui::Font::mono : ui::Font::label)), TRUE);
    for (const auto& button : pageButtons_) SendMessageW(button.window, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::label)), TRUE);
}
void ContractsWindow::layout() {
    if (!ok_ || layingOut_ || IsIconic(window_)) return;
    LayoutFrame frame(window_);
    layingOut_ = true;
    RECT r{}; GetClientRect(window_, &r); const auto s = [this](int v) { return scaleForDpi(v, resources_.dpi()); };
    const int margin = s(r.right < s(700) ? 16 : 32), right = (std::max)(margin, static_cast<int>(r.right) - margin);
    const bool compact = r.right < s(900);
    const int textRight = compact ? right : right-s(180);
    const auto dc=GetDC(window_);
    const int cancelWidth = s(104), buttonGap = s(8);
    const int saveWidth = (std::min)((std::max)(1, right-margin-cancelWidth-buttonGap),
        (std::max)(s(180), measuredButtonWidth(dc, resources_.font(ui::Font::label), saveText_, resources_.dpi())));
    const int headingBottom=s(24)+measureText(dc,resources_.font(ui::Font::heading),L"Communication contracts",(std::max)(1,textRight-margin),true).height;
    const int descriptionTop=(std::max)(s(64),headingBottom+s(4));
    const int descriptionBottom=descriptionTop+measureText(dc,resources_.font(ui::Font::label),L"Manage secure communication between this computer and other virtual IP addresses using end-to-end encryption.",(std::max)(1,textRight-margin),true).height;
    const int localTop=compact?descriptionBottom+s(12):s(28);
    const int tableTop=(std::max)(s(compact?140:104),compact?localTop+s(32):descriptionBottom+s(12))+(connectionActive_?s(28):0);
    const int errorLength=GetWindowTextLengthW(error_); std::wstring errorText(static_cast<std::size_t>(errorLength)+1,L'\0');
    GetWindowTextW(error_,errorText.data(),errorLength+1); errorText.resize(errorLength);
    const int errorHeight=errorText.empty()?0:measureText(dc,resources_.font(validationNotice_ ? ui::Font::strongValue : ui::Font::label),errorText,(std::max)(1,right-margin),true).height+s(12); ReleaseDC(window_,dc);
    const bool stackActions=IsWindowVisible(check_) && right-margin<s(528)+cancelWidth+buttonGap;
    const int footer = (std::max)(0,static_cast<int>(r.bottom)-s(stackActions?148:60)-errorHeight);
    const int buttonTop=static_cast<int>(r.bottom)-s(44);
    positionControls(window_, std::vector<ControlPlacement>{
        {body_, {0, 0, r.right, footer}},
        {error_, {margin,footer+s(6),right,footer+(std::max)(s(6),errorHeight)}},
        {check_, {margin,buttonTop-s(stackActions?88:0),margin+s(236),buttonTop-s(stackActions?88:0)+s(36)}},
        {repeat_, {margin+s(stackActions?0:244),buttonTop-s(stackActions?44:0),margin+s(stackActions?164:408),buttonTop-s(stackActions?44:0)+s(36)}},
        {cancel_, {right-saveWidth-buttonGap-cancelWidth,buttonTop,right-saveWidth-buttonGap,buttonTop+s(36)}},
        {ok_, {right-saveWidth,buttonTop,right,buttonTop+s(36)}}});
    // Reserve the possible body scrollbar before determining wrapped rows.
    const int pagerColumns = (std::max)(1, (right - margin - GetSystemMetricsForDpi(SM_CXVSCROLL, resources_.dpi())) / s(40));
    const int pagerRows = (static_cast<int>(pageButtons_.size()) + pagerColumns - 1) / pagerColumns;
    const int pagerHeight = pageCount() > 1 ? s(12) + pagerRows * s(40) : 0;
    const int bodyExtent=(std::max)(footer,tableTop+s(112)+pagerHeight);
    const int tableBottom = bodyExtent - pagerHeight;
    SCROLLINFO bodyInfo{sizeof(bodyInfo),SIF_RANGE|SIF_PAGE|SIF_POS};
    bodyInfo.nMax=bodyExtent-1; bodyInfo.nPage=footer;
    bodyScroll_=std::clamp(bodyScroll_,0,(std::max)(0,bodyExtent-footer)); bodyInfo.nPos=bodyScroll_;
    SetScrollInfo(body_,SB_VERT,&bodyInfo,TRUE);
    RECT bodyClient{};GetClientRect(body_,&bodyClient);
    const int bodyRight=(std::min)(right,static_cast<int>(bodyClient.right)-margin);
    std::vector<ControlPlacement> bodyPositions{
        {heading_, {margin,s(24),textRight,headingBottom}}, {description_, {margin,descriptionTop,textRight,descriptionBottom}},
        {localLabel_, {compact?margin:right-s(150),localTop,compact?margin+s(110):right,localTop+s(24)}},
        {local_, {compact?margin+s(124):right-s(150),compact?localTop:s(56),right,compact?localTop+s(28):s(84)}},
        {notice_, {margin,tableTop-s(28),right,tableTop-s(4)}},
        {table_, {margin,tableTop,bodyRight,tableBottom}},
        {pager_, {margin,tableBottom,bodyRight,bodyExtent}}};
    for(auto& placement:bodyPositions) { placement.rect.top-=bodyScroll_; placement.rect.bottom-=bodyScroll_; placement.rect.right=(std::min)(placement.rect.right,bodyRight); }
    positionControls(body_,bodyPositions);
    const int tableWidth = (std::max)(1, bodyRight - margin), tableHeight = tableBottom - tableTop;
    // The table owns the complete antialiased shape and header. Native children
    // stay in its rectangular interior, clear of the bottom corner arcs.
    positionControls(table_, std::vector<ControlPlacement>{
        {viewport_, {1,s(40),tableWidth-1,tableHeight-s(8)-1}}});
    std::vector<ControlPlacement> pagerPositions;
    const int columns = pagerColumns;
    for (std::size_t i = 0; i < pageButtons_.size(); ++i) {
        const int column = static_cast<int>(i) % columns, row = static_cast<int>(i) / columns;
        const int count = (std::min)(columns, static_cast<int>(pageButtons_.size()) - row * columns);
        const int left = (std::max)(0, (tableWidth - count * s(40)) / 2) + column * s(40);
        pagerPositions.push_back({pageButtons_[i].window, {left,s(8)+row*s(40),left+s(36),s(44)+row*s(40)}});
    }
    positionControls(pager_, pagerPositions);

    RECT v{}; GetClientRect(viewport_, &v);
    const auto oldWidth=width_,oldRowHeight=rowHeight_;
    keyWidth_ = (std::max)(s(260), static_cast<int>(v.right) - s(446)); width_ = s(446) + keyWidth_; rowHeight_ = s(48);
    SCROLLINFO vertical{sizeof(vertical), SIF_RANGE | SIF_PAGE | SIF_POS};
    vertical.nMax = static_cast<int>(rows_.size()) * rowHeight_ - 1; vertical.nPage = v.bottom;
    y_ = std::clamp(y_, 0, (std::max)(0, vertical.nMax + 1 - static_cast<int>(vertical.nPage))); vertical.nPos = y_;
    SetScrollInfo(viewport_, SB_VERT, &vertical, TRUE);
    SCROLLINFO horizontal{sizeof(horizontal), SIF_RANGE | SIF_PAGE | SIF_POS};
    horizontal.nMax = width_ - 1; horizontal.nPage = v.right;
    x_ = std::clamp(x_, 0, (std::max)(0, width_ - static_cast<int>(v.right))); horizontal.nPos = x_;
    SetScrollInfo(viewport_, SB_HORZ, &horizontal, TRUE);
    // Scrollbar visibility changes the viewport's client area. Use the final
    // page sizes, otherwise the last row cannot be completely revealed.
    GetClientRect(viewport_, &v);
    vertical.nPage=v.bottom;
    y_=std::clamp(y_,0,(std::max)(0,vertical.nMax+1-static_cast<int>(vertical.nPage))); vertical.nPos=y_;
    SetScrollInfo(viewport_,SB_VERT,&vertical,TRUE);
    GetClientRect(viewport_, &v); horizontal.nPage=v.right;
    x_=std::clamp(x_,0,(std::max)(0,width_-static_cast<int>(v.right))); horizontal.nPos=x_;
    SetScrollInfo(viewport_,SB_HORZ,&horizontal,TRUE);
    SetWindowPos(content_, nullptr, -x_, -y_, width_, static_cast<int>(rows_.size())*rowHeight_, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    if (oldWidth!=width_ || oldRowHeight!=rowHeight_) rowPositions();
    InvalidateRect(table_, nullptr, FALSE); InvalidateRect(viewport_, nullptr, FALSE);
    layingOut_ = false;
}
void ContractsWindow::rowPositions() {
    LayoutFrame frame(window_);
    const auto s = [this](int v) { return scaleForDpi(v, resources_.dpi()); };
    std::vector<ControlPlacement> positions; positions.reserve(rows_.size() * 3);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const int top = static_cast<int>(i) * rowHeight_ + s(6), action = s(330) + keyWidth_;
        const std::array<Rect,3> rects{{{s(262),top,s(318),top+s(36)}, {s(342),top,action-s(12),top+s(36)},
            {action+s(8),top,action+s(112),top+s(36)}}};
        for (std::size_t c = 0; c < 3; ++c) positions.push_back({rows_[i].controls[c], rects[c]});
    }
    positionControls(content_, positions);
}
void ContractsWindow::scroll(bool horizontal, int position) {
    LayoutFrame frame(window_);
    SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(viewport_, horizontal ? SB_HORZ : SB_VERT, &info);
    position = std::clamp(position, 0, (std::max)(0, info.nMax + 1 - static_cast<int>(info.nPage)));
    auto& value = horizontal ? x_ : y_; if (value == position) return;
    value = position; SetScrollPos(viewport_, horizontal ? SB_HORZ : SB_VERT, position, TRUE);
    SetWindowPos(content_, nullptr, -x_, -y_, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    InvalidateRect(viewport_, nullptr, FALSE); if (horizontal) InvalidateRect(table_, nullptr, FALSE);
}
void ContractsWindow::scrollBody(int position) {
    SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(body_,SB_VERT,&info);
    position=std::clamp(position,0,(std::max)(0,info.nMax+1-static_cast<int>(info.nPage)));
    if(position!=bodyScroll_) { bodyScroll_=position;layout();InvalidateRect(body_,nullptr,TRUE); }
}
void ContractsWindow::reveal(HWND field) {
    if (field && pager_ && IsChild(pager_, field)) {
        RECT area{}, bounds{}; GetClientRect(body_, &area); GetWindowRect(field, &bounds);
        MapWindowPoints(nullptr, body_, reinterpret_cast<POINT*>(&bounds), 2);
        if (bounds.top < 0) scrollBody(bodyScroll_ + bounds.top);
        else if (bounds.bottom > area.bottom) scrollBody(bodyScroll_ + bounds.bottom - area.bottom);
        return;
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) for (std::size_t c = 0; c < 3; ++c) if (rows_[i].controls[c] == field) {
        if (!controller_.model()) return;
        (void)controller_.model()->setFocus(ContractFocus{rows_[i].remote, (c == 2 ? ContractColumn::password : static_cast<ContractColumn>(c))});
        RECT v{}; GetClientRect(viewport_, &v);
        const int top=static_cast<int>(i)*rowHeight_+scaleForDpi(6,resources_.dpi());
        scroll(false, revealScrollPosition(y_, v.bottom, static_cast<int>(rows_.size()) * rowHeight_, {0,top,0,top+scaleForDpi(36,resources_.dpi())}));
        RECT f{}; GetWindowRect(field, &f); MapWindowPoints(nullptr, viewport_, reinterpret_cast<POINT*>(&f), 2);
        if (f.left < 0) scroll(true, x_ + f.left); else if (f.right > v.right) scroll(true, x_ + f.right - v.right);
        RECT area{};GetClientRect(body_,&area);GetWindowRect(field,&f);MapWindowPoints(nullptr,body_,reinterpret_cast<POINT*>(&f),2);
        if(f.top<0) scrollBody(bodyScroll_+f.top); else if(f.bottom>area.bottom) scrollBody(bodyScroll_+f.bottom-area.bottom);
        return;
    }
}
LRESULT CALLBACK ContractsWindow::fieldProcedure(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto* self = reinterpret_cast<ContractsWindow*>(data);
    if (m == WM_SETFOCUS) self->reveal(h);
    if (m == WM_KEYDOWN && GetDlgCtrlID(h) >= firstControl && (GetDlgCtrlID(h) - firstControl) % 4 != 1) {
        if (w == VK_END && GetKeyState(VK_CONTROL) < 0 && !self->rows_.empty()) {
            PostMessageW(self->window_, pageMessage, self->pageCount() - 1, 1); return 0;
        }
        if (w == VK_NEXT || w == VK_PRIOR) { RECT r{}; GetClientRect(self->viewport_, &r); self->scroll(false, self->y_ + (w == VK_NEXT ? r.bottom : -r.bottom)); return 0; }
    }
    if (m == WM_MOUSEWHEEL) { SendMessageW(GetParent(h) == self->pager_ ? self->body_ : self->viewport_, m, w, l); return 0; }
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, fieldProcedure, 2);
    return DefSubclassProc(h, m, w, l);
}
void ContractsWindow::paint(HWND target) {
    PAINTSTRUCT p{}; auto targetDc = BeginPaint(target, &p); PaintBuffer buffer(targetDc); auto dc = buffer.dc(); RECT r{}; GetClientRect(target, &r);
    FillRect(dc, &r, resources_.brush(target == table_ || target == window_ || target == body_ || target == pager_ ? ui::Color::background : ui::Color::surface));
    if (target == window_ && body_) {
        RECT bodyBounds{}; GetWindowRect(body_, &bodyBounds);
        MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&bodyBounds), 2);
        const RECT divider{0, bodyBounds.bottom, r.right, bodyBounds.bottom + 1};
        FillRect(dc, &divider, resources_.brush(ui::Color::separator));
    }
    if (target == table_) paintContractsTable(dc, {0,0,r.right,r.bottom}, scaleForDpi(40, resources_.dpi()), resources_);
    if (target == table_ || target == content_) {
        auto font = SelectObject(dc, resources_.font(target == table_ ? ui::Font::contractsHeader : ui::Font::label)); SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, resources_.color(target == table_ ? ui::Color::primaryText : ui::Color::text));
        const auto s = [this](int v) { return scaleForDpi(v, resources_.dpi()); };
        if (target == table_) {
            const wchar_t* labels[]{L"Contract",L"Enabled",L"Key",L"Actions"};
            const int lefts[]{0,s(250),s(330),s(330)+keyWidth_};
            const int inset = 1;
            const int saved = SaveDC(dc);
            IntersectClipRect(dc, 1, 1, r.right-1, scaleForDpi(40, resources_.dpi()));
            for (int c = 0; c < 4; ++c) drawText(dc, labels[c], {inset+lefts[c]+s(12)-x_,0,inset+(c == 3 ? width_ : lefts[c+1])-x_,s(40)}, DT_SINGLELINE | DT_VCENTER);
            RestoreDC(dc, saved);
        } else if (rowHeight_) {
            RECT clip{}; GetClipBox(dc,&clip);
            const auto first = static_cast<std::size_t>((std::max)(0L, clip.top) / rowHeight_);
            const auto last = (std::min)(rows_.size(), static_cast<std::size_t>(clip.bottom / rowHeight_ + 1));
            for (auto i = first; i < last; ++i) {
                const int top = static_cast<int>(i)*rowHeight_;
                if (controller_.model()->row(rows_[i].remote)->dirty()) {
                    RECT area{0, top, r.right, top+rowHeight_};
                    FillRect(dc, &area, resources_.brush(ui::Color::contractsChanged));
                }
                drawText(dc, rows_[i].identity, {s(12),top,s(250),top+rowHeight_}, DT_SINGLELINE | DT_VCENTER);
                RECT line{0,top+rowHeight_-1,r.right,top+rowHeight_}; FillRect(dc, &line, resources_.brush(ui::Color::separator));
            }
        }
        SelectObject(dc, font);
    }
    buffer.finish(); EndPaint(target, &p);
}
LRESULT CALLBACK ContractsWindow::procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<ContractsWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(l); self = static_cast<ContractsWindow*>(create->lpCreateParams);
        if (!(create->style & WS_CHILD)) self->window_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_COMMAND: self->command(LOWORD(w), HIWORD(w)); return 0;
    case WM_TIMER: if (h == self->window_ && w == timer) self->poll(); return 0;
    case resultMessage: self->poll(); return 0;
    case pageMessage:
        self->changePage(static_cast<int>(w));
        if (l == 1 && !self->rows_.empty() && self->controller_.model() && !self->controller_.model()->frozen()) SetFocus(self->rows_.back().controls[0]);
        return 0;
    case WM_SIZE: if (h == self->window_) { self->layout(); self->reveal(GetFocus()); } return 0;
    case WM_GETMINMAXINFO:
        if (h == self->window_) {
            const UINT dpi = GetDpiForWindow(h);
            RECT minimum{0, 0, scaleForDpi(920, static_cast<int>(dpi)), scaleForDpi(520, static_cast<int>(dpi))};
            AdjustWindowRectExForDpi(&minimum, static_cast<DWORD>(GetWindowLongPtrW(h, GWL_STYLE)), FALSE,
                static_cast<DWORD>(GetWindowLongPtrW(h, GWL_EXSTYLE)), dpi);
            auto* info = reinterpret_cast<MINMAXINFO*>(l);
            info->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &monitor)) {
                info->ptMinTrackSize.x = (std::min)(info->ptMinTrackSize.x, monitor.rcWork.right - monitor.rcWork.left);
                info->ptMinTrackSize.y = (std::min)(info->ptMinTrackSize.y, monitor.rcWork.bottom - monitor.rcWork.top);
            }
            return 0;
        }
        break;
    case WM_VSCROLL: case WM_HSCROLL: {
        if(h==self->body_) {
            SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(h,SB_VERT,&info);int next=self->bodyScroll_;
            switch(LOWORD(w)) {case SB_LINEUP:next-=scaleForDpi(24,self->resources_.dpi());break;case SB_LINEDOWN:next+=scaleForDpi(24,self->resources_.dpi());break;
            case SB_PAGEUP:next-=info.nPage;break;case SB_PAGEDOWN:next+=info.nPage;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:next=info.nTrackPos;break;}
            self->scrollBody(next);return 0;
        }
        const bool horizontal = m == WM_HSCROLL; SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(h, horizontal ? SB_HORZ : SB_VERT, &info);
        int value = horizontal ? self->x_ : self->y_;
        switch (LOWORD(w)) {
        case SB_LINEUP: value -= self->rowHeight_; break;
        case SB_LINEDOWN: value += self->rowHeight_; break;
        case SB_PAGEUP: value -= info.nPage; break;
        case SB_PAGEDOWN: value += info.nPage; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: value = info.nTrackPos; break;
        case SB_TOP: value = 0; break; case SB_BOTTOM: value = info.nMax; break;
        }
        self->scroll(horizontal, value); return 0;
    }
    case WM_MOUSEWHEEL:
        if(h==self->body_ || h==self->window_) self->scrollBody(self->bodyScroll_-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*scaleForDpi(48,self->resources_.dpi()));
        else self->scroll(false, self->y_ - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * self->rowHeight_ * 3); return 0;
    case WM_DPICHANGED: if (h == self->window_) { self->fonts(); applyDpiRect(h,l); self->layout(); } return 0;
    case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE: if (h == self->window_) { self->fonts(); self->layout(); } break;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (w && h == self->window_) self->endSession(); return 0;
    case WM_CLOSE: if (h == self->window_) self->close(); return 0;
    case WM_DESTROY:
        if (h == self->window_) {
            KillTimer(h,timer); self->changing_ = true; self->closeChildren();
            for (auto& row : self->rows_) { row.edit.clear(); if (self->accessibility_) for (auto field : row.controls) { const MSAAPROPID props[]{PROPID_ACC_NAME,PROPID_ACC_DESCRIPTION}; self->accessibility_->ClearHwndProps(field, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, props, 2); } }
            if (self->accessibility_) for (const auto& button : self->pageButtons_) {
                const MSAAPROPID props[]{PROPID_ACC_NAME};
                self->accessibility_->ClearHwndProps(button.window, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, props, 1);
            }
            self->window_ = nullptr; self->changing_ = false;
        }
        return 0;
    case WM_NCDESTROY:
        if (!(GetWindowLongPtrW(h,GWL_STYLE) & WS_CHILD)) {
            self->body_ = self->table_ = self->pager_ = self->viewport_ = self->content_ = self->heading_ = self->description_ = self->local_ = self->localLabel_ = self->notice_ = self->error_ = self->cancel_ = self->ok_ = self->check_ = self->repeat_ = nullptr;
            self->resources_.clear();
        }
        SetWindowLongPtrW(h,GWLP_USERDATA,0); break;
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: {
        const auto field = reinterpret_cast<HWND>(l); bool invalid = false, changedToggle = false;
        if (self->controller_.model()) for (const auto& row : self->rows_)
            if (row.controls[0] == field) changedToggle = self->controller_.model()->row(row.remote)->dirty();
        if (self->controller_.model()) for (const auto& row : self->rows_) if (row.controls[1] == field) invalid = self->controller_.model()->row(row.remote)->keyErrorVisible();
        const auto color = invalid ? ui::Color::errorBackground : changedToggle ? ui::Color::contractsChanged : h == self->window_ || h == self->body_ || h == self->pager_ ? ui::Color::background : ui::Color::surface;
        const auto dc = reinterpret_cast<HDC>(w); SetTextColor(dc,self->resources_.color(invalid || (field == self->error_ && self->validationNotice_) ? ui::Color::error : ui::Color::text)); SetBkColor(dc,self->resources_.color(color));
        return reinterpret_cast<LRESULT>(self->resources_.brush(color));
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: self->paint(h); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
} // namespace phpvpn::tray

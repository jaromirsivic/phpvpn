#include "window_utils.h"
#include <initguid.h>
#include "contracts_window.h"
#include "ui_controls.h"
#include "contracts_paint.h"
#include <windowsx.h>

#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
namespace phpvpn::tray {
namespace {
constexpr wchar_t rootClass[] = L"PhpVpnContracts", paneClass[] = L"PhpVpnContractsPane";
constexpr UINT resultMessage = WM_APP + 80;
constexpr UINT_PTR timer = 80;
constexpr int firstControl = 1000, checkId = 800, repeatId = 801;
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
    comInitialised_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&accessibility_)))) { endSession(); ending_ = false; return false; }
    for (const auto* name : {rootClass, paneClass}) {
        WNDCLASSEXW type{sizeof(type)}; type.hInstance = instance; type.lpfnWndProc = procedure;
        type.lpszClassName = name; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { endSession(); ending_ = false; return false; }
    }
    window_ = createWindow_(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, rootClass, L"Communication contracts",
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
    description_ = control(body_, L"STATIC", L"Manage secure communication between this computer and other virtual IP addresses.", SS_NOPREFIX);
    localLabel_ = control(body_, L"STATIC", L"Your virtual IP", SS_NOPREFIX);
    local_ = control(body_, L"STATIC", widen(controller_.model()->localIp().toString()).c_str(), SS_NOPREFIX);
    notice_ = control(body_, L"STATIC", L"", SS_NOPREFIX);
    header_ = createWindow_(0, paneClass, L"Contracts table header", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, body_, nullptr, instance, this);
    viewport_ = createWindow_(WS_EX_CONTROLPARENT, paneClass, L"Communication contracts table",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL, 0, 0, 1, 1, body_, nullptr, instance, this);
    // One native content parent moves inside the clipped viewport. All row
    // HWNDs retain their identity, native roles and complete accessibility tree.
    content_ = createWindow_(WS_EX_CONTROLPARENT, paneClass, L"Contracts rows", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 1, 1, viewport_, nullptr, instance, this);
    error_ = control(window_, L"STATIC", L"", SS_NOPREFIX | SS_NOTIFY, 802);
    check_ = control(window_, L"BUTTON", L"Check current configuration", WS_TABSTOP, checkId);
    repeat_ = control(window_, L"BUTTON", L"Save draft again", WS_TABSTOP, repeatId);
    ok_ = control(window_, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
    bool complete = heading_ && description_ && local_ && localLabel_ && notice_ && header_ && viewport_ && content_ && error_ && check_ && repeat_ && ok_;
    rows_.reserve(controller_.model()->rows().size());
    changing_ = true;
    for (const auto& source : controller_.model()->rows()) {
        rows_.emplace_back(); auto& row = rows_.back(); row.remote = source.remoteIp();
        row.identity = widen(controller_.model()->target(row.remote)->contractId());
        const auto base = firstControl + static_cast<int>(rows_.size() - 1) * 4;
        row.controls[0] = control(content_, L"BUTTON", L"", WS_TABSTOP | BS_AUTOCHECKBOX, base);
        row.controls[1] = control(content_, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, base + 1);
        row.controls[2] = control(content_, L"BUTTON", L"Random", WS_TABSTOP, base + 2);
        row.controls[3] = control(content_, L"BUTTON", L"Password SHA", WS_TABSTOP, base + 3);
        const wchar_t* names[]{L"Enabled", L"Key", L"Random", L"Password SHA"};
        for (std::size_t c = 0; c < 4; ++c) {
            complete = complete && row.controls[c];
            if (!row.controls[c]) continue;
            complete = SetWindowSubclass(row.controls[c], fieldProcedure, 2, reinterpret_cast<DWORD_PTR>(this)) && complete;
            const auto name = row.identity + L" " + names[c];
            complete = SUCCEEDED(accessibility_->SetHwndPropStr(row.controls[c], static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME, name.c_str())) && complete;
        }
        complete = row.edit.attach(row.controls[1], kMaxContractKeyTextBytes,
            [this, remote = row.remote](std::wstring_view text) { return acceptKey(remote, text); }) && complete;
        syncRow(row);
    }
    changing_ = false;
    if (!complete) { endSession(); ending_ = false; return false; }
    fonts(); sizeAndCenter(window_, ui::contractsClient); layout(); refresh();
    SetTimer(window_, timer, 50, nullptr);
    activate(); if (!rows_.empty()) SetFocus(rows_.front().controls[0]);
    return true;
}
void ContractsWindow::activate() const { if (window_) reactivateWindow(window_); }
void ContractsWindow::syncRow(Row& row) {
    const auto* source = controller_.model()->row(row.remote); if (!source) return;
    changing_ = true; auto key = widen(source->keyText()); row.edit.set(key);
    secureWipe(key.data(), key.size() * sizeof(wchar_t));
    SendMessageW(row.controls[0], BM_SETCHECK, source->enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    changing_ = false; InvalidateRect(row.controls[1], nullptr, TRUE);
}
bool ContractsWindow::acceptKey(Ipv4Address remote, std::wstring_view text) {
    if (changing_) return true;
    if (!controller_.model() || controller_.model()->frozen()) return false;
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
    if (id == 802) {
        if (auto focus = controller_.model()->firstBlockingFocus()) for (auto& row : rows_) if (row.remote == focus->remoteIp) SetFocus(row.controls[1]);
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
        const auto snapshot = lifecycleSnapshot();
        const auto yes = MessageBoxW(window_, widen(target->randomConfirmation()).c_str(), L"Communication contracts", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES;
        if (!window_ || ending_ || lifecycleSnapshot() != snapshot) return;
        if (yes) {
            auto result = controller_.model()->confirmRandom(*crypto_, *target, true);
            if (!result) localError_ = L"A random key could not be generated. The current key has been kept.";
            else { syncRow(row); controller_.changed(); localError_.clear(); }
            refresh();
        }
    } else if (column == 3) {
        std::erase_if(children_, [](const auto& child) { return !child->window(); });
        auto child = std::make_unique<PasswordShaWindow>(*crypto_, *target, [this](const ContractTarget& captured, std::string_view key) {
            if (ending_ || !controller_.model()) return false;
            auto applied = controller_.model()->applyPasswordKey(captured, key); if (!applied) return false;
            controller_.changed();
            for (auto& targetRow : rows_) if (targetRow.remote == captured.remoteIp()) syncRow(targetRow);
            localError_.clear(); refresh(); return true;
        }, [this](const ContractTarget& target) {
            if (ending_ || !window_ || !controller_.model() || controller_.model()->frozen()) return;
            for (const auto& targetRow : rows_) if (targetRow.remote == target.remoteIp()) { SetFocus(targetRow.controls[3]); break; }
        });
        if (child->open(instance_, window_)) children_.push_back(std::move(child));
        else { localError_ = L"Password SHA could not be opened."; refresh(); }
    }
}
void ContractsWindow::save(bool repeat) {
    if (!window_ || !controller_.model() || lifecycleSnapshot().saving) return;
    if (repeat) {
        if (!controller_.canRepeat()) return;
        const auto before = lifecycleSnapshot();
        if (MessageBoxW(window_, L"Replace the current whole configuration with your draft?", L"Save draft again", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES || !window_ || lifecycleSnapshot() != before) return;
    }
    auto prepared = controller_.prepare(repeat);
    if (!prepared) { refresh(); return; }
    closeChildren(); controller_.dispatch(window_, resultMessage); refresh();
}
void ContractsWindow::poll() {
    const auto event = controller_.poll();
    if (event == ContractsEvent::saved) { endSession(); ending_ = false; return; }
    if (event == ContractsEvent::matched) {
        const auto before = lifecycleSnapshot();
        MessageBoxW(window_, L"The current configuration matches your changes.", L"Communication contracts", MB_OK | MB_ICONINFORMATION);
        if (window_ && lifecycleSnapshot() == before) { endSession(); ending_ = false; }
        return;
    }
    if (event != ContractsEvent::none) refresh();
}
void ContractsWindow::refresh() {
    if (!window_ || !ok_ || !controller_.model()) return;
    const auto snapshot = lifecycleSnapshot();
    EnableWindow(viewport_, !snapshot.saving && !snapshot.unknown);
    EnableWindow(ok_, controller_.model()->canPrepareSave());
    ShowWindow(ok_, snapshot.unknown ? SW_HIDE : SW_SHOW);
    ShowWindow(check_, snapshot.unknown ? SW_SHOW : SW_HIDE);
    ShowWindow(repeat_, controller_.canRepeat() ? SW_SHOW : SW_HIDE);
    EnableWindow(check_, !snapshot.checking);
    std::wstring message = controller_.notice();
    if (!localError_.empty()) message = localError_;
    else if (auto focus = controller_.model()->firstBlockingFocus()) message = widen(kContractKeyError) + L"\nFirst invalid peer: " + widen(focus->remoteIp.toString());
    const int length = GetWindowTextLengthW(error_);
    std::wstring previous(static_cast<std::size_t>(length)+1,L'\0'); GetWindowTextW(error_,previous.data(),length+1); previous.resize(length);
    if (message != previous) SetWindowTextW(error_, message.c_str());
    ShowWindow(error_,message.empty()?SW_HIDE:SW_SHOW);
    SetWindowTextW(notice_, connectionActive_ ? L"Changes take effect on the next connection." : L"");
    ShowWindow(notice_, connectionActive_ ? SW_SHOW : SW_HIDE);
    for (auto& row : rows_) {
        const bool valid = controller_.model()->row(row.remote)->keyValid();
        if (valid != row.lastValid) {
            row.lastValid = valid; InvalidateRect(row.controls[1], nullptr, FALSE);
            if (accessibility_) accessibility_->SetHwndPropStr(row.controls[1],static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,
                PROPID_ACC_DESCRIPTION, valid ? L"" : L"Invalid: Key must contain exactly 64 lowercase hexadecimal characters.");
            NotifyWinEvent(EVENT_OBJECT_DESCRIPTIONCHANGE,row.controls[1],OBJID_CLIENT,CHILDID_SELF);
        }
    }
    if (message != previous) { layout(); reveal(GetFocus()); }
}
void ContractsWindow::setConnectionActive(bool active) { if (connectionActive_ != active) { connectionActive_ = active; refresh(); layout(); } }
void ContractsWindow::closeChildren() { for (auto& child : children_) child->close(); children_.clear(); }
void ContractsWindow::close() {
    if (!window_) return;
    const auto before = lifecycleSnapshot();
    if (before.saving) return;
    if (before.unknown || before.dirty) {
        const auto* message = before.unknown ? L"The file may already have changed. Close without checking the save result?" : L"Discard unsaved changes?";
        if (MessageBoxW(window_, message, L"Communication contracts", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
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
    controller_.close(); rows_.clear(); crypto_.reset();
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
    for (auto h : {description_, localLabel_, notice_, error_, ok_, check_, repeat_}) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::label)), TRUE);
    SendMessageW(local_,WM_SETFONT,reinterpret_cast<WPARAM>(resources_.font(ui::Font::strongValue)),TRUE);
    SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::heading)), TRUE);
    for (auto& row : rows_) for (std::size_t c = 0; c < 4; ++c) SendMessageW(row.controls[c], WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(c == 1 ? ui::Font::mono : ui::Font::label)), TRUE);
}
void ContractsWindow::layout() {
    if (!ok_ || layingOut_ || IsIconic(window_)) return;
    layingOut_ = true;
    RECT r{}; GetClientRect(window_, &r); const auto s = [this](int v) { return scaleForDpi(v, resources_.dpi()); };
    const int margin = s(r.right < s(700) ? 16 : 32), right = (std::max)(margin, static_cast<int>(r.right) - margin);
    const bool compact = r.right < s(900);
    const int textRight = compact ? right : right-s(180);
    const auto dc=GetDC(window_);
    const int headingBottom=s(24)+measureText(dc,resources_.font(ui::Font::heading),L"Communication contracts",(std::max)(1,textRight-margin),true).height;
    const int descriptionTop=(std::max)(s(64),headingBottom+s(4));
    const int descriptionBottom=descriptionTop+measureText(dc,resources_.font(ui::Font::label),L"Manage secure communication between this computer and other virtual IP addresses.",(std::max)(1,textRight-margin),true).height;
    const int localTop=compact?descriptionBottom+s(12):s(28);
    const int tableTop=(std::max)(s(compact?140:104),compact?localTop+s(32):descriptionBottom+s(12))+(connectionActive_?s(28):0);
    const int errorLength=GetWindowTextLengthW(error_); std::wstring errorText(static_cast<std::size_t>(errorLength)+1,L'\0');
    GetWindowTextW(error_,errorText.data(),errorLength+1); errorText.resize(errorLength);
    const int errorHeight=errorText.empty()?0:measureText(dc,resources_.font(ui::Font::label),errorText,(std::max)(1,right-margin),true).height+s(12); ReleaseDC(window_,dc);
    const bool stackActions=IsWindowVisible(check_) && right-margin<s(528);
    const int footer = (std::max)(0,static_cast<int>(r.bottom)-s(stackActions?148:60)-errorHeight);
    const int buttonTop=static_cast<int>(r.bottom)-s(44);
    SetWindowPos(body_,nullptr,0,0,r.right,footer,SWP_NOZORDER|SWP_NOACTIVATE);
    const int bodyExtent=(std::max)(footer,tableTop+s(112));
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
        {header_, {margin,tableTop,bodyRight,tableTop+s(40)}}, {viewport_, {margin,tableTop+s(40),bodyRight,bodyExtent}}};
    for(auto& placement:bodyPositions) { placement.rect.top-=bodyScroll_; placement.rect.bottom-=bodyScroll_; placement.rect.right=(std::min)(placement.rect.right,bodyRight); }
    positionControls(body_,bodyPositions);
    positionControls(window_, std::vector<ControlPlacement>{
        {error_, {margin,footer+s(6),right,footer+(std::max)(s(6),errorHeight)}},
        {check_, {margin,buttonTop-s(stackActions?88:0),margin+s(236),buttonTop-s(stackActions?88:0)+s(36)}},
        {repeat_, {margin+s(stackActions?0:244),buttonTop-s(stackActions?44:0),margin+s(stackActions?164:408),buttonTop-s(stackActions?44:0)+s(36)}},
        {ok_, {right-s(104),buttonTop,right,buttonTop+s(36)}}});
    RECT v{}; GetClientRect(viewport_, &v);
    const auto oldWidth=width_,oldRowHeight=rowHeight_;
    keyWidth_ = (std::max)(s(260), static_cast<int>(v.right) - s(550)); width_ = s(550) + keyWidth_; rowHeight_ = s(48);
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
    SetWindowPos(content_, nullptr, -x_, -y_, width_, static_cast<int>(rows_.size())*rowHeight_, SWP_NOZORDER | SWP_NOACTIVATE);
    if (oldWidth!=width_ || oldRowHeight!=rowHeight_) rowPositions();
    InvalidateRect(header_, nullptr, FALSE); InvalidateRect(viewport_, nullptr, FALSE);
    layingOut_ = false;
}
void ContractsWindow::rowPositions() {
    const auto s = [this](int v) { return scaleForDpi(v, resources_.dpi()); };
    std::vector<ControlPlacement> positions; positions.reserve(rows_.size() * 4);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const int top = static_cast<int>(i) * rowHeight_ + s(6), action = s(330) + keyWidth_;
        const std::array<Rect,4> rects{{{s(262),top,s(318),top+s(36)}, {s(342),top,action-s(12),top+s(36)},
            {action+s(8),top,action+s(96),top+s(36)}, {action+s(104),top,action+s(216),top+s(36)}}};
        for (std::size_t c = 0; c < 4; ++c) positions.push_back({rows_[i].controls[c], rects[c]});
    }
    // Move the complete native table as one batch without repainting each HWND
    // during the move. Paint only after every row has its final coordinates.
    auto batch = BeginDeferWindowPos(static_cast<int>(positions.size()));
    for (const auto& p : positions) if (batch) batch = DeferWindowPos(batch, p.window, nullptr,
        p.rect.left, p.rect.top, p.rect.right-p.rect.left, p.rect.bottom-p.rect.top,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    if (!batch || !EndDeferWindowPos(batch)) positionControls(content_, positions);
    RedrawWindow(content_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
void ContractsWindow::scroll(bool horizontal, int position) {
    SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(viewport_, horizontal ? SB_HORZ : SB_VERT, &info);
    position = std::clamp(position, 0, (std::max)(0, info.nMax + 1 - static_cast<int>(info.nPage)));
    auto& value = horizontal ? x_ : y_; if (value == position) return;
    value = position; SetScrollPos(viewport_, horizontal ? SB_HORZ : SB_VERT, position, TRUE);
    SetWindowPos(content_, nullptr, -x_, -y_, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(viewport_, nullptr, FALSE); if (horizontal) InvalidateRect(header_, nullptr, FALSE);
}
void ContractsWindow::scrollBody(int position) {
    SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(body_,SB_VERT,&info);
    position=std::clamp(position,0,(std::max)(0,info.nMax+1-static_cast<int>(info.nPage)));
    if(position!=bodyScroll_) { bodyScroll_=position;layout();InvalidateRect(body_,nullptr,TRUE); }
}
void ContractsWindow::reveal(HWND field) {
    for (std::size_t i = 0; i < rows_.size(); ++i) for (std::size_t c = 0; c < 4; ++c) if (rows_[i].controls[c] == field) {
        if (!controller_.model()) return;
        (void)controller_.model()->setFocus(ContractFocus{rows_[i].remote, static_cast<ContractColumn>(c)});
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
    if (m == WM_KEYDOWN && (GetDlgCtrlID(h) - firstControl) % 4 != 1) {
        if (w == VK_END && GetKeyState(VK_CONTROL) < 0 && !self->rows_.empty()) { SetFocus(self->rows_.back().controls[0]); return 0; }
        if (w == VK_NEXT || w == VK_PRIOR) { RECT r{}; GetClientRect(self->viewport_, &r); self->scroll(false, self->y_ + (w == VK_NEXT ? r.bottom : -r.bottom)); return 0; }
    }
    if (m == WM_MOUSEWHEEL) { SendMessageW(self->viewport_, m, w, l); return 0; }
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, fieldProcedure, 2);
    return DefSubclassProc(h, m, w, l);
}
void ContractsWindow::paint(HWND target) {
    PAINTSTRUCT p{}; auto dc = BeginPaint(target, &p); RECT r{}; GetClientRect(target, &r);
    FillRect(dc, &r, resources_.brush(target == window_ || target == body_ ? ui::Color::background : ui::Color::surface));
    if (target == header_ || target == content_) {
        auto font = SelectObject(dc, resources_.font(ui::Font::label)); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, resources_.color(ui::Color::text));
        const auto s = [this](int v) { return scaleForDpi(v, resources_.dpi()); };
        if (target == header_) {
            const wchar_t* labels[]{L"Contract",L"Enabled",L"Key",L"Actions"};
            const int lefts[]{0,s(250),s(330),s(330)+keyWidth_};
            for (int c = 0; c < 4; ++c) drawText(dc, labels[c], {lefts[c]+s(12)-x_,0,(c == 3 ? width_ : lefts[c+1])-x_,r.bottom}, DT_SINGLELINE | DT_VCENTER);
        } else if (rowHeight_) {
            RECT clip{}; GetClipBox(dc,&clip);
            const auto first = static_cast<std::size_t>((std::max)(0L, clip.top) / rowHeight_);
            const auto last = (std::min)(rows_.size(), static_cast<std::size_t>(clip.bottom / rowHeight_ + 1));
            for (auto i = first; i < last; ++i) {
                const int top = static_cast<int>(i)*rowHeight_;
                drawText(dc, rows_[i].identity, {s(12),top,s(250),top+rowHeight_}, DT_SINGLELINE | DT_VCENTER);
                RECT line{0,top+rowHeight_-1,r.right,top+rowHeight_}; FillRect(dc, &line, resources_.brush(ui::Color::separator));
            }
        }
        SelectObject(dc, font);
    }
    EndPaint(target, &p);
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
    case WM_NOTIFY: {
        const auto* draw=reinterpret_cast<NMCUSTOMDRAW*>(l);
        if(draw && draw->hdr.hwndFrom==self->ok_ && draw->hdr.code==NM_CUSTOMDRAW) return paintContractsPrimary(*draw,self->resources_);
        break;
    }
    case WM_COMMAND: self->command(LOWORD(w), HIWORD(w)); return 0;
    case WM_TIMER: if (h == self->window_ && w == timer) self->poll(); return 0;
    case resultMessage: self->poll(); return 0;
    case WM_SIZE: if (h == self->window_) { self->layout(); self->reveal(GetFocus()); } return 0;
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
            self->window_ = nullptr; self->changing_ = false;
        }
        return 0;
    case WM_NCDESTROY:
        if (!(GetWindowLongPtrW(h,GWL_STYLE) & WS_CHILD)) {
            self->body_ = self->viewport_ = self->content_ = self->header_ = self->heading_ = self->description_ = self->local_ = self->localLabel_ = self->notice_ = self->error_ = self->ok_ = self->check_ = self->repeat_ = nullptr;
            self->resources_.clear();
        }
        SetWindowLongPtrW(h,GWLP_USERDATA,0); break;
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: {
        const auto field = reinterpret_cast<HWND>(l); bool invalid = false;
        if (self->controller_.model()) for (const auto& row : self->rows_) if (row.controls[1] == field) invalid = !self->controller_.model()->row(row.remote)->keyValid();
        const auto color = invalid ? ui::Color::errorBackground : h == self->window_ || h == self->body_ ? ui::Color::background : ui::Color::surface;
        const auto dc = reinterpret_cast<HDC>(w); SetTextColor(dc,self->resources_.color(invalid ? ui::Color::error : ui::Color::text)); SetBkColor(dc,self->resources_.color(color));
        return reinterpret_cast<LRESULT>(self->resources_.brush(color));
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: self->paint(h); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
} // namespace phpvpn::tray

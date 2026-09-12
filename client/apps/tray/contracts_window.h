#pragma once
#include "contracts_save_controller.h"
#include "password_sha_window.h"
#include <oleacc.h>

namespace phpvpn::tray {
class ContractsWindow {
public:
    // Empty endpoint is permitted only for the legacy un-opened harness member.
    using CryptoFactory = std::function<Result<std::unique_ptr<CryptoProvider>>() >;
    explicit ContractsWindow(std::wstring pipeName = {}, CryptoFactory cryptoFactory = createPlatformCryptoProvider,
        decltype(&CreateWindowExW) createWindow = CreateWindowExW)
        : controller_(std::move(pipeName)), cryptoFactory_(std::move(cryptoFactory)), createWindow_(createWindow) {}
    ContractsWindow(const ContractsWindow&) = delete;
    ContractsWindow& operator=(const ContractsWindow&) = delete;
    ~ContractsWindow();
    bool open(HINSTANCE instance, HWND owner, std::string_view document, std::string path, std::string revision);
    bool isOpen() const { return window_ != nullptr; }
    HWND window() const { return window_; }
    bool translate(MSG& message) const;
    void activate() const;
    void close();
    void endSession();
    bool approvedDiscard(const ContractsLifecycleSnapshot& approved);
    ContractsLifecycleSnapshot lifecycleSnapshot() const { return controller_.snapshot(); }
    void setConnectionActive(bool active);
private:
    struct Row {
        Ipv4Address remote;
        std::wstring identity;
        std::array<HWND, 3> controls{};
        ContractsEdit edit;
        bool lastValid = true;
        bool lastDirty = false;
    };
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK fieldProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void command(int id, int code);
    void layout();
    void rowPositions();
    void fonts();
    void refresh();
    void poll();
    void save(bool repeat);
    void closeChildren();
    void reveal(HWND field);
    void scroll(bool horizontal, int position);
    void scrollBody(int position);
    void paint(HWND target);
    void syncRow(Row& row);
    bool createRows();
    void destroyRows();
    bool createPager();
    void changePage(int page);
    void focusContract(Ipv4Address remote, int column);
    int pageCount() const;
    bool acceptKey(Ipv4Address remote, std::wstring_view text);
    ContractsSaveController controller_;
    CryptoFactory cryptoFactory_;
    decltype(&CreateWindowExW) createWindow_;
    std::unique_ptr<CryptoProvider> crypto_;
    std::vector<Row> rows_;
    struct PageButton { HWND window; int page; };
    std::vector<PageButton> pageButtons_;
    int page_ = 0;
    std::vector<std::unique_ptr<PasswordShaWindow>> children_;
    UiResources resources_;
    IAccPropServices* accessibility_ = nullptr;
    bool comInitialised_ = false;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr, body_ = nullptr, table_ = nullptr, pager_ = nullptr, viewport_ = nullptr, content_ = nullptr, heading_ = nullptr, description_ = nullptr,
        local_ = nullptr, localLabel_ = nullptr, notice_ = nullptr, error_ = nullptr, cancel_ = nullptr, ok_ = nullptr, check_ = nullptr, repeat_ = nullptr;
    bool ending_ = false, changing_ = false, layingOut_ = false, connectionActive_ = false;
    int x_ = 0, y_ = 0, width_ = 0, keyWidth_ = 260, rowHeight_ = 48;
    int bodyScroll_ = 0, footerTop_ = 0;
    std::wstring localError_;
    std::wstring saveText_ = L"Save";
    bool validationNotice_ = false;
    bool notifyAfterSave_ = false;
};
} // namespace phpvpn::tray

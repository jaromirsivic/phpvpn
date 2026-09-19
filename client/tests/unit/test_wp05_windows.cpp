#include "harness.h"
#include "contracts_fixture.h"
#include "core/wizard/wizard_defaults.h"
#include "wp03_dialog_test.h"
#include "icon_test_helpers.h"
#include "../../apps/tray/tray_application.h"
#include "../../apps/tray/resource.h"
#include "../../apps/tray/http_error_detail_window.h"
#include "../../apps/tray/ui_controls.h"
#include <commctrl.h>
#include <ole2.h>
#include "platform/windows/ipc_pipe.h"
#include <atomic>
#include <condition_variable>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#include <source_location>
#pragma comment(lib, "psapi.lib")

namespace {
using namespace phpvpn;
using namespace phpvpn::tray;
namespace dialogs = phpvpn::tray::test;
void require(bool value, const std::source_location location = std::source_location::current()) {
    if (!value) throw std::runtime_error("WP05 prerequisite failed at line " + std::to_string(location.line()));
}
void pump(ContractsWindow* contracts = nullptr) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        if (message.message != WM_QUIT && (!contracts || !contracts->translate(message))) { TranslateMessage(&message); DispatchMessageW(&message); }
    Sleep(1);
}
template<class P> bool until(P predicate, DWORD timeout = 2500) {
    const auto end = GetTickCount64() + timeout;
    do { if (predicate()) return true; pump(); } while (GetTickCount64() < end);
    return predicate();
}
HWND top(const wchar_t* type) {
    struct Search { const wchar_t* type; HWND found = nullptr; } search{type};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND h, LPARAM p) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(p); wchar_t name[128]{}; GetClassNameW(h,name,128);
        if (std::wstring_view(name) == s.type) { s.found = h; return FALSE; } return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (search.found) PHPVPN_CHECK(phpvpn::tray::test::applicationIconsMatch(search.found));
    return search.found;
}
HWND field(HWND parent, int id) {
    struct Search { int id; HWND found = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND h, LPARAM p) -> BOOL { auto& s = *reinterpret_cast<Search*>(p);
        if (GetDlgCtrlID(h) == s.id) { s.found = h; return FALSE; } return TRUE;
    }, reinterpret_cast<LPARAM>(&search)); return search.found;
}
std::wstring text(HWND h) {
    const int n = GetWindowTextLengthW(h); std::wstring value(n+1,L'\0'); GetWindowTextW(h,value.data(),n+1); value.resize(n); return value;
}
void click(HWND parent, int id) { const auto h = field(parent,id); require(h != nullptr); SendMessageW(h,BM_CLICK,0,0); }
void replace(HWND h, const std::wstring& value) { SendMessageW(h,EM_SETSEL,0,-1); SendMessageW(h,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str())); }
void capture(HWND h,const wchar_t* name) {
    const auto directory=std::filesystem::path(L"work/logs/wp05/captures"); std::filesystem::create_directories(directory);
    DWORD pid=0; GetWindowThreadProcessId(h,&pid); require(pid==GetCurrentProcessId());
    RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    RECT r{};GetClientRect(h,&r);auto source=GetDC(h),dc=CreateCompatibleDC(source);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;auto bitmap=CreateDIBSection(source,&info,DIB_RGB_COLORS,&pixels,nullptr,0);require(bitmap && dc);
    auto old=SelectObject(dc,bitmap);const bool printed=PrintWindow(h,dc,PW_CLIENTONLY)!=FALSE;
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+r.right*r.bottom*4;
    std::ofstream out(directory/name,std::ios::binary);out.write(reinterpret_cast<const char*>(&header),sizeof(header));out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));out.write(static_cast<const char*>(pixels),r.right*r.bottom*4);const bool written=out.good();
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(h,source);require(printed&&written);
}
void resizeClient(HWND h,int width,int height) {
    width=MulDiv(width,GetDpiForWindow(h),96); height=MulDiv(height,GetDpiForWindow(h),96);
    RECT r{0,0,width,height};AdjustWindowRectExForDpi(&r,static_cast<DWORD>(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(h,GWL_EXSTYLE)),GetDpiForWindow(h));
    SetWindowPos(h,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
}
thread_local unsigned createCalls=0,failCreateAt=0;
HWND WINAPI createWithFailure(DWORD ex,LPCWSTR kind,LPCWSTR value,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE instance,LPVOID data) {
    if(++createCalls==failCreateAt){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
    return CreateWindowExW(ex,kind,value,style,x,y,width,height,parent,menu,instance,data);
}
class Shell final : public ShellAdapter { bool notify(DWORD, NOTIFYICONDATAW&) override { return true; } };
struct Backend {
    std::wstring suffix = widen(createPlatformCryptoProvider().value()->randomUuidV4().value());
    std::wstring pipe = L"\\\\.\\pipe\\phpvpn-wp05-" + suffix;
    std::atomic<unsigned> saves{0}, documents{0}, states{0}, statuses{0}, connects{0}, disconnects{0}, unexpected{0}, held{0};
    std::atomic<bool> holdSave{false}, holdStatus{false}, invalidReply{false}, publish{true}, documentError{false};
    std::atomic<bool> connected{false}, invalidStatus{false}, rejectSave{false};
    std::atomic<bool> changeAttempt{false}, changeStarted{false}, delayIdle{false}, idleReady{false};
    std::atomic<bool> disconnectRemainsRunning{false};
    std::atomic<int> disconnectFault{0}, connectFault{0}, offlineState{0};
    std::atomic<std::uint64_t> connectionEpoch{1};
    const std::string connectionAttempt = createPlatformCryptoProvider().value()->randomUuidV4().value();
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    std::string stored = contracts_test::document(), availability = "valid";
    ErrorCode readError=ErrorCode::none;
    std::vector<std::string> saveIds;
    std::vector<IpcCommand> connectionCommands;
    WizardView view;
    IpcPipeServer server;
    Backend() : server([this](std::string_view bytes) { return reply(bytes); }, options()) {
        auto initial = makeNewWizardDocument(); view.fields = initial.fields; view.context = initial.context;
        view.mode = WizardMode::create; view.availability = ConfigAvailability::missing;
        view.sessionId = createPlatformCryptoProvider().value()->randomUuidV4().value();
        view.serviceInstanceId = createPlatformCryptoProvider().value()->randomUuidV4().value();
        require(server.start().ok());
    }
    IpcPipeServerOptions options() {
        IpcPipeServerOptions value; value.pipeName = pipe; value.cancelHandlers = [this] { release(); };
        value.classify = [](std::string_view bytes) { if (auto request = IpcProtocol::parseRequest(bytes); request && request->command == IpcCommand::set_config) return IpcWorkClass::file; return IpcWorkClass::shortControl; };
        return value;
    }
    void release() { { std::lock_guard lock(mutex); released = true; } changed.notify_all(); }
    ~Backend() { release(); server.stop(); }
    std::string reply(std::string_view bytes) {
        if (const auto request = IpcProtocol::parseRequest(bytes)) {
            if (request->command == IpcCommand::set_config) {
                ++saves; std::unique_lock lock(mutex); saveIds.push_back(request->requestId);
                if (rejectSave) return IpcProtocol::buildError(request->requestId,Error(ErrorCode::config_invalid_value));
                if (publish) stored = request->config;
                if (holdSave) { ++held; changed.wait(lock,[&] { return released; }); }
                return invalidReply ? "{}" : IpcProtocol::buildAcknowledgement(request->requestId);
            }
            if (request->command == IpcCommand::get_config) { std::lock_guard lock(mutex); return stored; }
            if (request->command == IpcCommand::connect || request->command == IpcCommand::disconnect) {
                std::lock_guard lock(mutex); connectionCommands.push_back(request->command);
            }
            if (request->command == IpcCommand::connect) {
                ++connects;
                if (!request->config.empty()) ++unexpected; // ordinary Connect has no captured configuration payload
                if (connectFault == 1) return IpcProtocol::buildError(request->requestId, Error(ErrorCode::config_invalid_value));
                if (connectFault == 2) return "{}";
                if (connectFault == 3) return IpcProtocol::buildError(request->requestId, Error(ErrorCode::busy));
            }
            else if (request->command == IpcCommand::disconnect) {
                ++disconnects;
                if (disconnectFault == 7) return IpcProtocol::buildError(request->requestId, Error(ErrorCode::ipc_unavailable));
                if (!delayIdle) connected=false;
            }
            else if (request->command == IpcCommand::get_status) {
                ++statuses;
                if (holdStatus) { ++held; std::unique_lock lock(mutex); changed.wait(lock,[&] { return released; }); }
            } else ++unexpected;
            StatusSnapshot status; status.stateChangedAtUtc = "2026-09-08T00:00:00.000Z"; status.clientVersion = "wp05-fixture";
            if (invalidStatus && request->command == IpcCommand::get_status) return "{}";
            if (disconnects && idleReady) connected=false;
            status.connectionEpoch=connectionEpoch.load();
            status.connectAttemptId=changeAttempt ? "cfbd715b-3bb7-42b3-9679-8109150ce76f" : connectionAttempt;
            status.tunnelReady=Tristate::no;
            if (connected) {
                status.state=ConnectionState::connected; status.phase=ConnectionPhase::running;
                status.connectionStartedAtUtc=changeStarted ? "2026-09-08T00:00:01.000Z" : "2026-09-08T00:00:00.000Z";
                status.tunnelReady=Tristate::yes;
                if (disconnects && delayIdle && !disconnectRemainsRunning) status.phase=ConnectionPhase::stopping;
            } else if (!disconnects) {
                if (offlineState == 1) { status.state=ConnectionState::connecting; status.phase=ConnectionPhase::health; }
                if (offlineState == 2) { status.state=ConnectionState::recovering; status.phase=ConnectionPhase::health; }
                if (offlineState == 3) { status.state=ConnectionState::error; status.phase=ConnectionPhase::idle; }
            }
            if (disconnects && !connects && !connected) {
                switch (disconnectFault.load()) {
                    case 1: status.lastError=SafeError{"ipc_unavailable", "Synthetic disconnect failure", false}; break;
                    case 2: status.tunnelReady=Tristate::yes; break;
                    case 3: ++status.connectionEpoch; break;
                    case 4: status.connectAttemptId="cfbd715b-3bb7-42b3-9679-8109150ce76f"; break;
                    case 5: return "{}";
                    case 6: status.connectionStartedAtUtc="2026-09-08T00:00:00.000Z"; break;
                }
            }
            if (request->command == IpcCommand::connect) {
                status.state = ConnectionState::connecting; status.phase = ConnectionPhase::health;
                status.connectionStartedAtUtc.reset(); status.tunnelReady=Tristate::no;
            }
            return IpcProtocol::buildStatusResponse(request->requestId,status);
        }
        if (const auto request = WizardIpc::parseRequest(bytes)) {
            if (request->command == WizardCommand::get_config_document) {
                ++documents; std::lock_guard lock(mutex);
                if(documentError) return WizardIpc::buildError(request->requestId,WizardIpc::safeError(Error(ErrorCode::config_directory_security_invalid)));
                if(readError!=ErrorCode::none)return WizardIpc::buildError(request->requestId,WizardIpc::safeError(Error(readError),{},{},{},"Z:\\synthetic\\config.json"));
                return WizardIpc::buildResult(request->requestId,JsonValue::makeObject({
                    {"configPath",JsonValue::makeString("Z:\\synthetic\\config.json")}, {"availability",JsonValue::makeString(availability)},
                    {"reasonCode",availability == "valid" ? JsonValue::makeNull() : JsonValue::makeString(availability=="missing"?"config_not_found":"config_invalid_value")},
                    {"revisionToken",availability == "missing" ? JsonValue::makeNull() : JsonValue::makeString("synthetic")},
                    {"document",availability == "missing" ? JsonValue::makeNull() : JsonValue::makeString(stored)}}));
            }
            if (request->command == WizardCommand::get_config_state) ++states;
            if (request->command == WizardCommand::open) return WizardIpc::buildResult(request->requestId,WizardIpc::viewValue(view));
            if (request->command == WizardCommand::close) return WizardIpc::buildResult(request->requestId,JsonValue::makeObject({{"closed",JsonValue::makeBool(true)}}));
            if (request->command == WizardCommand::keepalive) return WizardIpc::buildResult(request->requestId,JsonValue::makeObject({{"alive",JsonValue::makeBool(true)},{"leaseDurationMs",JsonValue::makeInteger(kWizardLeaseMs)}}));
        }
        ++unexpected; return {};
    }
    std::unique_ptr<TrayApplication> app() {
        auto result = std::make_unique<TrayApplication>(TrayRuntime{pipe,L"Local\\wp05-"+suffix,std::make_unique<Shell>(),{}});
        require(result->initialise(GetModuleHandleW(nullptr))); return result;
    }
};
}
PHPVPN_TEST(WP05Windows, ContractsSavedDisconnectConsentAndIdentity) {
    std::cout << std::unitbuf;
    // 0 = real Enter/default, 1 = explicit Yes, 2 = No, 3 = Escape,
    // 4 = connection changed during modal, 5 = disconnected during Save,
    // 6 = unknown status, 7 = rejected Save, 8 = unknown Save,
    // 9 = connected during Save, 10 = end session inside the modal.
    for (bool editor : {false, true}) for (int scenario=0; scenario<=14; ++scenario) {
        std::cout << "[ SAVE CONSENT START ] editor=" << editor << " scenario=" << scenario << '\n';
        Backend backend;
        backend.connected=scenario!=9;
        backend.holdSave=scenario==5 || scenario==9;
        backend.invalidStatus=scenario==6;
        backend.rejectSave=scenario==7;
        backend.invalidReply=scenario==8;
        auto app=backend.app();
        SendMessageW(app->window(),WM_COMMAND,editor ? IDM_EDIT_CONFIG : 207,0);
        const auto open=[&] { return editor ? app->editorLifecycleSnapshot().open : app->contractsLifecycleSnapshot().open; };
        require(until(open));
        const auto root=top(editor ? L"PhpVpnTrayEditor" : L"PhpVpnContracts");
        require(root != nullptr);
        if (editor) replace(field(root,IDC_TEXT_AREA),widen(contracts_test::document()));
        else { click(root,1000); replace(field(root,1001),std::wstring(64,L'a')); }
        dialogs::dialogTexts.clear();
        unsigned observed=0;
        dialogs::dialogObserver=[&](HWND dialog) {
            ++observed;
            if(scenario==4) ++backend.connectionEpoch;
            if(scenario==10) app->endSession();
            if(scenario==11) backend.changeAttempt=true;
            if(scenario==12) backend.changeStarted=true;
            if(scenario==13) DestroyWindow(root);
            if(scenario==14) PostMessageW(dialog,WM_CLOSE,0,0);
            if(scenario==3) PostMessageW(dialog,WM_CLOSE,0,0);
            if(observed>100) { dialogs::dialogAnswer=IDNO; PHPVPN_CHECK(false); }
        };
        struct ClearObserver { ~ClearObserver() { dialogs::dialogObserver={}; } } clear;
        dialogs::answer(scenario==0?0:scenario==2?IDNO:scenario==3 || scenario==14?IDCANCEL:IDYES,[&] {
            if (editor) SendMessageW(root,WM_COMMAND,IDC_SAVE,0);
            else click(root,IDOK);
            if(backend.holdSave) {
                require(until([&] { return backend.saves==1; }));
                backend.connected=scenario==9;
                backend.release();
            }
            if(scenario==7 || scenario==8) {
                require(until([&] { return editor ? !app->editorLifecycleSnapshot().saving : !app->contractsLifecycleSnapshot().saving; }));
            } else {
                const bool closed=until([&] { return !open(); });
                if (!closed) std::cout << "[ NOT CLOSED ] editor=" << editor << " scenario=" << scenario << " saves=" << backend.saves << " status=" << backend.statuses << " disconnects=" << backend.disconnects << " connects=" << backend.connects << " dialogs=" << observed << '\n';
                require(closed);
            }
            if(scenario==0 || scenario==1 || scenario==9) {
                const bool connectedAgain=until([&] { return backend.connects==1; });
                std::cout << "[ RECONNECT ] editor=" << editor << " scenario=" << scenario << " disconnects=" << backend.disconnects << " connects=" << backend.connects << " dialogs=" << observed << '\n';
                require(connectedAgain);
            }
        });
        const bool expectDisconnect=scenario==0 || scenario==1 || scenario==9;
        if(expectDisconnect) require(until([&] { return backend.disconnects==1; }));
        PHPVPN_CHECK_EQ(backend.disconnects.load(),expectDisconnect?1u:0u);
        PHPVPN_CHECK_EQ(backend.connects.load(),expectDisconnect?1u:0u);
        PHPVPN_CHECK_EQ(backend.saves.load(),1u);
        PHPVPN_CHECK_EQ(backend.unexpected.load(),0u);
        PHPVPN_CHECK_EQ(backend.documents.load(),1u); // Status checking never rereads config.
        const bool expectDialog=scenario<=4 || scenario>=9;
        PHPVPN_CHECK_EQ(!dialogs::dialogTexts.empty(),expectDialog || scenario==6 || (editor && (scenario==7 || scenario==8)));
        if(expectDialog) PHPVPN_CHECK(std::any_of(dialogs::dialogTexts.begin(),dialogs::dialogTexts.end(),[](const auto& value) {
            return value.find(kReconnectSavedText)!=std::wstring::npos;
        }));
        if(expectDisconnect) PHPVPN_CHECK(top(L"PhpVpnConnectingProgress")!=nullptr);
        app->endSession(); app.reset(); pump();
        std::cout << "[ CONTRACT CONSENT ] scenario=" << scenario << " disconnects=" << backend.disconnects << " dialogs=" << observed << '\n';
    }
}
namespace {
void saveForReconnect(TrayApplication& app, bool editor) {
    SendMessageW(app.window(),WM_COMMAND,editor ? IDM_EDIT_CONFIG : 207,0);
    require(until([&] { return editor ? app.editorLifecycleSnapshot().open : app.contractsLifecycleSnapshot().open; }));
    const auto root=top(editor ? L"PhpVpnTrayEditor" : L"PhpVpnContracts");
    require(root!=nullptr);
    if (editor) {
        replace(field(root,IDC_TEXT_AREA),widen(contracts_test::document()));
        SendMessageW(root,WM_COMMAND,IDC_SAVE,0);
    } else {
        click(root,1000); replace(field(root,1001),std::wstring(64,L'a')); click(root,IDOK);
    }
}
struct ReconnectMessageProbe {
    HWND window;
    unsigned disconnectDepth=0, queued=0;
    WPARAM lastToken=0;
    std::function<void()> beforeConnect;
    explicit ReconnectMessageProbe(HWND h) : window(h) { require(SetWindowSubclass(h,procedure,909,reinterpret_cast<DWORD_PTR>(this))!=FALSE); }
    ~ReconnectMessageProbe() { if(IsWindow(window)) RemoveWindowSubclass(window,procedure,909); }
    static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto& probe=*reinterpret_cast<ReconnectMessageProbe*>(data);
        if(m==WM_APP+5) {
            ++probe.disconnectDepth;
            const auto result=DefSubclassProc(h,m,w,l);
            --probe.disconnectDepth;
            return result;
        }
        if(m==WM_APP+7) {
            ++probe.queued; probe.lastToken=w;
            PHPVPN_CHECK_EQ(probe.disconnectDepth,0u);
            if(probe.beforeConnect) { const auto callback=std::move(probe.beforeConnect); callback(); }
        }
        return DefSubclassProc(h,m,w,l);
    }
};
}
PHPVPN_TEST(WP05Windows, ReconnectWaitsForIdleAndRejectsUnsafeCompletion) {
    for (bool editor : {false,true}) for (int scenario=0;scenario<=14;++scenario) {
        Backend backend; backend.connected=true; backend.delayIdle=true;
        if(scenario>=1 && scenario<=7) backend.disconnectFault=scenario;
        if(scenario==8 || scenario==9) backend.connectFault=scenario-7;
        if(scenario==13) backend.disconnectRemainsRunning=true;
        if(scenario==14) backend.connectFault=3;
        auto app=backend.app();
        dialogs::dialogTexts.clear();
        dialogs::answer(IDYES,[&] {
            saveForReconnect(*app,editor);
            require(until([&] { return backend.disconnects==1; }));
            const auto reads=backend.statuses.load();
            if(scenario!=7) require(until([&] { return backend.statuses>reads; }));
            PHPVPN_CHECK_EQ(backend.connects.load(),0u);
            if(scenario==10) SendMessageW(app->window(),WM_COMMAND,IDM_TOGGLE,0); // supersede, even if refused as pending
            if(scenario==11) DestroyWindow(top(L"PhpVpnConnectingProgress"));
            if(scenario==12) ++backend.connectionEpoch;
            backend.idleReady=true;
            if(scenario==0 || scenario==8 || scenario==9 || scenario>=13) {
                require(until([&] { return backend.connects==1; }));
                if(backend.connectFault) {
                    dialogs::dialogAnswer=IDOK;
                    require(until([&] { return dialogs::dialogTexts.size()>=2; }));
                } else {
                    require(until([&] {
                        const auto h=top(L"PhpVpnConnectingProgress");
                        // Both progress windows may coexist. Inspect all own windows.
                        bool found=false;
                        EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM p)->BOOL {
                            if(text(window)==L"PHPVPN connection") *reinterpret_cast<bool*>(p)=true;
                            return TRUE;
                        },reinterpret_cast<LPARAM>(&found));
                        return h && found;
                    }));
                }
            } else {
                const auto end=GetTickCount64()+650;
                while(GetTickCount64()<end) pump();
                PHPVPN_CHECK_EQ(backend.connects.load(),0u);
            }
            // A later healthy Idle never revives a failed or superseded intent.
            backend.disconnectFault=0;
            const auto end=GetTickCount64()+100;
            while(GetTickCount64()<end) pump();
        });
        const unsigned expected=scenario==0 || scenario==8 || scenario==9 || scenario>=13 ? 1u : 0u;
        PHPVPN_CHECK_EQ(backend.connects.load(),expected);
        { std::lock_guard lock(backend.mutex);
            PHPVPN_CHECK(!backend.connectionCommands.empty());
            PHPVPN_CHECK_EQ(backend.connectionCommands.front(),IpcCommand::disconnect);
            PHPVPN_CHECK_EQ(backend.connectionCommands.size(),static_cast<std::size_t>(1+expected));
            if(expected) PHPVPN_CHECK_EQ(backend.connectionCommands.back(),IpcCommand::connect);
        }
        app->endSession(); app.reset(); pump();
        std::cout << "[ RECONNECT IDLE ] editor=" << editor << " scenario=" << scenario << " connects=" << expected << '\n';
    }
}
PHPVPN_TEST(WP05Windows, ReconnectQueuedConnectIsDeferredCancellableAndNotRepeatable) {
    for(int scenario=0;scenario<4;++scenario) {
        Backend backend; backend.connected=true;
        auto app=backend.app();
        ReconnectMessageProbe probe(app->window());
        if(scenario==1) probe.beforeConnect=[&] { app->requestExit(); };
        if(scenario==2) probe.beforeConnect=[&] { SendMessageW(app->window(),WM_COMMAND,IDM_TOGGLE,0); };
        if(scenario==3) probe.beforeConnect=[&] { app->endSession(); };
        dialogs::answer(IDYES,[&] {
            saveForReconnect(*app,true);
            require(until([&] { return probe.queued==1; }));
            if(scenario==0 || scenario==2) {
                require(until([&] { return backend.connects==1; }));
                // Deliver duplicate and old reader wakeups after consumption.
                SendMessageW(app->window(),WM_APP+7,probe.lastToken,0);
                SendMessageW(app->window(),WM_APP+5,0,0);
                SendMessageW(app->window(),WM_APP+6,0,0);
                const auto end=GetTickCount64()+100; while(GetTickCount64()<end) pump();
                PHPVPN_CHECK_EQ(backend.connects.load(),1u);
            } else PHPVPN_CHECK_EQ(backend.connects.load(),0u);
        });
        app->endSession(); app.reset(); pump();
    }
}
PHPVPN_TEST(WP05Windows, ReconnectEditorNonConnectedClosesSilentlyAndLateStatusCannotOwnNewWindow) {
    for(int state=0;state<4;++state) {
        Backend backend; backend.offlineState=state;
        auto app=backend.app(); dialogs::dialogTexts.clear();
        dialogs::answer(IDYES,[&] {
            saveForReconnect(*app,true);
            require(until([&] { return !app->editorLifecycleSnapshot().open; }));
        });
        PHPVPN_CHECK(dialogs::dialogTexts.empty());
        PHPVPN_CHECK_EQ(backend.connects.load(),0u); PHPVPN_CHECK_EQ(backend.disconnects.load(),0u);
        app->endSession(); app.reset(); pump();
    }
    for(bool editor : {false,true}) {
        Backend backend; backend.connected=true;
        auto app=backend.app(); require(until([&] { return backend.statuses>0; }));
        backend.holdStatus=true;
        dialogs::dialogTexts.clear();
        dialogs::answer(IDYES,[&] {
            saveForReconnect(*app,editor);
            require(until([&] { return backend.held>0; }));
            const auto root=top(editor ? L"PhpVpnTrayEditor" : L"PhpVpnContracts");
            SendMessageW(root,WM_CLOSE,0,0);
            require(until([&] { return editor ? !app->editorLifecycleSnapshot().open : !app->contractsLifecycleSnapshot().open; }));
            backend.holdStatus=false; backend.release();
            SendMessageW(app->window(),WM_COMMAND,editor ? IDM_EDIT_CONFIG : 207,0);
            require(until([&] { return editor ? app->editorLifecycleSnapshot().open : app->contractsLifecycleSnapshot().open; }));
            const auto end=GetTickCount64()+150; while(GetTickCount64()<end) pump();
        });
        PHPVPN_CHECK(dialogs::dialogTexts.empty());
        PHPVPN_CHECK_EQ(backend.connects.load(),0u); PHPVPN_CHECK_EQ(backend.disconnects.load(),0u);
        app->endSession(); app.reset(); pump();
    }
}
PHPVPN_TEST(WP05Windows, ReconnectNotSentSaveKeepsDraftWithoutConnectionCommands) {
    for(bool editor : {false,true}) {
        Backend backend; backend.connected=true;
        auto app=backend.app();
        SendMessageW(app->window(),WM_COMMAND,editor ? IDM_EDIT_CONFIG : 207,0);
        require(until([&] { return editor ? app->editorLifecycleSnapshot().open : app->contractsLifecycleSnapshot().open; }));
        const auto root=top(editor ? L"PhpVpnTrayEditor" : L"PhpVpnContracts");
        if(editor) replace(field(root,IDC_TEXT_AREA),L"synthetic unsent draft");
        else { click(root,1000); replace(field(root,1001),std::wstring(64,L'a')); }
        backend.server.stop();
        dialogs::dialogTexts.clear();
        dialogs::answer(IDOK,[&] {
            SendMessageW(root,WM_COMMAND,editor ? IDC_SAVE : IDOK,0);
            require(until([&] { return editor ? !app->editorLifecycleSnapshot().saving : !app->contractsLifecycleSnapshot().saving; }));
        });
        if(editor) {
            const auto state=app->editorLifecycleSnapshot(); PHPVPN_CHECK(state.open && state.dirty && !state.unknown);
            PHPVPN_CHECK_EQ(text(field(root,IDC_TEXT_AREA)),L"synthetic unsent draft");
        } else {
            const auto state=app->contractsLifecycleSnapshot(); PHPVPN_CHECK(state.open && state.dirty && !state.unknown);
            PHPVPN_CHECK_EQ(text(field(root,1001)),std::wstring(64,L'a'));
        }
        PHPVPN_CHECK_EQ(backend.saves.load(),0u);
        PHPVPN_CHECK_EQ(backend.connects.load(),0u); PHPVPN_CHECK_EQ(backend.disconnects.load(),0u);
        PHPVPN_CHECK(std::none_of(dialogs::dialogTexts.begin(),dialogs::dialogTexts.end(),[](const auto& text) { return text.find(kReconnectSavedText)!=std::wstring::npos; }));
        app->endSession(); app.reset(); pump();
    }
}
PHPVPN_TEST(WP05Windows, NativeRowsKeysChildrenAndImmutableTargets) {
    Backend backend; ContractsWindow window(backend.pipe);
    require(window.open(GetModuleHandleW(nullptr),nullptr,contracts_test::document(),"Z:\\synthetic\\config.json","one"));
    const auto root = window.window();
    PHPVPN_CHECK_EQ(text(root),L"Communication contracts");
    const auto rowControls = [&] {
        unsigned count = 0;
        EnumChildWindows(root,[](HWND h,LPARAM p)->BOOL {
            const auto id=GetDlgCtrlID(h);
            if(id>=1000 && id<1080) ++*reinterpret_cast<unsigned*>(p);
            return TRUE;
        },reinterpret_cast<LPARAM>(&count));
        return count;
    };
    // The model has 252 peers, while only 20 rows with three controls each
    // are materialized on the current page. Disabled keys are read-only.
    PHPVPN_CHECK_EQ(rowControls(),60u); PHPVPN_CHECK(!window.lifecycleSnapshot().dirty);
    auto key = field(root,1001);
    PHPVPN_CHECK((GetWindowLongPtrW(key,GWL_STYLE)&ES_READONLY)!=0);
    replace(key,L"blocked"); PHPVPN_CHECK(text(key).empty());
    click(root,1000);
    replace(key,std::wstring(65,L'a')); PHPVPN_CHECK_EQ(text(key).size(),65u);
    PHPVPN_CHECK(!IsWindowEnabled(field(root,IDOK)));
    replace(key,std::wstring(64,L'a')); PHPVPN_CHECK(IsWindowEnabled(field(root,IDOK)));
    click(root,1002); const auto first = top(L"PhpVpnPasswordSha"); require(first!=nullptr);
    click(root,1002); const auto second = top(L"PhpVpnPasswordSha"); require(second && first!=second);
    PHPVPN_CHECK_EQ(text(first),L"Password SHA - Contract 10.99.99.2_10.99.99.3"); PHPVPN_CHECK_EQ(text(first),text(second));
    PHPVPN_CHECK(GetWindowLongPtrW(first,GWL_EXSTYLE)&WS_EX_APPWINDOW); PHPVPN_CHECK(IsWindowEnabled(root));
    click(root,1004); click(root,1006);const auto different=top(L"PhpVpnPasswordSha");
    PHPVPN_CHECK_EQ(text(different),L"Password SHA - Contract 10.99.99.2_10.99.99.4");
    IAccessible* accessible=nullptr;require(SUCCEEDED(AccessibleObjectFromWindow(different,static_cast<DWORD>(OBJID_WINDOW),IID_IAccessible,reinterpret_cast<void**>(&accessible))));
    VARIANT self{};self.vt=VT_I4;self.lVal=CHILDID_SELF;BSTR title=nullptr;require(SUCCEEDED(accessible->get_accName(self,&title)));
    PHPVPN_CHECK_EQ(std::wstring(title,SysStringLen(title)),text(different));SysFreeString(title);accessible->Release();
    replace(field(first,501),L"first"); const auto firstHash=text(field(first,502));
    replace(field(second,501),L"second"); const auto secondHash=text(field(second,502));
    const auto lastPage = FindWindowExW(GetParent(field(root,900)),nullptr,L"BUTTON",L"13");
    require(lastPage!=nullptr); click(root,GetDlgCtrlID(lastPage));
    require(until([&] { return rowControls()==36u; }));
    PHPVPN_CHECK_EQ(text(first),text(second));
    PHPVPN_CHECK_EQ(text(different),L"Password SHA - Contract 10.99.99.2_10.99.99.4");
    click(first,IDOK); require(until([&] { return rowControls()==60u; }));
    key=field(root,1001); PHPVPN_CHECK_EQ(text(key),firstHash);
    click(second,IDOK); PHPVPN_CHECK_EQ(text(key),secondHash);
    click(root,1004); // Leave the other Password draft unconfirmed and disabled.
    click(root,1002); const auto unconfirmed=top(L"PhpVpnPasswordSha"); replace(field(unconfirmed,501),L"discard child");
    window.setConnectionActive(true); backend.connected=true; dialogs::dialogTexts.clear();
    dialogs::answer(IDNO,[&] {
        click(root,IDOK); PHPVPN_CHECK(!IsWindow(unconfirmed)&&!IsWindow(different));
        require(until([&] { return !window.isOpen(); }));
    });
    PHPVPN_CHECK(std::any_of(dialogs::dialogTexts.begin(),dialogs::dialogTexts.end(),[](const auto& value) {
        return value.find(L"Configuration saved")!=std::wstring::npos;
    }));
    PHPVPN_CHECK_EQ(backend.connects.load(),0u); PHPVPN_CHECK_EQ(backend.disconnects.load(),0u);
    PHPVPN_CHECK_EQ(backend.saves.load(),1u); PHPVPN_CHECK_EQ(backend.documents.load(),0u);
    auto saved=ContractsDocument::parse(backend.stored); PHPVPN_CHECK(saved && saved.value()->existingKey(contracts_test::ip("10.99.99.3"))==narrow(secondHash));
    PHPVPN_CHECK_EQ(backend.unexpected.load(),0u);
}
class DiagnosticClipboardRestore {
    std::vector<std::pair<UINT, HANDLE>> data_;
    HWND owner_;
    bool captured_ = false;
public:
    explicit DiagnosticClipboardRestore(HWND owner) : owner_(owner) {}
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
    ~DiagnosticClipboardRestore() {
        if (captured_) {
            bool opened = false;
            for (int retry = 0; retry < 100 && !opened; ++retry) {
                opened = OpenClipboard(owner_) != FALSE;
                if (!opened) Sleep(10);
            }
            PHPVPN_CHECK(opened);
            if (opened) {
                PHPVPN_CHECK(EmptyClipboard());
                for (auto& [format, handle] : data_) {
                    if (SetClipboardData(format, handle)) handle = nullptr;
                    else PHPVPN_CHECK(false);
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
PHPVPN_TEST(WP05Windows, HttpDiagnosticErrorButtonPaletteStates) {
    const auto instance = GetModuleHandleW(nullptr);
    const auto owner = CreateWindowExW(0, L"STATIC", L"palette fixture", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr, nullptr, instance, nullptr);
    require(owner != nullptr);
    UiResources resources; require(resources.update(instance, 96, false));
    const auto button = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 160, 40, owner, nullptr, instance, nullptr);
    require(button && attachModernButton(button, resources, ButtonRole::error));
    const auto fill = [&] {
        const auto source = GetDC(button), memory = CreateCompatibleDC(source);
        const auto bitmap = CreateCompatibleBitmap(source, 160, 40); const auto old = SelectObject(memory, bitmap);
        SendMessageW(button, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
        const auto color = GetPixel(memory, 80, 20);
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(button, source);
        return color;
    };
    PHPVPN_CHECK_EQ(fill(), resources.color(resources.highContrast() ? ui::Color::primary : ui::Color::error));
    SendMessageW(button, WM_MOUSEMOVE, 0, MAKELPARAM(80, 20));
    PHPVPN_CHECK_EQ(fill(), resources.color(resources.highContrast() ? ui::Color::primary : ui::Color::buttonResetHover));
    SendMessageW(button, BM_SETSTATE, TRUE, 0);
    PHPVPN_CHECK_EQ(fill(), resources.color(resources.highContrast() ? ui::Color::primary : ui::Color::buttonResetPressed));
    SendMessageW(button, BM_SETSTATE, FALSE, 0); EnableWindow(button, FALSE);
    PHPVPN_CHECK_EQ(fill(), resources.color(ui::Color::errorBackground));
    DestroyWindow(owner);
}
PHPVPN_TEST(WP05Windows, HttpDiagnosticMaximumProjectionTabsAndOwnerDestruction) {
    const auto instance = GetModuleHandleW(nullptr);
    HWND owner = CreateWindowExW(0, L"STATIC", L"diagnostic fixture owner", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, instance, nullptr);
    require(owner != nullptr);
    HttpDiagnostic d; d.action = HttpAction::health; d.responseComplete = false;
    d.request = {65537, std::string(64512, '\0'), std::string(1024, '\0')};
    const std::string response = "HTTP/? 200\r\n\r\n<script>inert</script>\nlast\r\n";
    d.response = {response.size(), response, {}};
    HttpErrorDetailWindow detail; require(detail.open(instance, owner, d));
    const auto h = detail.handle();
    PHPVPN_CHECK_EQ(GetWindow(h, GW_OWNER), owner);
    auto request = field(h, 781), responseEdit = field(h, 782), tabs = field(h, 780);
    PHPVPN_CHECK_EQ(text(request).size(), kHttpDiagnosticDisplayLimit);
    PHPVPN_CHECK_EQ(text(request), diagnosticDisplayText(d.request).text);
    PHPVPN_CHECK_EQ(text(responseEdit), diagnosticDisplayText(d.response).text);
    {
        DiagnosticClipboardRestore clipboard(h);
        const bool saved = clipboard.capture();
        if (!saved) std::cout << "[ NOT RUN ] HTTP diagnostic Ctrl+C: existing clipboard formats cannot be preserved\n";
        if (saved) {
            SendMessageW(request, EM_SETSEL, kHttpDiagnosticDisplayLimit - 4, kHttpDiagnosticDisplayLimit);
            SetFocus(request);
            BYTE previousKeys[256]{}, copyKeys[256]{}; GetKeyboardState(previousKeys);
            std::copy(std::begin(previousKeys), std::end(previousKeys), std::begin(copyKeys));
            copyKeys[VK_CONTROL] = 0x80; SetKeyboardState(copyKeys);
            SendMessageW(request, WM_KEYDOWN, 'C', 0); SetKeyboardState(previousKeys);
            const bool opened = OpenClipboard(h) != FALSE; PHPVPN_CHECK(opened);
            if (opened) {
                const auto handle = GetClipboardData(CF_UNICODETEXT);
                const auto value = handle ? static_cast<const wchar_t*>(GlobalLock(handle)) : nullptr;
                PHPVPN_CHECK(value != nullptr);
                if (value) { PHPVPN_CHECK_EQ(std::wstring(value), L"\\x00"); GlobalUnlock(handle); }
                CloseClipboard();
            }
        }
    }
    PHPVPN_CHECK_EQ(TabCtrl_GetItemCount(tabs), 2);
    for (int i = 0; i < 2; ++i) {
        wchar_t label[32]{}; TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = label; item.cchTextMax = 32;
        PHPVPN_CHECK(TabCtrl_GetItem(tabs, i, &item)); PHPVPN_CHECK_EQ(std::wstring(label), i ? L"Response" : L"Request");
    }
    SendMessageW(request, EM_SETSEL, kHttpDiagnosticDisplayLimit - 4, kHttpDiagnosticDisplayLimit);
    SendMessageW(request, EM_SCROLLCARET, 0, 0);
    DWORD start = 0, end = 0; SendMessageW(request, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    PHPVPN_CHECK_EQ(end, kHttpDiagnosticDisplayLimit);
    PHPVPN_CHECK_EQ(start, kHttpDiagnosticDisplayLimit - 4);
    const auto previous = text(request);
    SendMessageW(request, WM_CHAR, 'z', 0); PHPVPN_CHECK_EQ(text(request), previous);
    for (int dpi : {96, 144, 192}) {
        RECT bounds{}; GetWindowRect(h, &bounds); SendMessageW(h, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&bounds));
        resizeClient(h, 480, 400); pump();
        PHPVPN_CHECK_EQ(text(request).size(), kHttpDiagnosticDisplayLimit);
    }
    PHPVPN_CHECK(!GetDlgItem(h, IDOK) && !GetDlgItem(h, IDCANCEL));
    DestroyWindow(owner); pump(); PHPVPN_CHECK(!detail.handle());
}
PHPVPN_TEST(WP05Windows, PasswordUnicodeLimitPreservesSelectionAndResult) {
    contracts_test::Crypto crypto; auto model=ContractsModel::create(crypto,contracts_test::document()).value();
    PasswordShaWindow window(crypto,model->target(contracts_test::ip("10.99.99.20")).value(),[](const ContractTarget&,std::string_view){return true;});
    require(window.open(GetModuleHandleW(nullptr),nullptr)); const auto root=window.window(), input=field(root,501), hash=field(root,502);
    PHPVPN_CHECK_EQ(text(input),L""); PHPVPN_CHECK_EQ(text(hash),L"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const auto dirtyBefore=model->dirty();
    click(root,503);
    PHPVPN_CHECK_EQ(text(input),L"10.99.99.2_10.99.99.20");
    const auto defaultKey=defaultPayloadKey(crypto,contracts_test::ip("10.99.99.20"),model->localIp());
    PHPVPN_REQUIRE_OK(defaultKey);
    PHPVPN_CHECK_EQ(narrow(text(hash)),defaultKey.value());
    PHPVPN_CHECK_EQ(model->dirty(),dirtyBefore);
    PHPVPN_CHECK((GetWindowLongPtrW(hash,GWL_STYLE)&ES_READONLY)!=0 && IsWindowEnabled(hash));
    PHPVPN_CHECK(GetNextDlgTabItem(root,input,FALSE)==hash);
    PHPVPN_CHECK(GetNextDlgTabItem(root,hash,FALSE)==field(root,503));
    PHPVPN_CHECK(GetNextDlgTabItem(root,field(root,503),FALSE)==field(root,504));
    PHPVPN_CHECK(GetNextDlgTabItem(root,field(root,504),FALSE)==field(root,IDOK));
    click(root,504);
    const auto randomPassword=text(input);
    PHPVPN_CHECK_EQ(randomPassword.size(),64u);
    PHPVPN_CHECK(randomPassword.find_first_not_of(L"ghjkmnpqrstuvwxyz")==std::wstring::npos);
    PasswordKeyModel expectedRandom;
    PHPVPN_CHECK(expectedRandom.update(crypto,{reinterpret_cast<const char16_t*>(randomPassword.data()),randomPassword.size()})==PasswordKeyError::none);
    PHPVPN_CHECK_EQ(narrow(text(hash)),expectedRandom.key());
    PHPVPN_CHECK_EQ(model->dirty(),dirtyBefore);
    crypto.failRandom=true; click(root,504); PHPVPN_CHECK_EQ(text(input),randomPassword);
    crypto.failRandom=false; click(root,503);
    PHPVPN_CHECK((GetWindowLongPtrW(hash,GWL_STYLE)&WS_BORDER)==0);
    capture(root,L"password-micro010-default.bmp");
    MINMAXINFO minimum{}; SendMessageW(root,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&minimum));
    const auto dpi=GetDpiForWindow(root);
    RECT expectedMinimum{0,0,scaleForDpi(480,dpi),scaleForDpi(240,dpi)};
    AdjustWindowRectExForDpi(&expectedMinimum,static_cast<DWORD>(GetWindowLongPtrW(root,GWL_STYLE)),FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(root,GWL_EXSTYLE)),dpi);
    PHPVPN_CHECK_EQ(minimum.ptMinTrackSize.x,expectedMinimum.right-expectedMinimum.left);
    PHPVPN_CHECK_EQ(minimum.ptMinTrackSize.y,expectedMinimum.bottom-expectedMinimum.top);
    SetWindowPos(root,nullptr,0,0,minimum.ptMinTrackSize.x,minimum.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER);
    RECT body{},inputBounds{};GetClientRect(GetParent(input),&body);GetWindowRect(input,&inputBounds);
    MapWindowPoints(nullptr,GetParent(input),reinterpret_cast<POINT*>(&inputBounds),2);
    PHPVPN_CHECK(inputBounds.top>=0 && inputBounds.bottom<=body.bottom);
    capture(root,L"password-micro010-minimum.bmp");
    std::wstring emoji; for(unsigned i=0;i<255;++i) emoji+=L"\xd83d\xde00";
    replace(input,emoji); const auto expected=text(hash); PHPVPN_CHECK_EQ(text(input).size(),510u);
    SendMessageW(input,EM_SETSEL,5,5); SendMessageW(input,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"x"));
    DWORD first=0,last=0; SendMessageW(input,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
    PHPVPN_CHECK_EQ(text(input),emoji); PHPVPN_CHECK_EQ(text(hash),expected); PHPVPN_CHECK_EQ(first,5u); PHPVPN_CHECK_EQ(last,5u);
    SendMessageW(input,WM_CHAR,0,0); PHPVPN_CHECK_EQ(text(input),emoji); PHPVPN_CHECK_EQ(text(hash),L""); PHPVPN_CHECK(!IsWindowEnabled(field(root,IDOK)));
    replace(input,L"e\x0301"); PHPVPN_CHECK(IsWindowEnabled(field(root,IDOK)));
    crypto.failHash=true; replace(input,L"failure"); PHPVPN_CHECK_EQ(text(hash),L""); PHPVPN_CHECK(!IsWindowEnabled(field(root,IDOK)));
    window.close();
}
PHPVPN_TEST(WP05Windows, ManualLoadSingleReadReopenAndCommonExitNoDiscard) {
    Backend backend; auto app=backend.app();
    SendMessageW(app->window(),WM_COMMAND,207,0); require(until([&] { return app->contractsLifecycleSnapshot().open; }));
    const auto root=top(L"PhpVpnContracts"); replace(field(root,1001),std::wstring(64,L'b'));
    const auto before=app->contractsLifecycleSnapshot(); SendMessageW(app->window(),WM_COMMAND,207,0);
    PHPVPN_CHECK_EQ(app->contractsLifecycleSnapshot(),before); PHPVPN_CHECK_EQ(backend.documents.load(),1u); PHPVPN_CHECK_EQ(backend.states.load(),0u);
    SendMessageW(app->window(),WM_COMMAND,IDM_EDIT_CONFIG,0); require(until([&] { return app->editorLifecycleSnapshot().open; }));
    const auto editor=top(L"PhpVpnTrayEditor"); SendMessageW(field(editor,IDC_TEXT_AREA),EM_SETSEL,0,0); SendMessageW(field(editor,IDC_TEXT_AREA),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L" "));
    const auto editorBefore=app->editorLifecycleSnapshot();
    const auto count=dialogs::dialogTexts.size(); dialogs::answer(IDNO,[&] { app->requestExit(); });
    PHPVPN_CHECK_EQ(dialogs::dialogTexts.size(),count+1); PHPVPN_CHECK_EQ(app->editorLifecycleSnapshot(),editorBefore); PHPVPN_CHECK_EQ(app->contractsLifecycleSnapshot(),before);
    dialogs::answer(IDYES,[&] { app->requestExit(); }); PHPVPN_CHECK(!app->window()); PHPVPN_CHECK(!IsWindow(root)); PHPVPN_CHECK(!IsWindow(editor));
    PHPVPN_CHECK_EQ(backend.saves.load(),0u); PHPVPN_CHECK_EQ(backend.disconnects.load(),0u);
}
PHPVPN_TEST(WP05Windows, UnknownCheckMatchAndConfirmedRepeatAreExplicit) {
    Backend backend; backend.invalidReply=true;
    ContractsWindow window(backend.pipe); require(window.open(GetModuleHandleW(nullptr),nullptr,backend.stored,"synthetic","one"));
    window.setConnectionActive(true); dialogs::dialogTexts.clear();
    click(window.window(),1000); replace(field(window.window(),1001),std::wstring(64,L'a'));
    click(window.window(),IDOK); require(until([&] { return window.lifecycleSnapshot().unknown; }));
    PHPVPN_CHECK_EQ(backend.saves.load(),1u); PHPVPN_CHECK_EQ(backend.documents.load(),0u);
    PHPVPN_CHECK(dialogs::dialogTexts.empty());
    dialogs::answer(IDOK,[&] { click(window.window(),800); require(until([&] { return !window.isOpen(); })); });
    PHPVPN_CHECK_EQ(backend.documents.load(),1u); PHPVPN_CHECK_EQ(backend.saves.load(),1u);
    backend.publish=false; require(window.open(GetModuleHandleW(nullptr),nullptr,contracts_test::document(),"synthetic","two"));
    window.setConnectionActive(false);
    click(window.window(),1000); replace(field(window.window(),1001),std::wstring(64,L'c')); click(window.window(),IDOK);
    require(until([&] { return window.lifecycleSnapshot().unknown; })); click(window.window(),800);
    require(until([&] { return IsWindowVisible(field(window.window(),801))!=FALSE; }));
    PHPVPN_CHECK_EQ(backend.saves.load(),2u); backend.invalidReply=false; backend.publish=true;
    dialogs::answer(IDYES,[&] { click(window.window(),801); }); require(until([&] { return !window.isOpen(); }));
    PHPVPN_CHECK_EQ(backend.saves.load(),3u); PHPVPN_CHECK_EQ(backend.documents.load(),2u);
    PHPVPN_CHECK_EQ(backend.saveIds.size(),3u); PHPVPN_CHECK(backend.saveIds[1]!=backend.saveIds[2]);
}
PHPVPN_TEST(WP05Windows, ManualAvailabilityMatrixNeverCreatesOrFallsBack) {
    Backend backend;auto app=backend.app();
    auto repair=Json::parse(contracts_test::document()).value();
    for(auto& member:repair.objectValue())if(member.first=="masterSetup")std::erase_if(member.second.objectValue(),[](const auto& item){return item.first=="accessKey";});
    for(const auto& entry:std::vector<std::pair<std::string,std::string>>{{"missing",""},{"invalid","{"},{"repairable",Json::serialize(repair)}}) {
        {std::lock_guard lock(backend.mutex);backend.availability=entry.first;backend.stored=entry.second;}
        const auto before=dialogs::dialogTexts.size();
        dialogs::answer(IDOK,[&]{SendMessageW(app->window(),WM_COMMAND,207,0);require(until([&]{return dialogs::dialogTexts.size()>before;}));});
        PHPVPN_CHECK(!app->contractsLifecycleSnapshot().open);PHPVPN_CHECK(dialogs::dialogTexts.back().find(widen(entry.first))!=std::wstring::npos);
    }
    for(const auto error:{ErrorCode::config_unreadable,ErrorCode::config_encoding_error,ErrorCode::config_too_large,ErrorCode::config_directory_security_invalid,ErrorCode::busy}) {
        {std::lock_guard lock(backend.mutex);backend.readError=error;}
        const auto before=dialogs::dialogTexts.size();
        dialogs::answer(IDOK,[&]{SendMessageW(app->window(),WM_COMMAND,207,0);require(until([&]{return dialogs::dialogTexts.size()>before;}));});
        PHPVPN_CHECK(!app->contractsLifecycleSnapshot().open);
    }
    PHPVPN_CHECK_EQ(backend.documents.load(),8u);PHPVPN_CHECK_EQ(backend.states.load(),0u);PHPVPN_CHECK_EQ(backend.saves.load(),0u);PHPVPN_CHECK_EQ(backend.unexpected.load(),0u);
    app->endSession();app.reset();pump();
}
PHPVPN_TEST(WP05Windows, OriginalDeadlineExitWaitAndIndependentEditor) {
    Backend backend; backend.holdSave=true;
    auto app=backend.app(); SendMessageW(app->window(),WM_COMMAND,207,0);
    require(until([&] { return app->contractsLifecycleSnapshot().open; }));
    const auto root=top(L"PhpVpnContracts"); click(root,1000); click(root,IDOK);
    require(until([&] { return backend.saves==1; }));
    const auto saving=app->contractsLifecycleSnapshot(); require(saving.saving);
    app->requestExit(); PHPVPN_CHECK(app->window()!=nullptr);
    SendMessageW(app->window(),WM_COMMAND,IDM_EDIT_CONFIG,0);
    require(until([&] { return app->editorLifecycleSnapshot().open; }));
    const auto edit=field(top(L"PhpVpnTrayEditor"),IDC_TEXT_AREA);
    SendMessageW(edit,EM_SETSEL,0,0); SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L" "));
    PHPVPN_CHECK(app->editorLifecycleSnapshot().dirty); PHPVPN_CHECK(IsWindowEnabled(edit));
    PHPVPN_CHECK_EQ(app->contractsLifecycleSnapshot().deadlineMs,saving.deadlineMs);
    // One production 30 s deadline, not a test-only shortened operation.
    const auto dialogsBefore=dialogs::dialogTexts.size();
    dialogs::answer(IDNO,[&] { require(until([&] { return app->contractsLifecycleSnapshot().unknown && dialogs::dialogTexts.size()>dialogsBefore; },32000)); });
    PHPVPN_CHECK(app->window()!=nullptr); PHPVPN_CHECK(app->editorLifecycleSnapshot().dirty);
    PHPVPN_CHECK_EQ(backend.saves.load(),1u); PHPVPN_CHECK_EQ(dialogs::dialogTexts.size(),dialogsBefore+1);
    PHPVPN_CHECK(GetTickCount64()>=saving.deadlineMs && GetTickCount64()-saving.deadlineMs<1500);
    app->endSession(); backend.release();
}
PHPVPN_TEST(WP05Windows, FailedCheckBlocksRepeatAndMissingRequiresFreshConfirmation) {
    Backend backend;backend.invalidReply=true;backend.publish=false;
    ContractsWindow window(backend.pipe);require(window.open(GetModuleHandleW(nullptr),nullptr,backend.stored,"synthetic","unknown"));
    click(window.window(),1000); replace(field(window.window(),1001),std::wstring(64,L'a'));
    click(window.window(),IDOK);require(until([&]{return window.lifecycleSnapshot().unknown;}));
    const auto generation=window.lifecycleSnapshot().operationGeneration;
    backend.documentError=true;click(window.window(),800);require(until([&]{return !window.lifecycleSnapshot().checking;}));
    PHPVPN_CHECK(!IsWindowVisible(field(window.window(),801)));PHPVPN_CHECK_EQ(backend.saves.load(),1u);
    backend.documentError=false;{std::lock_guard lock(backend.mutex);backend.availability="missing";}
    click(window.window(),800);require(until([&]{return IsWindowVisible(field(window.window(),801))!=FALSE;}));
    resizeClient(window.window(),480,320);capture(window.window(),L"contracts-unknown-small.bmp");
    RECT r{},button{};GetClientRect(window.window(),&r);GetWindowRect(field(window.window(),801),&button);MapWindowPoints(nullptr,window.window(),reinterpret_cast<POINT*>(&button),2);
    PHPVPN_CHECK(button.top>=0 && button.bottom<=r.bottom);
    dialogs::answer(IDNO,[&]{click(window.window(),801);});PHPVPN_CHECK_EQ(window.lifecycleSnapshot().operationGeneration,generation);
    PHPVPN_CHECK_EQ(backend.documents.load(),2u);PHPVPN_CHECK_EQ(backend.saves.load(),1u);
    window.endSession();
}
PHPVPN_TEST(WP05Windows, CommonExitRevalidatesAllDraftsAfterModalReentry) {
    Backend backend;auto app=backend.app();SendMessageW(app->window(),WM_COMMAND,207,0);require(until([&]{return app->contractsLifecycleSnapshot().open;}));
    const auto root=top(L"PhpVpnContracts");click(root,1000);
    const auto key=field(root,1001);replace(key,std::wstring(64,L'a'));
    static thread_local HWND changedField=nullptr;static thread_local unsigned answers=0;
    changedField=key;answers=0;
    const auto timer=SetTimer(nullptr,0,10,[](HWND,UINT,UINT_PTR,DWORD){
        if(!changedField)return;
        const auto dialog=top(L"#32770");if(!dialog || !IsWindowVisible(dialog))return;
        if(answers==0)replace(changedField,std::wstring(64,L'b'));
        dialogs::dialogAnswer=answers++==0?IDYES:IDNO;dialogs::answerDialog(nullptr,0,0,0);
    });
    struct TimerLifetime { UINT_PTR id; HWND& field; void stop(){field=nullptr;if(id){KillTimer(nullptr,id);id=0;}} ~TimerLifetime(){stop();} } lifetime{timer,changedField};
    app->requestExit();require(until([&]{return answers>=2;}));lifetime.stop();pump();
    PHPVPN_CHECK(app->window()!=nullptr);PHPVPN_CHECK(app->contractsLifecycleSnapshot().dirty);PHPVPN_CHECK_EQ(text(key),std::wstring(64,L'b'));
    PHPVPN_CHECK_EQ(backend.saves.load(),0u);PHPVPN_CHECK_EQ(backend.disconnects.load(),0u);app->endSession();
}
PHPVPN_TEST(WP05Windows, ReleaseResourcesAccessibilityAnd252RowTiming) {
    Backend backend; const auto instance=GetModuleHandleW(nullptr);
    auto app=backend.app();require(until([&]{return backend.statuses>0;}));pump();
    // Warm up the same native paint, input, MSAA and candidate paths that are
    // measured below. First use of the accessibility proxy is process-wide.
    {
        ContractsWindow warm(backend.pipe); require(warm.open(instance,nullptr,backend.stored,"synthetic","warm"));
        SetFocus(field(warm.window(),2007));SetFocus(field(warm.window(),1001));replace(field(warm.window(),1001),std::wstring(64,L'a'));
        for(unsigned paint=0;paint<500;++paint)RedrawWindow(warm.window(),nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
        IAccessible* accessible=nullptr;require(SUCCEEDED(AccessibleObjectFromWindow(field(warm.window(),1001),static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(&accessible))));
        VARIANT child{};child.vt=VT_I4;child.lVal=CHILDID_SELF;BSTR name=nullptr;require(SUCCEEDED(accessible->get_accName(child,&name)));SysFreeString(name);accessible->Release();
        click(warm.window(),1003);const auto password=top(L"PhpVpnPasswordSha");replace(field(password,501),std::wstring(255,L'x'));RedrawWindow(password,nullptr,nullptr,RDW_UPDATENOW|RDW_ALLCHILDREN);SendMessageW(password,WM_CLOSE,0,0);
        auto crypto=createPlatformCryptoProvider().value();auto model=ContractsModel::create(*crypto,backend.stored).value();for(const auto& row:model->rows())require(model->setEnabled(row.remoteIp(),true).ok());require(model->prepareSave(contractsEnvelopeSize).ok());
        warm.endSession();
    }
    pump();
    const auto baseUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS), baseGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    PROCESS_MEMORY_COUNTERS_EX before{}; before.cb=sizeof(before); GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before),sizeof(before));
    using Clock=std::chrono::steady_clock;
    const auto elapsed=[](Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
    double worstOpen=0,worstScroll=0,worstKey=0,worstPassword=0,worstSave=0; DWORD peakUser=0,peakGdi=0; SIZE_T peakBytes=0,peakWorkingSet=0;
    for(unsigned cycle=0;cycle<20;++cycle) {
        ContractsWindow window(backend.pipe); const auto started=Clock::now();
        require(window.open(instance,nullptr,backend.stored,"synthetic","cycle"));
        RedrawWindow(window.window(),nullptr,nullptr,RDW_UPDATENOW|RDW_ALLCHILDREN);
        const double openMs=elapsed(started); worstOpen=std::max(worstOpen,openMs);
        peakUser=std::max(peakUser,GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)-baseUser);
        peakGdi=std::max(peakGdi,GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)-baseGdi);
        PROCESS_MEMORY_COUNTERS_EX current{}; current.cb=sizeof(current); GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&current),sizeof(current));
        if(current.PrivateUsage>before.PrivateUsage) peakBytes=std::max(peakBytes,current.PrivateUsage-before.PrivateUsage);
        if(current.WorkingSetSize>before.WorkingSetSize) peakWorkingSet=std::max(peakWorkingSet,current.WorkingSetSize-before.WorkingSetSize);
        const auto root=window.window(), key=field(root,1001), last=field(root,2007);
        const auto scrollStart=Clock::now(); SetFocus(last); RedrawWindow(root,nullptr,nullptr,RDW_UPDATENOW|RDW_ALLCHILDREN); const double scrollMs=elapsed(scrollStart); worstScroll=std::max(worstScroll,scrollMs);
        SetFocus(key); RedrawWindow(root,nullptr,nullptr,RDW_UPDATENOW|RDW_ALLCHILDREN);
        const auto typeStart=Clock::now(); replace(key,std::wstring(64,L'a')); RedrawWindow(root,nullptr,nullptr,RDW_UPDATENOW|RDW_ALLCHILDREN); const double keyMs=elapsed(typeStart); worstKey=std::max(worstKey,keyMs);
        IAccessible* accessible=nullptr;
        require(SUCCEEDED(AccessibleObjectFromWindow(key,static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(&accessible))));
        VARIANT child{}; child.vt=VT_I4; child.lVal=CHILDID_SELF; BSTR name=nullptr;
        require(SUCCEEDED(accessible->get_accName(child,&name)));
        PHPVPN_CHECK_EQ(std::wstring(name,SysStringLen(name)),L"10.99.99.2_10.99.99.3 Key"); SysFreeString(name); accessible->Release();
        if(cycle==0) {
            const auto gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
            for(unsigned paint=0;paint<500;++paint) RedrawWindow(root,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
            PHPVPN_CHECK_EQ(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),gdi);
            PHPVPN_CHECK_EQ(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS),user);
        }
        auto crypto=createPlatformCryptoProvider().value(); auto model=ContractsModel::create(*crypto,backend.stored).value();
        for(const auto& row:model->rows())require(model->setEnabled(row.remoteIp(),true).ok());
        const auto saveStart=Clock::now(); auto candidate=model->prepareSave(contractsEnvelopeSize); require(candidate.ok());
        const double saveMs=elapsed(saveStart); worstSave=std::max(worstSave,saveMs);
        click(root,1003);const auto password=top(L"PhpVpnPasswordSha");const auto passwordStart=Clock::now();replace(field(password,501),std::wstring(255,L'x'));RedrawWindow(password,nullptr,nullptr,RDW_UPDATENOW|RDW_ALLCHILDREN);
        const double passwordMs=elapsed(passwordStart);worstPassword=std::max(worstPassword,passwordMs);SendMessageW(password,WM_CLOSE,0,0);
        std::cout<<"[ SAMPLE ] cycle="<<cycle+1<<", open="<<openMs<<", scroll="<<scrollMs<<", key="<<keyMs<<", password="<<passwordMs<<", candidate="<<saveMs<<" ms\n";
        window.endSession(); pump();
    }
    PHPVPN_CHECK(worstOpen<=1000 && worstScroll<=100 && worstKey<=100 && worstPassword<=100 && worstSave<=2000);
    PHPVPN_CHECK(peakUser<=1200 && peakGdi<=1200 && peakBytes<=10*1024*1024 && peakWorkingSet<=10*1024*1024);
    const auto remainingUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS),remainingGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    PHPVPN_CHECK(remainingUser<=baseUser+5 && remainingGdi<=baseGdi+5);
    PROCESS_MEMORY_COUNTERS_EX after{};after.cb=sizeof(after);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after),sizeof(after));
    PHPVPN_CHECK(after.PrivateUsage<=before.PrivateUsage+1024*1024 && after.WorkingSetSize<=before.WorkingSetSize+1024*1024);
    std::cout<<"[ EVIDENCE ] warmup + 20 cycles: open="<<worstOpen<<"ms, scroll="<<worstScroll<<"ms, key="<<worstKey<<"ms, password="<<worstPassword<<"ms, candidate="<<worstSave
        <<"ms; peak USER="<<peakUser<<", GDI="<<peakGdi<<", private bytes="<<peakBytes<<", working set="<<peakWorkingSet<<"; remaining USER="<<(remainingUser-baseUser)<<", GDI="<<(remainingGdi-baseGdi)<<", private bytes="<<(static_cast<long long>(after.PrivateUsage)-static_cast<long long>(before.PrivateUsage))<<", working set="<<(static_cast<long long>(after.WorkingSetSize)-static_cast<long long>(before.WorkingSetSize))<<"\n";
    // This test owns a real TrayApplication. Its destruction posts WM_QUIT on
    // this UI thread. Consume that fixture message before another test opens a
    // modal dialog, otherwise MessageBox returns without waiting for its answer.
    app->endSession();app.reset();
    MSG quit{};PHPVPN_CHECK(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_NOREMOVE)!=FALSE);
    pump();PHPVPN_CHECK(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_NOREMOVE)==FALSE);
}
PHPVPN_TEST(WP05Windows, RealSessionBroadcastOnIsolatedDesktopNeverWaitsForReaders) {
    // Broadcast only on a newly created, unshown desktop. Never send synthetic
    // session messages to the user's desktop or request an actual logoff.
    const auto suffix=widen(createPlatformCryptoProvider().value()->randomUuidV4().value());
    const auto name=L"wp05-broadcast-"+suffix;
    const auto desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr); require(desktop!=nullptr);
    std::exception_ptr error;
    std::thread worker([&] {
        try {
            require(SetThreadDesktop(desktop)!=FALSE);
            wchar_t currentName[128]{}; DWORD needed=0;
            require(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,currentName,sizeof(currentName),&needed)!=FALSE);
            require(std::wstring(currentName)==name);
            Backend backend; auto app=backend.app();
            SendMessageW(app->window(),WM_COMMAND,207,0); require(until([&] { return app->contractsLifecycleSnapshot().open; }));
            const auto contracts=top(L"PhpVpnContracts"); click(contracts,1000);
            SendMessageW(app->window(),WM_COMMAND,IDM_EDIT_CONFIG,0); require(until([&] { return app->editorLifecycleSnapshot().open; }));
            const auto editor=top(L"PhpVpnTrayEditor"); replace(field(editor,IDC_TEXT_AREA),L"synthetic dirty editor");
            SendMessageW(app->window(),WM_COMMAND,206,0); require(until([&] { return app->wizardLifecycleSnapshot().open && field(top(L"PhpVpnConfigurationWizard"),710); }));
            const auto wizard=top(L"PhpVpnConfigurationWizard");
            require(until([&] { return IsWindowEnabled(field(wizard,710))!=FALSE; })); replace(field(wizard,710),L"http://broadcast.invalid/phpvpn.php");
            SendMessageW(app->window(),WM_COMMAND,IDM_TOGGLE,0); require(until([&] { return top(L"PhpVpnConnectingProgress")!=nullptr; }));
            const auto progress=top(L"PhpVpnConnectingProgress");
            backend.holdStatus=true; backend.holdSave=true; click(contracts,IDOK);
            require(until([&] { return backend.held>=2; },6500));
            const auto host=app->window(); require(GetAncestor(host,GA_ROOT)==host && GetParent(host)!=HWND_MESSAGE);
            struct Broadcasts { unsigned query=0,end=0; } broadcasts;
            require(SetWindowSubclass(host,[](HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data)->LRESULT {
                auto& b=*reinterpret_cast<Broadcasts*>(data); if(m==WM_QUERYENDSESSION)++b.query; if(m==WM_ENDSESSION)++b.end;
                return DefSubclassProc(h,m,w,l);
            },919,reinterpret_cast<DWORD_PTR>(&broadcasts))!=FALSE);
            bool onlyOwn=true;
            EnumDesktopWindows(desktop,[](HWND h,LPARAM p)->BOOL { DWORD pid=0; GetWindowThreadProcessId(h,&pid); if(pid!=GetCurrentProcessId())*reinterpret_cast<bool*>(p)=false; return TRUE; },reinterpret_cast<LPARAM>(&onlyOwn));
            require(onlyOwn);
            const auto es=app->editorLifecycleSnapshot();
            const auto ws=app->wizardLifecycleSnapshot(); const auto cs=app->contractsLifecycleSnapshot();
            DWORD recipients=BSM_APPLICATIONS;
            require(BroadcastSystemMessageW(BSF_QUERY|BSF_NOHANG,&recipients,WM_QUERYENDSESSION,0,ENDSESSION_LOGOFF)>0);
            PHPVPN_CHECK_EQ(broadcasts.query,1u); PHPVPN_CHECK_EQ(app->editorLifecycleSnapshot(),es); PHPVPN_CHECK_EQ(app->wizardLifecycleSnapshot(),ws); PHPVPN_CHECK_EQ(app->contractsLifecycleSnapshot(),cs);
            recipients=BSM_APPLICATIONS; require(BroadcastSystemMessageW(BSF_NOHANG,&recipients,WM_ENDSESSION,FALSE,ENDSESSION_LOGOFF)>0);
            PHPVPN_CHECK_EQ(app->editorLifecycleSnapshot(),es); PHPVPN_CHECK_EQ(app->wizardLifecycleSnapshot(),ws); PHPVPN_CHECK_EQ(app->contractsLifecycleSnapshot(),cs);
            const auto sends=backend.saves.load(),disconnects=backend.disconnects.load();
            const auto start=GetTickCount64(); recipients=BSM_APPLICATIONS;
            require(BroadcastSystemMessageW(BSF_NOHANG,&recipients,WM_ENDSESSION,TRUE,ENDSESSION_LOGOFF)>0);
            const auto elapsed=GetTickCount64()-start;
            PHPVPN_CHECK(elapsed<250); PHPVPN_CHECK_EQ(broadcasts.end,2u);
            PHPVPN_CHECK(!app->window()&&!IsWindow(contracts)&&!IsWindow(editor)&&!IsWindow(wizard)&&!IsWindow(progress));
            PHPVPN_CHECK_EQ(backend.saves.load(),sends); PHPVPN_CHECK_EQ(backend.disconnects.load(),disconnects);
            app.reset(); backend.release(); backend.server.stop();
            const auto end=GetTickCount64()+100; while(GetTickCount64()<end)pump();
            PHPVPN_CHECK(!top(L"#32770")); PHPVPN_CHECK(!top(L"PhpVpnContracts")); PHPVPN_CHECK_EQ(backend.saves.load(),sends); PHPVPN_CHECK_EQ(backend.disconnects.load(),disconnects);
            std::cout<<"[ EVIDENCE ] isolated desktop broadcast: query="<<broadcasts.query<<", end="<<broadcasts.end<<", cleanup="<<elapsed<<"ms; late dialogs=0, retry=0, Disconnect=0\n";
        } catch(...) { error=std::current_exception(); }
    }); worker.join(); CloseDesktop(desktop); if(error)std::rethrow_exception(error);
}
PHPVPN_TEST(WP05Windows, SmallCidrResizeTabOrderAndVisualCapture) {
    Backend backend; ContractsWindow window(backend.pipe); require(window.open(GetModuleHandleW(nullptr),nullptr,contracts_test::document("192.0.2.2","192.0.2.0/29"),"synthetic","one"));
    const auto root=window.window();
    unsigned count=0;EnumChildWindows(root,[](HWND h,LPARAM p)->BOOL{if(GetDlgCtrlID(h)>=1000)++*reinterpret_cast<unsigned*>(p);return TRUE;},reinterpret_cast<LPARAM>(&count)); PHPVPN_CHECK_EQ(count,12u);
    PHPVPN_CHECK(!IsWindowEnabled(field(root,IDOK))); PHPVPN_CHECK(IsWindowEnabled(field(root,IDCANCEL)));
    click(root,1000); PHPVPN_CHECK(!IsWindowEnabled(field(root,IDOK))); PHPVPN_CHECK_EQ(text(field(root,IDOK)),L"Save (1 change)");
    capture(root,L"contracts-validation-footer.bmp");
    SendMessageW(root,WM_COMMAND,MAKEWPARAM(802,STN_CLICKED),reinterpret_cast<LPARAM>(field(root,802)));
    PHPVPN_CHECK_EQ(GetFocus(),field(root,1001));
    dialogs::answer(IDNO,[&]{click(root,IDCANCEL);}); PHPVPN_CHECK(window.isOpen());
    click(root,1000); PHPVPN_CHECK(!IsWindowEnabled(field(root,IDOK))); PHPVPN_CHECK_EQ(text(field(root,IDOK)),L"Save");
    const auto key=field(root,1001); const auto originalKey=text(key); PHPVPN_CHECK(originalKey.empty());
    PHPVPN_CHECK((GetWindowLongPtrW(key,GWL_STYLE)&ES_READONLY)!=0); PHPVPN_CHECK(!IsWindowEnabled(field(root,1002)));
    SetFocus(key); PHPVPN_CHECK_EQ(GetFocus(),key); replace(key,L"blocked"); SendMessageW(key,WM_PASTE,0,0);
    PHPVPN_CHECK_EQ(text(key),originalKey); PHPVPN_CHECK(!window.lifecycleSnapshot().dirty);
    capture(root,L"contracts-29.bmp");
    const auto first=field(root,1000); HWND cursor=first;
    for(int id=1001;id<1015;++id){if((id-1000)%4>=2)continue;cursor=GetNextDlgTabItem(root,cursor,FALSE);PHPVPN_CHECK_EQ(cursor,field(root,id));}
    PHPVPN_CHECK_EQ(GetNextDlgTabItem(root,cursor,FALSE),field(root,IDCANCEL));
    PHPVPN_CHECK_EQ(GetNextDlgTabItem(root,field(root,IDCANCEL),TRUE),cursor);
    click(root,1000); PHPVPN_CHECK((GetWindowLongPtrW(key,GWL_STYLE)&ES_READONLY)==0);
    click(root,1002);const auto password=top(L"PhpVpnPasswordSha");require(password!=nullptr);capture(password,L"password-empty.bmp");
    replace(field(password,501),L"hello");capture(password,L"password-hello.bmp");resizeClient(password,480,320);SetFocus(field(password,502));
    const auto draftHash=text(field(password,502));
    click(root,1000); PHPVPN_CHECK(!IsWindowEnabled(field(password,IDOK)));
    SendMessageW(password,WM_COMMAND,IDOK,0); PHPVPN_CHECK(IsWindow(password)); PHPVPN_CHECK_EQ(text(key),originalKey);
    PHPVPN_CHECK_EQ(text(field(password,502)),draftHash);
    click(root,1000); PHPVPN_CHECK(IsWindowEnabled(field(password,IDOK)));
    click(password,IDOK); PHPVPN_CHECK_EQ(text(key),draftHash);
    click(root,1000); PHPVPN_CHECK_EQ(text(key),draftHash); PHPVPN_CHECK(!window.lifecycleSnapshot().dirty);
    PHPVPN_CHECK_EQ(text(field(root,IDOK)),L"Save"); PHPVPN_CHECK(!IsWindowEnabled(field(root,IDOK)));
    click(root,1000); replace(key,originalKey);
    click(root,1002); const auto resumed=top(L"PhpVpnPasswordSha"); require(resumed!=nullptr); resizeClient(resumed,480,320);
    RECT client{},button{};GetClientRect(resumed,&client);GetWindowRect(field(resumed,IDOK),&button);MapWindowPoints(nullptr,resumed,reinterpret_cast<POINT*>(&button),2);PHPVPN_CHECK(button.bottom<=client.bottom);
    capture(resumed,L"password-small.bmp");SendMessageW(resumed,WM_CLOSE,0,0);PHPVPN_CHECK_EQ(GetFocus(),field(root,1002)); click(root,1000);
    resizeClient(root,480,320);SetFocus(field(root,1013));capture(root,L"contracts-small.bmp");
    RECT viewport{},focused{};const auto table=GetParent(GetParent(field(root,1013)));
    GetClientRect(table,&viewport);GetWindowRect(field(root,1013),&focused);MapWindowPoints(nullptr,table,reinterpret_cast<POINT*>(&focused),2);
    PHPVPN_CHECK(focused.top>=0 && focused.bottom<=viewport.bottom && focused.left>=0 && focused.right<=viewport.right);
    GetClientRect(root,&client);GetWindowRect(field(root,IDOK),&button);MapWindowPoints(nullptr,root,reinterpret_cast<POINT*>(&button),2);PHPVPN_CHECK(button.bottom<=client.bottom && button.right<=client.right);
    resizeClient(root,320,213);SetFocus(field(root,1012));SetFocus(field(root,1013));capture(root,L"contracts-tiny.bmp");
    const auto body=GetParent(table);GetClientRect(body,&viewport);GetWindowRect(field(root,1013),&focused);MapWindowPoints(nullptr,body,reinterpret_cast<POINT*>(&focused),2);
    PHPVPN_CHECK(focused.top>=0 && focused.bottom<=viewport.bottom);
    click(root,IDCANCEL); PHPVPN_CHECK(!window.isOpen());
    window.endSession();
    require(!window.open(GetModuleHandleW(nullptr),nullptr,backend.stored,"synthetic","terminal"));
}
PHPVPN_TEST(WP05Windows, RandomFailureAndPartialCreateFailureRetainSafeState) {
    Backend backend;contracts_test::Crypto* crypto=nullptr;
    ContractsWindow window(backend.pipe,[&]() -> Result<std::unique_ptr<CryptoProvider>> {auto value=std::make_unique<contracts_test::Crypto>();crypto=value.get();return std::unique_ptr<CryptoProvider>(std::move(value));},createWithFailure);
    for(unsigned fail:{1u,9u,20u,900u}) {
        createCalls=0;failCreateAt=fail;
        PHPVPN_CHECK(!window.open(GetModuleHandleW(nullptr),nullptr,backend.stored,"synthetic","failure"));
        PHPVPN_CHECK(!window.lifecycleSnapshot().open);PHPVPN_CHECK(!top(L"PhpVpnContracts"));
    }
    failCreateAt=0;require(window.open(GetModuleHandleW(nullptr),nullptr,backend.stored,"synthetic","retry"));
    const auto root=window.window(),key=field(root,1001);const auto original=text(key);const auto snapshot=window.lifecycleSnapshot();
    crypto->failRandom=true;dialogs::answer(IDYES,[&]{click(root,1002);});
    PHPVPN_CHECK_EQ(text(key),original);PHPVPN_CHECK_EQ(window.lifecycleSnapshot(),snapshot);PHPVPN_CHECK_EQ(crypto->randomSize,32u);
    PHPVPN_CHECK_EQ(text(field(root,802)),L"A random key could not be generated. The current key has been kept.");
    window.endSession();PHPVPN_CHECK_EQ(backend.saves.load(),0u);
}
PHPVPN_TEST(WP05Windows, KeyInputLimitAndIndependentEditorsDoNotRebase) {
    Backend backend;ContractsWindow first(backend.pipe),second(backend.pipe);
    require(first.open(GetModuleHandleW(nullptr),nullptr,backend.stored,"synthetic","same"));require(second.open(GetModuleHandleW(nullptr),nullptr,backend.stored,"synthetic","same"));
    const auto key=field(first.window(),1001),next=field(first.window(),1005);
    PHPVPN_CHECK_EQ(SendMessageW(key,EM_GETLIMITTEXT,0,0),128);
    PHPVPN_CHECK_EQ(SendMessageW(next,EM_GETLIMITTEXT,0,0),128);
    replace(key,std::wstring(128,L'a'));
    PHPVPN_CHECK_EQ(text(key).size(),128u);
    const auto old=text(key);SendMessageW(key,EM_SETSEL,3,3);SendMessageW(key,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"x"));
    DWORD begin=0,end=0;SendMessageW(key,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));
    PHPVPN_CHECK_EQ(text(key),old);PHPVPN_CHECK_EQ(begin,3u);PHPVPN_CHECK_EQ(end,3u);
    replace(key,std::wstring(129,L'b'));PHPVPN_CHECK_EQ(text(key),old);
    replace(key,std::wstring(64,L'a'));click(first.window(),1000);
    replace(field(second.window(),1005),std::wstring(64,L'b'));click(second.window(),1004);
    click(first.window(),IDOK);require(until([&]{return !first.isOpen();}));
    click(second.window(),IDOK);require(until([&]{return !second.isOpen();}));
    auto result=ContractsDocument::parse(backend.stored);require(result.ok());
    PHPVPN_CHECK(!result.value()->existingKey(contracts_test::ip("10.99.99.3")));
    PHPVPN_CHECK_EQ(result.value()->existingKey(contracts_test::ip("10.99.99.4")).value(),std::string(64,'b'));
    PHPVPN_CHECK_EQ(backend.saves.load(),2u);PHPVPN_CHECK_EQ(backend.documents.load(),0u);
}

// Included only by the opt-in UI harness, inside its private namespace.
// Reuse Backend's Wizard protocol and real native controls. No fixture code is
// compiled into the service, CLI or production tray.
struct DevSelection { std::string view = "Contracts", scene = "Default"; };
const std::map<std::string, std::vector<std::string>> devScenes{
    {"Tray", {"Default"}}, {"Connect", {"Default", "Connecting", "Connected", "Failed"}},
    {"Editor", {"Default", "Invalid"}}, {"Statistics", {"Default", "Unavailable"}},
    {"Wizard", {"Default", "Gate", "Url", "Secret", "Identity", "Contracts"}},
    {"Contracts", {"Default", "Invalid", "Saving", "SaveUnknown"}}, {"Password", {"Default"}}
};
bool validDevSelection(const DevSelection& value) {
    const auto found = devScenes.find(value.view);
    return found != devScenes.end() && std::find(found->second.begin(), found->second.end(), value.scene) != found->second.end();
}
DevSelection parseDevSelection(int argc, char** argv) {
    check(argc == 5 && std::string_view(argv[1]) == "--dev-preview" && std::string_view(argv[3]) == "--scene",
        "Usage: --dev-preview <View> --scene <Scene>");
    DevSelection value{argv[2], argv[4]};
    check(validDevSelection(value), "Unknown View/Scene combination");
    return value;
}
std::string devDocument() {
    auto dom = Json::parse(makeDefaultEditorDocument()).value();
    for (auto& [name, value] : dom.objectValue()) if (name == "masterSetup")
        for (auto& [key, item] : value.objectValue()) {
            if (key == "serverUrl") item = str("https://gui-preview.invalid/phpvpn.php");
            if (key == "virtualIp") item = str("10.99.99.2");
            if (key == "vpnCidr") item = str("10.99.99.0/24");
            if (key == "autoDetectIpAddressInWizard") item = J::makeBool(false);
        }
    std::erase_if(dom.objectValue(), [](const auto& member) { return member.first == "communicationContracts"; });
    return Json::serialize(dom);
}
struct DevBackend : Backend {
    DevSelection selection;
    std::string document = devDocument();
    StatusSnapshot status;
    std::optional<WizardDocument> wizardDocument;
    unsigned documents = 0, states = 0, statuses = 0, saves = 0, infos = 0, unexpected = 0;
    bool unavailable = false;
    std::uint64_t saveDeadline = 0;
    explicit DevBackend(DevSelection value) : Backend(true), selection(std::move(value)) {
        const auto parsed = analyzeWizardDocument(document);
        check(parsed.ok(), "Invalid synthetic document");
        view.fields = parsed.value().fields; view.context = parsed.value().context;
        view.contractCount = 0; view.existingDefaultContractPeers.clear();
        status.stateChangedAtUtc = "2026-09-08T12:00:00.000Z"; status.clientVersion = "gui-preview-synthetic";
        status.counters = {9007199254740993ULL, 123456789, 9007199131284204ULL,
            9876543210987654ULL, 12345678901234567ULL, 543210987654321ULL,
            765432109876543ULL, 12345678909876543ULL, 18446744073709551615ULL};
        if (selection.view == "Wizard" && selection.scene == "Gate") {
            status.state = view.vpnState = ConnectionState::connected;
            status.phase = view.vpnPhase = ConnectionPhase::running;
        }
        if (selection.view == "Editor" && selection.scene == "Invalid") document = "{ invalid JSON, \xc4\x8d synthetic text }";
    }
    unsigned countDev(unsigned DevBackend::*member) { std::lock_guard lock(mutex); return this->*member; }
    void loseStatus() { std::lock_guard lock(mutex); unavailable = true; }
    std::string reply(std::string_view bytes) {
        if (auto request = IpcProtocol::parseRequest(bytes)) {
            std::unique_lock lock(mutex);
            switch (request->command) {
            case IpcCommand::get_info: ++infos; return "{\"syntheticGuiPreview\":true}";
            case IpcCommand::get_config: return document;
            case IpcCommand::set_config:
                { ConfigWarnings warnings;
                  const auto valid = validateConfigValues(request->config, warnings, false);
                  if (!valid) return IpcProtocol::buildError(request->requestId, valid.error()); }
                ++saves; document = request->config;
                if (selection.scene == "Saving" && saves == 1) {
                    changed.wait(lock, [&] { return stopping || saveDeadline; });
                    while (!stopping) {
                        const auto now = GetTickCount64(); if (now >= saveDeadline) break;
                        changed.wait_for(lock, std::chrono::milliseconds(saveDeadline - now));
                    }
                    return {}; // the original production deadline, never renewed
                }
                if (selection.scene == "SaveUnknown" && saves == 1) {
                    // Publication was possible, then another synthetic writer
                    // changed the whole document. Check can offer explicit repeat.
                    auto dom = Json::parse(document).value();
                    for (auto& [name, value] : dom.objectValue()) if (name == "masterSetup")
                        for (auto& [key, item] : value.objectValue()) if (key == "serverSecret") item = str("synthetic-other-writer");
                    document = Json::serialize(dom);
                    return {}; // lost acknowledgement, never a guessed rejection
                }
                return IpcProtocol::buildAcknowledgement(request->requestId);
            case IpcCommand::connect:
                ++connects;
                status.state = ConnectionState::connecting; status.phase = ConnectionPhase::health;
                if (selection.scene == "Connected") {
                    status.state = ConnectionState::connected; status.phase = ConnectionPhase::running;
                    status.serverUrlCheck = status.serverSecretCheck = status.accessKeyCheck = phpvpn::CheckpointResult::success;
                    status.serverReachable = status.serverVerified = status.tunnelReady = Tristate::yes;
                } else if (selection.scene == "Failed") {
                    status.state = ConnectionState::error; status.serverUrlCheck = phpvpn::CheckpointResult::failed;
                    status.lastError = SafeError{"endpoint_unavailable_or_invalid", "The endpoint is unavailable or invalid.", true};
                }
                break;
            case IpcCommand::disconnect:
                ++disconnects; ++epoch;
                status.state = ConnectionState::disconnected; status.phase = ConnectionPhase::idle;
                status.lastError.reset(); status.tunnelReady = Tristate::no;
                break;
            case IpcCommand::get_status: ++statuses; if (unavailable) return {}; break;
            default: ++unexpected; return {};
            }
            if (status.state == ConnectionState::connected && !status.connectionStartedAtUtc)
                status.connectionStartedAtUtc = status.stateChangedAtUtc;
            if (status.state == ConnectionState::connecting || status.state == ConnectionState::disconnected)
                status.connectionStartedAtUtc.reset();
            return IpcProtocol::buildStatusResponse(request->requestId, status);
        }
        if (auto request = WizardIpc::parseRequest(bytes)) {
            std::lock_guard lock(mutex);
            if (request->command == WizardCommand::open) {
                const auto original = analyzeWizardDocument(document);
                if (!original) return WizardIpc::buildError(request->requestId, WizardIpc::safeError(original.error()));
                wizardDocument = original.value();
                view.fields = wizardDocument->fields; view.context = wizardDocument->context;
                view.contractCount = static_cast<unsigned>(wizardDocument->contractPeers.size());
                view.existingDefaultContractPeers = wizardDocument->contractPeers;
                view.vpnState = status.state; view.vpnPhase = status.phase; view.connectionEpoch = epoch;
                view.mode = WizardMode::edit;
            } else if (request->command == WizardCommand::reset) wizardDocument = makeNewWizardDocument();
            else if (request->command == WizardCommand::finish) {
                check(wizardDocument.has_value(), "Synthetic Finish without open");
                auto crypto = createPlatformCryptoProvider(); check(crypto.ok(), "Synthetic crypto unavailable");
                auto candidate = buildWizardDocument(*crypto.value(), *wizardDocument, view.mode, request->fields, request->createDefaultContracts);
                if (!candidate) return WizardIpc::buildError(request->requestId, WizardIpc::safeError(candidate.error()));
                document = std::move(candidate).value();
            }
        }
        if (auto request = WizardIpc::parseRequest(bytes); request &&
            (request->command == WizardCommand::get_config_document || request->command == WizardCommand::get_config_state)) {
            std::lock_guard lock(mutex);
            const bool invalid = !Json::parse(document);
            auto members = JsonObject{{"configPath", str("Z:\\synthetic-gui-preview\\config.json")},
                {"availability", str(invalid ? "invalid" : "valid")},
                {"reasonCode", invalid ? str("config_invalid_json") : J::makeNull()},
                {"revisionToken", str("synthetic-preview-revision")}};
            if (request->command == WizardCommand::get_config_document) { ++documents; members.emplace_back("document", str(document)); }
            else {
                ++states; members.emplace_back("serviceInstanceId", str(view.serviceInstanceId));
                members.emplace_back("connectionEpoch", J::makeUnsignedInteger(epoch));
                members.emplace_back("vpnState", str(connectionStateName(status.state)));
                members.emplace_back("vpnPhase", str(connectionPhaseName(status.phase)));
            }
            return WizardIpc::buildResult(request->requestId, J::makeObject(std::move(members)));
        }
        // All Wizard stages and Finish/Reset/heartbeat actions share the legacy
        // deterministic backend, not a second implementation or fake frontend.
        return Backend::handle(bytes);
    }
};
class DevShell final : public ShellAdapter {
public:
    explicit DevShell(bool visible) : visible_(visible) {}
    bool notify(DWORD op, NOTIFYICONDATAW& data) override {
        check(op == NIM_DELETE || std::wstring_view(data.szTip) == L"PHPVPN GUI preview - synthetic data", "Incorrect preview tooltip");
        if (op == NIM_ADD) ++adds;
        if (op == NIM_DELETE) ++deletes;
        return !visible_ || real_.notify(op, data);
    }
    std::optional<Point> menuAnchor(HWND window) override { return visible_ ? real_.menuAnchor(window) : std::nullopt; }
    unsigned adds = 0, deletes = 0;
private:
    bool visible_; Win32ShellAdapter real_;
};
struct DevPreview {
    DevSelection selection;
    DevBackend backend;
    const std::wstring suffix = widen(uuid());
    const std::wstring pipe = L"\\\\.\\pipe\\phpvpn-gui-preview-" + suffix;
    const std::wstring mutex = L"Local\\phpvpn-gui-preview-" + suffix;
    IpcPipeServer server;
    DevShell* shell = nullptr;
    std::unique_ptr<TrayApplication> app;
    UINT_PTR timer = 0;
    ULONGLONG started = 0, readyAt = 0;
    unsigned stage = 0;
    bool ready = false, inTick = false;
    std::string error;
    std::function<void(DevPreview&)> automation;
    inline static thread_local DevPreview* active = nullptr;
    explicit DevPreview(DevSelection value) : selection(std::move(value)), backend(selection),
        server([this](std::string_view bytes) { return backend.reply(bytes); }, options()) {
        check(validDevSelection(selection), "Invalid preview selection");
        check(server.start().ok(), "Private preview backend could not start");
        auto adapter = std::make_unique<DevShell>(selection.view == "Tray"); shell = adapter.get();
        app = std::make_unique<TrayApplication>(TrayRuntime{pipe, mutex, std::move(adapter), L"PHPVPN GUI preview - synthetic data"});
        check(app->initialise(GetModuleHandleW(nullptr)), "Preview application initialisation failed");
        check(IsWindow(app->window()), "Preview mutex collision");
    }
    ~DevPreview() {
        if (timer) KillTimer(nullptr, timer);
        active = nullptr;
        if (app) app->endSession();
        backend.release(); server.stop(); app.reset();
        MSG message{}; while (PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    }
    IpcPipeServerOptions options() {
        check(!pipe.empty(), "Private pipe missing");
        IpcPipeServerOptions value; value.pipeName = pipe;
        value.cancelHandlers = [this] { backend.release(); };
        value.classify = [](std::string_view bytes) {
            if (auto request = IpcProtocol::parseRequest(bytes); request && request->command == IpcCommand::set_config) return IpcWorkClass::file;
            return IpcWorkClass::shortControl;
        };
        return value;
    }
    HWND window() const {
        if (selection.view == "Tray") return app->window();
        if (selection.view == "Connect") return findTop(L"PhpVpnConnectingProgress");
        if (selection.view == "Editor") return findTop(L"PhpVpnTrayEditor");
        if (selection.view == "Statistics") return findTop(L"PhpVpnStatistics");
        if (selection.view == "Wizard") return findTop(wizardClass);
        return findTop(L"PhpVpnContracts"); // Password lives until its parent closes
    }
    void action(int id) { SendMessageW(app->window(), WM_COMMAND, id, 0); }
    static void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) {
        if (!active || active->inTick) return;
        auto& self = *active; self.inTick = true;
        try { self.advance(); }
        catch (const std::exception& error) { self.error = error.what(); self.app->endSession(); }
        self.inTick = false;
    }
    void markReady() {
        ready = true; readyAt = GetTickCount64();
        std::cout << "DEV PREVIEW READY " << selection.view << '/' << selection.scene << ", PID " << GetCurrentProcessId()
            << ", private UUID pipe/mutex, synthetic memory only\n" << std::flush;
    }
    void advance() {
        if (uiDiagnostics.enabled) uiDiagnostics.add("preview:stage/ready/window", stage, ready, reinterpret_cast<UINT_PTR>(window()));
        if (ready) {
            if (automation) automation(*this);
            if (selection.view != "Tray" && !window()) app->endSession();
            return;
        }
        check(GetTickCount64() - started < 12000, "Preview scene did not become ready");
        if (!stage) {
            // Let the initial real status reply reach the application first.
            if (!backend.countDev(&DevBackend::statuses)) return;
            ++stage;
            if (selection.view == "Tray") { markReady(); return; }
            if (selection.view == "Connect") action(IDM_TOGGLE);
            else if (selection.view == "Editor") action(IDM_EDIT_CONFIG);
            else if (selection.view == "Statistics") action(IDM_STATISTICS);
            else if (selection.view == "Wizard") action(206);
            else action(207);
            return;
        }
        HWND root = window(); if (!root) return;
        // A hidden launcher sets STARTUPINFO. Explicit SW_SHOW must be applied
        // after the production open path, before any screenshot or ready signal.
        if (!IsWindowVisible(root)) ShowWindow(root, SW_SHOW);
        if (selection.view == "Wizard") {
            if (selection.scene == "Gate") { if (text(child(root, nextId)) == wizardText::disconnect) markReady(); return; }
            const unsigned destination = selection.scene == "Secret" ? 2 : selection.scene == "Identity" ? 3 : selection.scene == "Contracts" ? 4 : 1;
            const int fields[]{0, urlId, secretId, ipId, contractsId};
            if (!child(root, fields[stage]) || !IsWindowEnabled(child(root, nextId))) return;
            if (stage == destination) { markReady(); return; }
            ++stage; click(root, nextId); return;
        }
        if (selection.view == "Contracts" || selection.view == "Password") {
            if (!child(root, 1001)) return;
            if (stage == 1) {
                check(backend.countDev(&DevBackend::documents) == 1 && backend.countDev(&DevBackend::states) == 0, "Contracts must use one document read");
                if (selection.view == "Password") { click(root, 1000); click(root, 1002); ShowWindow(findTop(L"PhpVpnPasswordSha"), SW_SHOW); }
                else if (selection.scene == "Invalid") {
                    click(root, 1000); edit(root, 1001, L"invalid key");
                } else if (selection.scene == "Saving" || selection.scene == "SaveUnknown") {
                    click(root, 1000);
                    edit(root, 1001, std::wstring(64, L'a'));
                    click(root, IDOK);
                    { std::lock_guard lock(backend.mutex); backend.saveDeadline = app->contractsLifecycleSnapshot().deadlineMs; }
                    backend.changed.notify_all();
                }
                ++stage;
            }
            if (selection.scene == "Saving" && !app->contractsLifecycleSnapshot().saving) return;
            if (selection.scene == "SaveUnknown" && !app->contractsLifecycleSnapshot().unknown) return;
        }
        if (selection.view == "Statistics") {
            if (text(child(root, 500)) != L"9,007,199,254,740,993") return;
            if (selection.scene == "Unavailable") {
                if (stage == 1) { backend.loseStatus(); ++stage; }
                if (allText(root).find(L"unavailable") == std::wstring::npos && allText(root).find(L"Unavailable") == std::wstring::npos) return;
            }
        }
        markReady();
    }
    int run(std::function<void(DevPreview&)> callback = {}) {
        automation = std::move(callback); active = this; started = GetTickCount64();
        timer = SetTimer(nullptr, 0, 20, tick); check(timer != 0, "Preview timer could not start");
        const auto result = app->run();
        KillTimer(nullptr, timer); timer = 0; active = nullptr;
        if (!error.empty()) throw std::runtime_error(error);
        return result;
    }
};
int devPreview(int argc, char** argv) {
    try { DevPreview preview(parseDevSelection(argc, argv)); return preview.run(); }
    catch (const std::exception& error) { std::cerr << "Preview failed: " << error.what() << '\n'; return 1; }
}
void devSceneMatrix() {
    for (const auto& [view, scenes] : devScenes) for (const auto& scene : scenes) {
        std::array<std::string, 5> args{"phpvpn-tray-ui-tests", "--dev-preview", view, "--scene", scene};
        char* argv[]{args[0].data(), args[1].data(), args[2].data(), args[3].data(), args[4].data()};
        DevPreview preview(parseDevSelection(5, argv));
        preview.run([](DevPreview& p) {
            check(p.window() && (p.selection.view == "Tray" || IsWindowVisible(p.window())), "Preview hidden or missing");
            if (p.selection.view == "Password") check(findTop(L"PhpVpnPasswordSha") && IsWindowEnabled(p.window()), "Password child blocked parent");
            if (p.selection.scene == "Invalid" && p.selection.view == "Contracts") check(!IsWindowEnabled(child(p.window(), IDOK)), "Invalid enabled Key did not block Save");
            check(p.backend.countDev(&DevBackend::unexpected) == 0, "Unexpected synthetic request");
            p.app->endSession();
        });
        check(preview.ready, "Scene never became ready");
        check(preview.shell->adds == 1 && preview.shell->deletes == 1, "Shell lifecycle mismatch");
        std::cout << "[ SCENE OK ] " << view << '/' << scene << '\n';
    }
}
void devLongPreview() {
    DevPreview preview({"Contracts", "Default"});
    preview.run([](DevPreview& p) {
        if (GetTickCount64() - p.readyAt < 46000) return;
        check(IsWindowVisible(p.window()), "Preview did not survive 45 seconds");
        SendMessageW(p.window(), WM_CLOSE, 0, 0);
    });
    check(GetTickCount64() - preview.readyAt >= 46000 && !preview.window(), "Long preview close failed");
}
void devConnectedCountdown() {
    DevPreview preview({"Connect", "Connected"});
    preview.run([](DevPreview& p) {
        check(GetTickCount64() - p.readyAt < 4000, "Connected preview did not close at real deadline");
    });
    const auto elapsed = GetTickCount64() - preview.readyAt;
    check(preview.ready && elapsed >= 2900 && elapsed < 4000, "Real Connect countdown was bypassed");
    check(preview.backend.count(&Backend::disconnects) == 0, "Preview close disconnected");
    std::cout << "[ EVIDENCE ] Connect success after READY: " << elapsed << " ms\n";
}
void devSaveUnknownRepeat() {
    DevPreview preview({"Contracts", "SaveUnknown"});
    unsigned step = 0;
    preview.run([&](DevPreview& p) {
        check(GetTickCount64() - p.readyAt < 6000, "Unknown workflow timed out");
        if (!p.window()) return;
        if (!step) {
            check(p.backend.countDev(&DevBackend::saves) == 1, "Unknown retried automatically");
            check(!IsWindowVisible(child(p.window(), 801)), "Repeat offered before Check");
            ++step; click(p.window(), 800);
        } else if (step == 1 && IsWindowVisible(child(p.window(), 801))) {
            check(p.backend.countDev(&DevBackend::documents) == 2, "Check did not issue one fresh read");
            phpvpn::tray::test::answer(IDNO, [&] { click(p.window(), 801); });
            check(p.backend.countDev(&DevBackend::saves) == 1, "Repeat No wrote configuration");
            ++step; phpvpn::tray::test::answer(IDYES, [&] { click(p.window(), 801); });
        }
    });
    check(step == 2 && preview.backend.countDev(&DevBackend::saves) == 2, "Confirmed repeat did not save and close");
}
void devPasswordChildren() {
    DevPreview preview({"Password", "Default"});
    unsigned step = 0;
    preview.run([&](DevPreview& p) {
        check(GetTickCount64() - p.readyAt < 6000, "Password workflow timed out");
        if (step++) return;
        const HWND first = findTop(L"PhpVpnPasswordSha");
        click(p.window(), 1002); const HWND second = findTop(L"PhpVpnPasswordSha");
        check(first && second && first != second && IsWindowEnabled(p.window()), "Multiple Password children unavailable");
        edit(first, 501, L"first synthetic password");
        edit(second, 501, L"second synthetic password");
        click(first, IDOK);
        check(IsWindow(second) && IsWindowEnabled(p.window()), "Child OK closed its sibling or disabled parent");
        click(p.window(), IDOK);
        check(!IsWindow(first) && !IsWindow(second), "Parent OK retained a Password child");
    });
    check(preview.backend.countDev(&DevBackend::saves) == 1, "Parent OK failed to save");
}
void devSavingDeadline() {
    DevPreview preview({"Contracts", "Saving"});
    std::uint64_t deadline = 0;
    preview.run([&](DevPreview& p) {
        if (!p.window()) return;
        const auto current = p.app->contractsLifecycleSnapshot();
        if (!deadline) deadline = current.deadlineMs;
        if (current.saving) check(deadline == current.deadlineMs, "Saving deadline was renewed");
        check(GetTickCount64() < deadline + 2000, "Saving stayed disabled beyond its deadline");
        if (current.unknown) {
            check(!current.saving && GetTickCount64() >= deadline, "Saving did not wait for original deadline");
            phpvpn::tray::test::answer(IDYES, [&] { SendMessageW(p.window(), WM_CLOSE, 0, 0); });
        }
    });
    check(deadline && preview.backend.countDev(&DevBackend::saves) == 1, "Saving deadline retried publication");
}
void devEndpointIsolation() {
    std::wstring oldPipe, oldMutex;
    {
        DevPreview first({"Contracts", "Default"});
        DevPreview second({"Contracts", "Default"});
        check(first.pipe != second.pipe && first.mutex != second.mutex && first.app->window() != second.app->window(), "Preview instances collided");
        HANDLE ownMutex = OpenMutexW(SYNCHRONIZE, FALSE, first.mutex.c_str());
        check(ownMutex != nullptr, "Preview did not own its private mutex"); CloseHandle(ownMutex);
        first.app->endSession(); second.app->endSession();
        first.server.stop(); second.server.stop();
        CommandReader reader(first.pipe);
        check(reader.request(IpcCommand::disconnect, {}, nullptr, 0), "Missing fixture request was not accepted");
        std::optional<std::string> response;
        const auto until = GetTickCount64() + 2000;
        while (!(response = reader.take()) && GetTickCount64() < until) Sleep(5);
        check(response && response->empty(), "Missing private fixture did not fail closed");
        check(first.backend.count(&Backend::disconnects) == 0 && second.backend.count(&Backend::disconnects) == 0, "Disconnected another fixture");
        oldPipe = first.pipe; oldMutex = first.mutex;
    }
    check(!WaitNamedPipeW(oldPipe.c_str(), 1), "Private pipe survived destruction");
    HANDLE orphan = OpenMutexW(SYNCHRONIZE, FALSE, oldMutex.c_str());
    if (orphan) CloseHandle(orphan);
    check(!orphan, "Private mutex survived destruction");
}
void devTrayActions() {
    DevPreview preview({"Tray", "Default"});
    unsigned step = 0;
    preview.run([&](DevPreview& p) {
        check(GetTickCount64() - p.readyAt < 8000, "Tray actions timed out");
        switch (step) {
        case 0: ++step; p.action(207); break;
        case 1:
            if (!findTop(L"PhpVpnContracts")) break;
            ++step; p.action(IDM_EDIT_CONFIG); break;
        case 2:
            if (!findTop(L"PhpVpnTrayEditor")) break;
            ++step; p.action(206); break;
        case 3:
            if (!findTop(wizardClass) || !child(findTop(wizardClass), urlId)) break;
            ++step; p.action(IDM_STATISTICS); break;
        case 4:
            if (!findTop(L"PhpVpnStatistics")) break;
            ++step; p.action(IDM_SHOW_INFO); break;
        case 5:
            if (!p.backend.countDev(&DevBackend::infos)) break;
            ++step; p.action(IDM_TOGGLE); break;
        case 6:
            if (!findTop(L"PhpVpnConnectingProgress") || p.backend.count(&Backend::connects) != 1) break;
            ++step; p.action(IDM_TOGGLE); break;
        case 7:
            if (p.backend.count(&Backend::disconnects) != 1) break;
            check(findTop(L"PhpVpnContracts") && findTop(L"PhpVpnTrayEditor") && findTop(wizardClass) &&
                findTop(L"PhpVpnStatistics"), "Tray did not preserve independent windows");
            check(p.backend.countDev(&DevBackend::documents) == 2 && p.backend.countDev(&DevBackend::states) == 1 &&
                p.backend.countDev(&DevBackend::infos) == 1 && p.backend.countDev(&DevBackend::saves) == 0, "Unexpected menu IPC counts");
            ++step; p.action(IDM_EXIT); break;
        default: break;
        }
    });
    check(step == 8 && !preview.app->window() && preview.shell->deletes == 1, "Real Exit did not finish Tray preview");
}
void devWizardPublication() {
    DevPreview preview({"Wizard", "Contracts"});
    bool pressed = false;
    answer(IDOK, [&] {
        preview.run([&](DevPreview& p) {
            check(GetTickCount64() - p.readyAt < 6000, "Synthetic Wizard Finish timed out");
            if (!pressed) { pressed = true; click(p.window(), nextId); }
        });
    });
    check(preview.backend.count(&Backend::finishCalls) == 1 && preview.backend.count(&Backend::closeCalls) == 1 &&
        preview.backend.count(&Backend::connects) == 0 && preview.backend.countDev(&DevBackend::documents) == 0,
        "Synthetic Finish changed production action sequencing");
    std::lock_guard lock(preview.backend.mutex);
    const auto saved = analyzeWizardDocument(preview.backend.document);
    // Wizard defaults remain the established .2-.15 subset, distinct from the
    // complete 252-row Contracts editor.
    check(saved && saved->contractPeers.size() == 13, "Wizard Finish did not publish its candidate in memory");
    check(!findTop(L"PhpVpnContracts"), "Wizard Finish opened Contracts");
}

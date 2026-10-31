// ServicePlatformHost of the Android client (plan-android-v001 step 4, points
// 8, 12 and 13).
//
// Storage lives in Context.getNoBackupFilesDir() (file based encryption, never
// in a backup):
//
//   <noBackupFilesDir>/phpvpn/          0700, owner getuid()
//                     config/config.json  ConfigFiles, exchange backup strategy
//                     state/ownership.json the adapter ownership journal
//                     run/                 unused (no socket on Android)
//                     logs/phpvpn-service.log, rotated 8 MiB x 5 like the desktop
//
// Every directory is 0700 and owned by the application uid; the trusted walk
// is anchored at noBackupFilesDir (the sandbox isolates everything above it).
// A storage error does not stop the runtime: IPC stays available and reports
// the configuration error, as on the desktop.
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "../service/service_runtime.h"
#include "inprocess_ipc_server.h"

namespace phpvpn {

// Internal adapter identity of the journal, never shown to the user.
inline constexpr const wchar_t* kAndroidAdapterName = L"phpvpn";
inline constexpr const wchar_t* kAndroidAdapterGuid = L"{c2e5a8d1-7b4f-4e2a-8d3c-6f1b9e0a4d27}";
// Directory below noBackupFilesDir and the mode of it and its subdirectories.
inline constexpr const char* kAndroidDataDirectoryName = "phpvpn";
inline constexpr unsigned kAndroidDirectoryMode = 0700;

struct AndroidStorageLayout {
    std::filesystem::path root;     // <noBackupFilesDir>/phpvpn
    std::filesystem::path config;   // root/config
    std::filesystem::path state;    // root/state
    std::filesystem::path runtime;  // root/run
    std::filesystem::path logs;     // root/logs
};

// The layout below `noBackupFilesDir`, which must be absolute and lexically
// normal (Context.getNoBackupFilesDir().getAbsolutePath()).
AndroidStorageLayout androidStorageLayout(const std::filesystem::path& noBackupFilesDir);

// `endpoint` receives the in-process server that createIpcServer builds, so
// the JNI entry can dispatch into it without owning the server.
ServicePlatformHost androidPlatformHost(std::filesystem::path noBackupFilesDir,
                                        std::shared_ptr<std::shared_ptr<InProcessIpcEndpoint>> endpoint);

}  // namespace phpvpn

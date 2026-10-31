// logcat output of the logger (plan-android-v001, "Nativní část").
//
// Android keeps the logger of client.md "Logování a diagnostika" unchanged:
// the rotated file (8 MiB, five history files) in
// noBackupFilesDir/phpvpn/logs, read by diagnostics, plus a copy of every
// record in logcat through the secondary sink. The logger only ever receives
// already safe text, so logcat sees exactly what the file sees, nothing more.
//
// Expected call by the host (WP04, android_host prepareStorage), after the
// log directory was secured with secureServiceDirectory:
//
//   Logger::instance().configureFile(logsDirectory, "phpvpn-service");
//   installLogcatSink();
#pragma once

#include <string_view>

#include "core/base/logger.h"

namespace phpvpn {

inline constexpr const char* kLogcatTag = "phpvpn";

// The formatted logger line without its "<ISO timestamp> [<LEVEL>] " prefix:
// logcat records its own time and the priority carries the level.
std::string_view logcatMessage(std::string_view formattedLine);

// ANDROID_LOG_VERBOSE for trace up to ANDROID_LOG_FATAL for critical.
int logcatPriority(LogLevel level) noexcept;

// Writes every record with __android_log_write and the tag "phpvpn".
LogSink makeLogcatSink();

// Logger::instance().setSecondarySink(makeLogcatSink()).
void installLogcatSink();

}  // namespace phpvpn

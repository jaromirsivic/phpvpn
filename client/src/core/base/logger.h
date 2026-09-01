// Process wide text logger with size based rotation.
//
// client.md chapter "Logovani a diagnostika" forbids writing keys, HMAC or GCM
// tags, ciphertext, packet content, request timestamps, the exact clock offset,
// whole bodies and internal paths. The logger cannot enforce that by itself, so
// every call site passes already safe text.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace phpvpn {

enum class LogLevel { trace = 0, debug, info, warning, error, critical };

const char* logLevelName(LogLevel level) noexcept;

// Sink that receives already formatted lines without the trailing newline.
using LogSink = std::function<void(LogLevel, const std::string&)>;

class Logger {
public:
    static Logger& instance();

    // The directory is created when missing. Rotation keeps `history` previous
    // files next to the active one, matching the 8 MiB and five file policy.
    void configureFile(const std::filesystem::path& directory,
                       const std::string& baseName,
                       std::uint64_t maxBytes = 8ull * 1024 * 1024,
                       int history = 5);

    void setLevel(LogLevel level);
    LogLevel level() const;

    // Additional sink used for the Windows Event Log and for tests.
    void setSecondarySink(LogSink sink);

    void write(LogLevel level, std::string_view component, std::string_view message);

    void flush();

private:
    Logger() = default;

    void rotateLocked();

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::info;
    std::filesystem::path directory_;
    std::string baseName_;
    std::uint64_t maxBytes_ = 8ull * 1024 * 1024;
    int history_ = 5;
    std::uint64_t currentBytes_ = 0;
    std::ofstream stream_;
    LogSink secondary_;
};

void logMessage(LogLevel level, std::string_view component, std::string_view message);

#define PHPVPN_LOG_TRACE(component, message) \
    ::phpvpn::logMessage(::phpvpn::LogLevel::trace, (component), (message))
#define PHPVPN_LOG_DEBUG(component, message) \
    ::phpvpn::logMessage(::phpvpn::LogLevel::debug, (component), (message))
#define PHPVPN_LOG_INFO(component, message) \
    ::phpvpn::logMessage(::phpvpn::LogLevel::info, (component), (message))
#define PHPVPN_LOG_WARN(component, message) \
    ::phpvpn::logMessage(::phpvpn::LogLevel::warning, (component), (message))
#define PHPVPN_LOG_ERROR(component, message) \
    ::phpvpn::logMessage(::phpvpn::LogLevel::error, (component), (message))
#define PHPVPN_LOG_CRITICAL(component, message) \
    ::phpvpn::logMessage(::phpvpn::LogLevel::critical, (component), (message))

}  // namespace phpvpn

#include "core/base/logger.h"

#include <chrono>
#include <system_error>

#include "core/protocol/iso8601.h"

namespace phpvpn {
namespace {

std::string activeFileName(const std::string& baseName) { return baseName + ".log"; }

std::string historyFileName(const std::string& baseName, int index) {
    return baseName + "." + std::to_string(index) + ".log";
}

}  // namespace

const char* logLevelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::trace:
            return "TRACE";
        case LogLevel::debug:
            return "DEBUG";
        case LogLevel::info:
            return "INFO";
        case LogLevel::warning:
            return "WARN";
        case LogLevel::error:
            return "ERROR";
        case LogLevel::critical:
            return "CRIT";
    }
    return "INFO";
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::configureFile(const std::filesystem::path& directory,
                           const std::string& baseName,
                           std::uint64_t maxBytes,
                           int history) {
    std::lock_guard<std::mutex> lock(mutex_);
    directory_ = directory;
    baseName_ = baseName;
    maxBytes_ = maxBytes > 0 ? maxBytes : 1;
    history_ = history >= 0 ? history : 0;

    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);

    const std::filesystem::path active = directory_ / activeFileName(baseName_);
    currentBytes_ = 0;
    const auto existing = std::filesystem::file_size(active, ec);
    if (!ec) {
        currentBytes_ = existing;
    }

    stream_.open(active, std::ios::binary | std::ios::app);
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::setSecondarySink(LogSink sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    secondary_ = std::move(sink);
}

void Logger::rotateLocked() {
    if (baseName_.empty()) {
        return;
    }
    stream_.close();

    std::error_code ec;
    if (history_ > 0) {
        const std::filesystem::path oldest = directory_ / historyFileName(baseName_, history_);
        std::filesystem::remove(oldest, ec);
        for (int index = history_ - 1; index >= 1; --index) {
            const std::filesystem::path from = directory_ / historyFileName(baseName_, index);
            const std::filesystem::path to = directory_ / historyFileName(baseName_, index + 1);
            std::filesystem::rename(from, to, ec);
        }
        std::filesystem::rename(directory_ / activeFileName(baseName_),
                                directory_ / historyFileName(baseName_, 1),
                                ec);
    } else {
        std::filesystem::remove(directory_ / activeFileName(baseName_), ec);
    }

    currentBytes_ = 0;
    stream_.open(directory_ / activeFileName(baseName_), std::ios::binary | std::ios::trunc);
}

void Logger::write(LogLevel level, std::string_view component, std::string_view message) {
    LogSink secondary;
    std::string line;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (level < level_) {
            return;
        }

        const auto now = std::chrono::system_clock::now();
        const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now.time_since_epoch())
                                 .count();

        line.reserve(component.size() + message.size() + 48);
        line += Iso8601::format(static_cast<std::int64_t>(epochMs));
        line += " [";
        line += logLevelName(level);
        line += "] ";
        line += component;
        line += ": ";
        line += message;

        if (stream_.is_open()) {
            if (currentBytes_ + line.size() + 2 > maxBytes_) {
                rotateLocked();
            }
            if (stream_.is_open()) {
                stream_ << line << "\r\n";
                stream_.flush();
                currentBytes_ += line.size() + 2;
            }
        }

        secondary = secondary_;
    }

    if (secondary) {
        secondary(level, line);
    }
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_.is_open()) {
        stream_.flush();
    }
}

void logMessage(LogLevel level, std::string_view component, std::string_view message) {
    Logger::instance().write(level, component, message);
}

}  // namespace phpvpn

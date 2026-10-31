// logcat sink of the logger, see log_sink_android.h.
#include "platform/android/log_sink_android.h"

#include <android/log.h>

#include <string>

namespace phpvpn {

std::string_view logcatMessage(std::string_view formattedLine) {
    // "2026-10-31T12:00:00.000Z [INFO] component: message"
    const std::size_t open = formattedLine.find(" [");
    if (open == std::string_view::npos || formattedLine.find(' ') != open) return formattedLine;
    const std::size_t close = formattedLine.find("] ", open + 2);
    if (close == std::string_view::npos) return formattedLine;
    return formattedLine.substr(close + 2);
}

int logcatPriority(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::trace:
            return ANDROID_LOG_VERBOSE;
        case LogLevel::debug:
            return ANDROID_LOG_DEBUG;
        case LogLevel::info:
            return ANDROID_LOG_INFO;
        case LogLevel::warning:
            return ANDROID_LOG_WARN;
        case LogLevel::error:
            return ANDROID_LOG_ERROR;
        case LogLevel::critical:
            return ANDROID_LOG_FATAL;
    }
    return ANDROID_LOG_INFO;
}

LogSink makeLogcatSink() {
    return [](LogLevel level, const std::string& line) {
        const std::string message(logcatMessage(line));
        __android_log_write(logcatPriority(level), kLogcatTag, message.c_str());
    };
}

void installLogcatSink() {
    Logger::instance().setSecondarySink(makeLogcatSink());
}

}  // namespace phpvpn

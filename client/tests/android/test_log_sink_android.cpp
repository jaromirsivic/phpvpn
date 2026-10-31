// T-N02 (plan-android-v001 WP03): the logcat sink of the logger. Android only.
#include "harness.h"

#include <android/log.h>

#include <string>

#include "platform/android/log_sink_android.h"

namespace phpvpn {

PHPVPN_TEST(AndroidLogSink, LogcatGetsTheMessageWithoutTimestampAndLevel) {
    PHPVPN_CHECK_EQ(std::string(logcatMessage("2026-10-31T12:00:00.000Z [INFO] tunnel-session: the virtual interface is ready")),
                    std::string("tunnel-session: the virtual interface is ready"));
    PHPVPN_CHECK_EQ(std::string(logcatMessage("2026-10-31T12:00:00.000Z [WARN] a: b [c] d")), std::string("a: b [c] d"));
    // Anything else passes unchanged.
    PHPVPN_CHECK_EQ(std::string(logcatMessage("plain text")), std::string("plain text"));
    PHPVPN_CHECK_EQ(std::string(logcatMessage("a b [INFO] c")), std::string("a b [INFO] c"));
    PHPVPN_CHECK_EQ(std::string(logcatMessage("")), std::string());
}

PHPVPN_TEST(AndroidLogSink, LevelsMapToLogcatPriorities) {
    PHPVPN_CHECK_EQ(logcatPriority(LogLevel::trace), static_cast<int>(ANDROID_LOG_VERBOSE));
    PHPVPN_CHECK_EQ(logcatPriority(LogLevel::debug), static_cast<int>(ANDROID_LOG_DEBUG));
    PHPVPN_CHECK_EQ(logcatPriority(LogLevel::info), static_cast<int>(ANDROID_LOG_INFO));
    PHPVPN_CHECK_EQ(logcatPriority(LogLevel::warning), static_cast<int>(ANDROID_LOG_WARN));
    PHPVPN_CHECK_EQ(logcatPriority(LogLevel::error), static_cast<int>(ANDROID_LOG_ERROR));
    PHPVPN_CHECK_EQ(logcatPriority(LogLevel::critical), static_cast<int>(ANDROID_LOG_FATAL));
}

PHPVPN_TEST(AndroidLogSink, SinkWritesThroughLiblog) {
    // The record is visible with: adb logcat -s phpvpn
    const LogSink sink = makeLogcatSink();
    sink(LogLevel::info, "2026-10-31T12:00:00.000Z [INFO] android-log-test: the logcat sink works");
    PHPVPN_CHECK(sink != nullptr);
}

}  // namespace phpvpn

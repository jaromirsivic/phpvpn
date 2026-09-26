#include "harness.h"

#include <algorithm>
#include <barrier>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <set>
#include <thread>

#include "core/base/logger.h"

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read logger fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct SinkCapture {
    std::mutex mutex;
    std::vector<std::string> lines;
    bool levelsMatch = true;
    ~SinkCapture() { phpvpn::Logger::instance().setSecondarySink({}); }
};

}  // namespace

// A separate executable gives the process-wide logger one configuration only.
PHPVPN_TEST(logger, threshold_history_concurrent_lines_and_secondary_sink) {
    namespace fs = std::filesystem;
    using namespace phpvpn;
    const auto directory = fs::temp_directory_path() /
        ("phpvpn-tt20-" + std::to_string(std::random_device{}()));
    if (!fs::create_directory(directory)) throw std::runtime_error("fixture already exists");
    // Retain this tiny fixture for inspection. Never use production ProgramData.
    std::cout << "    fixture: " << directory.string() << "\n";
    const auto active = directory / "phpvpn-service.log";
    const auto history = [&](int index) {
        return directory / ("phpvpn-service." + std::to_string(index) + ".log");
    };
    auto& logger = Logger::instance();
    SinkCapture capture;
    logger.setSecondarySink([&](LogLevel level, const std::string& line) {
        // Also prove callback invocation is outside the logger mutex.
        const auto configuredLevel = logger.level();
        std::lock_guard lock(capture.mutex);
        capture.levelsMatch &= level == LogLevel::info && configuredLevel == LogLevel::info;
        capture.lines.push_back(line);
    });
    constexpr std::uint64_t threshold = 1024;
    // Keep the production default of five histories, with a small injected threshold.
    logger.configureFile(directory, "phpvpn-service", threshold);
    logger.setLevel(LogLevel::info);
    logger.write(LogLevel::debug, "TT20", "filtered");
    PHPVPN_CHECK(capture.lines.empty());

    // 24-byte UTC timestamp + " [INFO] TT20: " + message + line ending. With the
    // Windows CRLF a record is 40 + message bytes (64, 961 and 63 below), with
    // the LF used elsewhere it is 39 + message bytes.
    const std::string eol(kLogLineEnding);
    const std::size_t overhead = 38 + eol.size();
    logger.write(LogLevel::info, "TT20", std::string(24, 'x'));
    const auto shortRecord = readFile(active);
    PHPVPN_CHECK_EQ(shortRecord.size(), overhead + 24);
    const std::size_t secondRecord = threshold + 1 - shortRecord.size();
    logger.write(LogLevel::info, "TT20", std::string(secondRecord - overhead, 'y'));
    // short + second = threshold + 1: rotate before the incoming whole record.
    PHPVPN_CHECK_EQ(readFile(history(1)), shortRecord);
    PHPVPN_CHECK_EQ(readFile(active), capture.lines.back() + eol);
    PHPVPN_CHECK_EQ(readFile(active).size(), secondRecord);
    logger.write(LogLevel::info, "TT20", std::string(threshold - secondRecord - overhead, 'z'));
    // second + third = threshold: no rotation at equality, even with a nonempty file.
    const auto exact = readFile(active);
    PHPVPN_CHECK_EQ(exact.size(), threshold);
    PHPVPN_CHECK_EQ(exact, capture.lines.at(1) + eol + capture.lines.at(2) + eol);
    PHPVPN_CHECK_EQ(readFile(history(1)), shortRecord);
    PHPVPN_CHECK(!fs::exists(history(2)));
    capture.lines.clear();

    // A message of 984 bytes with CRLF fills a whole threshold sized record.
    const std::size_t fullMessage = threshold - overhead;
    logger.write(LogLevel::info, "TT20", std::string(fullMessage, 'a'));
    const auto first = readFile(active);
    PHPVPN_CHECK_EQ(first.size(), threshold);
    PHPVPN_CHECK_EQ(readFile(history(1)), exact);
    PHPVPN_CHECK_EQ(first, capture.lines.back() + eol);

    // A 1025-byte whole record exceeds the threshold by exactly one byte.
    logger.write(LogLevel::info, "TT20", std::string(fullMessage + 1, 'b'));
    PHPVPN_CHECK_EQ(readFile(history(1)), first);
    PHPVPN_CHECK_EQ(readFile(active).size(), threshold + 1);
    PHPVPN_CHECK_EQ(readFile(active), capture.lines.back() + eol);

    // Repeated rotation evicts the oldest record and preserves newest-first order.
    for (int index = 0; index < 7; ++index) {
        logger.write(LogLevel::info, "TT20", std::string(fullMessage, static_cast<char>('c' + index)));
    }
    PHPVPN_CHECK_EQ(readFile(active), capture.lines.at(8) + eol);
    for (int index = 1; index <= 5; ++index) {
        PHPVPN_CHECK_EQ(readFile(history(index)), capture.lines.at(8 - index) + eol);
    }
    PHPVPN_CHECK(!fs::exists(history(6)));
    PHPVPN_CHECK_EQ(std::distance(fs::directory_iterator(directory), fs::directory_iterator()), 6);

    const auto previous = readFile(active);
    std::set<std::string> expectedMessages;
    for (int worker = 0; worker < 4; ++worker) {
        for (int index = 0; index < 4; ++index) {
            std::string message = "worker" + std::to_string(worker) + "-" + std::to_string(index);
            message.resize(24, '.');
            expectedMessages.insert(message);
        }
    }
    std::barrier start(4);
    std::vector<std::jthread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&, worker] {
            start.arrive_and_wait();
            for (int index = 0; index < 4; ++index) {
                std::string message = "worker" + std::to_string(worker) + "-" + std::to_string(index);
                message.resize(24, '.');
                logger.write(LogLevel::info, "TT20", message);
            }
        });
    }
    for (auto& worker : workers) worker.join();
    logger.flush();
    PHPVPN_CHECK_EQ(readFile(history(1)), previous);
    const auto concurrent = readFile(active);
    // Sixteen complete 64-byte records with CRLF, exactly the threshold.
    PHPVPN_CHECK_EQ(concurrent.size(), 16 * (overhead + 24));
    std::vector<std::string> fileLines;
    std::set<std::string> actualMessages;
    std::size_t offset = 0;
    while (offset < concurrent.size()) {
        const auto end = concurrent.find(eol, offset);
        PHPVPN_CHECK(end != std::string::npos);
        if (end == std::string::npos) break;
        const auto line = concurrent.substr(offset, end - offset);
        PHPVPN_CHECK_EQ(line.size(), 62u);
        if (line.size() >= 38) {
            PHPVPN_CHECK_EQ(line.substr(24, 14), " [INFO] TT20: ");
            actualMessages.insert(line.substr(38));
        }
        fileLines.push_back(line);
        offset = end + eol.size();
    }
    PHPVPN_CHECK_EQ(fileLines.size(), 16u);
    PHPVPN_CHECK_EQ(actualMessages, expectedMessages);
    PHPVPN_CHECK_EQ(capture.lines.size(), 25u);
    PHPVPN_CHECK(capture.levelsMatch);
    std::vector<std::string> sinkLines(capture.lines.begin() + 9, capture.lines.end());
    // Callbacks may arrive in a different order from the serialized disk writes.
    std::sort(fileLines.begin(), fileLines.end());
    std::sort(sinkLines.begin(), sinkLines.end());
    PHPVPN_CHECK_EQ(fileLines, sinkLines);
    PHPVPN_CHECK_EQ(std::distance(fs::directory_iterator(directory), fs::directory_iterator()), 6);
}

PHPVPN_TEST(logger, journald_records_carry_sd_daemon_priorities) {
    using namespace phpvpn;
    PHPVPN_CHECK_EQ(std::string(journaldPriorityPrefix(LogLevel::trace)), "<7>");
    PHPVPN_CHECK_EQ(std::string(journaldPriorityPrefix(LogLevel::debug)), "<7>");
    PHPVPN_CHECK_EQ(std::string(journaldPriorityPrefix(LogLevel::info)), "<6>");
    PHPVPN_CHECK_EQ(std::string(journaldPriorityPrefix(LogLevel::warning)), "<4>");
    PHPVPN_CHECK_EQ(std::string(journaldPriorityPrefix(LogLevel::error)), "<3>");
    PHPVPN_CHECK_EQ(std::string(journaldPriorityPrefix(LogLevel::critical)), "<2>");
    PHPVPN_CHECK_EQ(journaldLine(LogLevel::warning, "2026-01-01T00:00:00.000Z [WARN] c: m"),
                    "<4>2026-01-01T00:00:00.000Z [WARN] c: m\n");
    // An embedded line break would start a record without a priority.
    PHPVPN_CHECK_EQ(journaldLine(LogLevel::error, "a\r\nb\nc"), "<3>a  b c\n");
    PHPVPN_CHECK_EQ(journaldLine(LogLevel::info, ""), "<6>\n");
    PHPVPN_CHECK(static_cast<bool>(makeJournaldStderrSink()));
}

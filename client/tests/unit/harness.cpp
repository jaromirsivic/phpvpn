#include "harness.h"

#include <algorithm>

namespace phpvpn::test {

Registry& Registry::instance() {
    static Registry registry;
    return registry;
}

void Registry::add(TestCase testCase) { cases_.push_back(std::move(testCase)); }

void Registry::reportFailure(const std::string& file, int line, const std::string& text) {
    ++failuresInCurrentCase_;
    std::cout << "    " << file << "(" << line << "): " << text << "\n";
}

int Registry::run(const std::string& filter) {
    std::stable_sort(cases_.begin(), cases_.end(), [](const TestCase& a, const TestCase& b) {
        return a.suite < b.suite;
    });

    int executed = 0;
    int failed = 0;
    for (const TestCase& testCase : cases_) {
        const std::string fullName = testCase.suite + "." + testCase.name;
        if (!filter.empty() && fullName.find(filter) == std::string::npos) {
            continue;
        }

        ++executed;
        failuresInCurrentCase_ = 0;
        std::cout << "[ RUN  ] " << fullName << "\n";
        try {
            testCase.body();
        } catch (const std::exception& error) {
            reportFailure("<exception>", 0, std::string("unexpected exception: ") + error.what());
        } catch (...) {
            reportFailure("<exception>", 0, "unexpected unknown exception");
        }

        if (failuresInCurrentCase_ == 0) {
            std::cout << "[  OK  ] " << fullName << "\n";
        } else {
            std::cout << "[ FAIL ] " << fullName << "\n";
            ++failed;
        }
    }

    std::cout << "\n" << executed << " test(s) run, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace phpvpn::test

// A shared test library (the Android instrumented tests) calls
// Registry::run itself and has no main.
#if !defined(PHPVPN_TEST_HARNESS_NO_MAIN)
int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : std::string();
    return phpvpn::test::Registry::instance().run(filter);
}
#endif

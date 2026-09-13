// Tiny self registering test harness, plan_client.md chapter 21.
//
// Tests register themselves through a static object, so adding a file is enough
// to add its cases. CTest runs the whole binary and, with --gtest style
// filtering deliberately absent, a single failing check prints the file, the
// line and the expression.
#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace phpvpn::test {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> body;
};

class Registry {
public:
    static Registry& instance();

    void add(TestCase testCase);
    int run(const std::string& filter);

    void reportFailure(const std::string& file, int line, const std::string& text);

private:
    std::vector<TestCase> cases_;
    int failuresInCurrentCase_ = 0;
};

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> body) {
        Registry::instance().add(TestCase{suite, name, std::move(body)});
    }
};

}  // namespace phpvpn::test

#define PHPVPN_TEST(suite, name)                                                  \
    static void phpvpn_test_##suite##_##name();                                   \
    static const ::phpvpn::test::Registrar phpvpn_registrar_##suite##_##name(     \
        #suite, #name, phpvpn_test_##suite##_##name);                             \
    static void phpvpn_test_##suite##_##name()

#define PHPVPN_CHECK(expression)                                                  \
    do {                                                                          \
        if (!(expression)) {                                                      \
            ::phpvpn::test::Registry::instance().reportFailure(                    \
                __FILE__, __LINE__, "expected " #expression);                     \
        }                                                                         \
    } while (false)

// Compare within one full expression so dereferenced temporary optionals and
// Result members remain alive until the comparison completes.
#define PHPVPN_CHECK_EQ(actual, expected)                                         \
    do {                                                                          \
        if (![](const auto& a, const auto& b) { return a == b; }((actual), (expected))) { \
            std::ostringstream phpvpn_stream_;                                    \
            phpvpn_stream_ << "expected " #actual " == " #expected;               \
            ::phpvpn::test::Registry::instance().reportFailure(                    \
                __FILE__, __LINE__, phpvpn_stream_.str());                        \
        }                                                                         \
    } while (false)

#define PHPVPN_CHECK_NE(actual, expected)                                         \
    do {                                                                          \
        if ([](const auto& a, const auto& b) { return a == b; }((actual), (expected))) { \
            ::phpvpn::test::Registry::instance().reportFailure(                    \
                __FILE__, __LINE__, "expected " #actual " != " #expected);        \
        }                                                                         \
    } while (false)

#define PHPVPN_REQUIRE_OK(result)                                                 \
    do {                                                                          \
        const auto& phpvpn_result_ = (result);                                    \
        if (!phpvpn_result_.ok()) {                                               \
            ::phpvpn::test::Registry::instance().reportFailure(                    \
                __FILE__, __LINE__,                                               \
                std::string("expected success, got ") + phpvpn_result_.error().name()); \
            return;                                                               \
        }                                                                         \
    } while (false)

#define PHPVPN_REQUIRE_ERROR(result, expectedCode)                                \
    do {                                                                          \
        const auto& phpvpn_result_ = (result);                                    \
        if (phpvpn_result_.ok()) {                                                \
            ::phpvpn::test::Registry::instance().reportFailure(                    \
                __FILE__, __LINE__, "expected failure " #expectedCode);           \
        } else if (phpvpn_result_.error().code != (expectedCode)) {               \
            ::phpvpn::test::Registry::instance().reportFailure(                    \
                __FILE__, __LINE__,                                               \
                std::string("expected " #expectedCode ", got ") +                 \
                    phpvpn_result_.error().name());                               \
        }                                                                         \
    } while (false)

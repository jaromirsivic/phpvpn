#include "harness.h"

#include <cstddef>
#include <iterator>

#include "service_registration.h"

PHPVPN_TEST(service_registration, depends_only_on_tcpip) {
    constexpr wchar_t expected[] = L"Tcpip\0";
    PHPVPN_CHECK_EQ(std::size(phpvpn::kServiceDependencies), std::size(expected));

    for (std::size_t index = 0; index < std::size(expected); ++index) {
        PHPVPN_CHECK_EQ(phpvpn::kServiceDependencies[index], expected[index]);
    }
}

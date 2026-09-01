// Byte exact canonical strings and AAD blocks.
//
// Every value is separated by a single LF and no string ends with LF, exactly as
// protocol.md chapter 7 requires. The functions take the already validated text
// of each field, so the bytes that enter an HMAC are the bytes that appeared on
// the wire.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace phpvpn {

// SHA256HEX(x) as 64 lowercase hexadecimal characters.
std::string sha256Hex(std::span<const std::uint8_t> digest);

// protocol.md chapter 8.1
std::string authSemiCanonical(std::string_view timestamp, std::string_view requestId);

// protocol.md chapter 9.2, `bodySha256Hex` covers every byte after the LF that
// terminates the auth-full line.
std::string authFullCanonical(std::string_view src,
                              std::string_view timestamp,
                              std::string_view requestId,
                              std::string_view bodySha256Hex);

// protocol.md chapter 7.3
std::string healthAad(std::string_view requestId,
                      std::string_view serverVersion,
                      std::string_view status,
                      std::string_view description,
                      std::string_view timestamp,
                      std::string_view ivBase64Url);

// protocol.md chapter 8.3
std::string infoAad(std::string_view requestId,
                    std::string_view saltBase64Url,
                    std::string_view ivBase64Url);

// protocol.md chapter 9.4
std::string dataAad(std::string_view messageId,
                    std::string_view src,
                    std::string_view dst,
                    std::string_view ivBase64Url);

// protocol.md chapter 9.5. `records` holds the ordered messageId and the exact
// base64url GCM tag text of every data line of the response.
std::string dataMacCanonical(
    std::string_view requestId,
    std::span<const std::pair<std::string, std::string>> records,
    std::string_view dataResultSha256Hex);

}  // namespace phpvpn

// Strict JSON DOM and the single pass wire scanners, plan_client.md decision 10.

#include "core/protocol/json.h"

#include <string>

#include "core/protocol/json_scanner.h"
#include "harness.h"

namespace phpvpn {

PHPVPN_TEST(json, parses_the_basic_types) {
    auto document = Json::parse(R"({"a":1,"b":"two","c":true,"d":null,"e":[1,2],"f":{"g":1.5}})");
    PHPVPN_REQUIRE_OK(document);

    const JsonValue& root = document.value();
    PHPVPN_CHECK(root.isObject());
    PHPVPN_CHECK(root.find("a")->isInteger());
    PHPVPN_CHECK_EQ(root.find("a")->integerValue(), std::int64_t(1));
    PHPVPN_CHECK_EQ(root.find("b")->stringValue(), std::string("two"));
    PHPVPN_CHECK(root.find("c")->boolValue());
    PHPVPN_CHECK(root.find("d")->isNull());
    PHPVPN_CHECK_EQ(root.find("e")->arrayValue().size(), std::size_t(2));
    PHPVPN_CHECK(root.find("f")->find("g")->isNumber());
    PHPVPN_CHECK(root.find("missing") == nullptr);
}

PHPVPN_TEST(json, keeps_the_member_order_of_the_source) {
    auto document = Json::parse(R"({"z":1,"a":2,"m":3})");
    PHPVPN_REQUIRE_OK(document);
    const JsonObject& members = document.value().objectValue();
    PHPVPN_CHECK_EQ(members.size(), std::size_t(3));
    PHPVPN_CHECK_EQ(members[0].first, std::string("z"));
    PHPVPN_CHECK_EQ(members[1].first, std::string("a"));
    PHPVPN_CHECK_EQ(members[2].first, std::string("m"));
}

PHPVPN_TEST(json, rejects_duplicates_trailing_bytes_and_comments) {
    PHPVPN_CHECK(!Json::parse(R"({"a":1,"a":2})").ok());
    PHPVPN_CHECK(!Json::parse(R"({"a":1} )" "\n" R"({"b":2})").ok());
    PHPVPN_CHECK(!Json::parse(R"({"a":1}x)").ok());
    PHPVPN_CHECK(!Json::parse("// comment\n{}").ok());
    PHPVPN_CHECK(!Json::parse(R"({"a":1,})").ok());
    PHPVPN_CHECK(!Json::parse("[1,]").ok());
    PHPVPN_CHECK(!Json::parse("").ok());
}

PHPVPN_TEST(json, rejects_non_finite_numbers_and_raw_control_characters) {
    PHPVPN_CHECK(!Json::parse(R"({"a":NaN})").ok());
    PHPVPN_CHECK(!Json::parse(R"({"a":Infinity})").ok());
    PHPVPN_CHECK(!Json::parse(R"({"a":01})").ok());
    PHPVPN_CHECK(!Json::parse("{\"a\":\"line\nbreak\"}").ok());
    PHPVPN_CHECK(!Json::parse("{\"a\":\"tab\there\"}").ok());
}

PHPVPN_TEST(json, handles_escapes_and_surrogate_pairs) {
    auto document = Json::parse(R"({"a":"quote:\" backslash:\\ tab:\t unicode:\u0041"})");
    PHPVPN_REQUIRE_OK(document);
    PHPVPN_CHECK_EQ(document.value().find("a")->stringValue(),
                    std::string("quote:\" backslash:\\ tab:\t unicode:A"));

    auto pair = Json::parse(R"({"a":"\ud83d\ude00"})");
    PHPVPN_REQUIRE_OK(pair);
    PHPVPN_CHECK_EQ(pair.value().find("a")->stringValue().size(), std::size_t(4));

    // A lone surrogate is not a valid scalar value.
    PHPVPN_CHECK(!Json::parse(R"({"a":"\ud83d"})").ok());
}

PHPVPN_TEST(json, enforces_the_depth_limit) {
    std::string deep;
    for (int level = 0; level < 64; ++level) {
        deep += "[";
    }
    for (int level = 0; level < 64; ++level) {
        deep += "]";
    }
    PHPVPN_CHECK(!Json::parse(deep, 32).ok());
    PHPVPN_CHECK(Json::parse("[[[[]]]]", 32).ok());
}

PHPVPN_TEST(json, serialises_compactly_and_escapes_correctly) {
    JsonObject members;
    members.emplace_back("b", JsonValue::makeString("va\"lue"));
    members.emplace_back("a", JsonValue::makeInteger(-5));
    members.emplace_back("c", JsonValue::makeBool(false));
    members.emplace_back("d", JsonValue::makeNull());

    const std::string text = Json::serialize(JsonValue::makeObject(std::move(members)));
    PHPVPN_CHECK_EQ(text, std::string(R"({"b":"va\"lue","a":-5,"c":false,"d":null})"));
    PHPVPN_CHECK_EQ(Json::escapeString("a\tb"), std::string(R"("a\tb")"));
    PHPVPN_CHECK_EQ(Json::escapeString(std::string("\x01")), std::string(R"("\u0001")"));
}

PHPVPN_TEST(json, round_trips_through_serialisation) {
    const std::string original =
        R"({"masterSetup":{"serverUrl":"https://a/b"},"list":[1,2,3],"flag":true})";
    auto parsed = Json::parse(original);
    PHPVPN_REQUIRE_OK(parsed);
    const std::string serialised = Json::serialize(parsed.value());
    PHPVPN_CHECK_EQ(serialised, original);

    auto again = Json::parse(Json::serializePretty(parsed.value()));
    PHPVPN_REQUIRE_OK(again);
    PHPVPN_CHECK_EQ(Json::serialize(again.value()), original);
}

PHPVPN_TEST(json_scanner, accepts_a_flat_string_object_in_any_order) {
    static constexpr std::string_view kAllowed[] = {"type", "requestId"};

    auto parsed =
        JsonScanner::parseFlatStringObject(R"({"type":"auth-none","requestId":"abc"})", kAllowed);
    PHPVPN_REQUIRE_OK(parsed);
    PHPVPN_CHECK_EQ(parsed.value()[0], std::string_view("auth-none"));
    PHPVPN_CHECK_EQ(parsed.value()[1], std::string_view("abc"));

    // The flat scanner tolerates a different member order and insignificant
    // whitespace, because only the `data` record is order sensitive.
    auto swapped = JsonScanner::parseFlatStringObject(
        R"({ "requestId" : "abc" , "type" : "auth-none" })", kAllowed);
    PHPVPN_REQUIRE_OK(swapped);
    PHPVPN_CHECK_EQ(swapped.value()[0], std::string_view("auth-none"));
    PHPVPN_CHECK_EQ(swapped.value()[1], std::string_view("abc"));
}

PHPVPN_TEST(json_scanner, rejects_escapes_duplicates_unknown_and_missing_members) {
    static constexpr std::string_view kAllowed[] = {"type", "requestId"};

    // A wire message never needs an escape sequence, so the scanner refuses one
    // instead of decoding it.
    PHPVPN_CHECK(!JsonScanner::parseFlatStringObject(
                      R"({"type":"auth\u002dnone","requestId":"a"})", kAllowed)
                      .ok());
    PHPVPN_CHECK(
        !JsonScanner::parseFlatStringObject(R"({"type":"a","type":"a","requestId":"c"})", kAllowed)
             .ok());
    PHPVPN_CHECK(!JsonScanner::parseFlatStringObject(R"({"type":"a","other":"b"})", kAllowed).ok());
    PHPVPN_CHECK(!JsonScanner::parseFlatStringObject(R"({"type":"a"})", kAllowed).ok());
    PHPVPN_CHECK(
        !JsonScanner::parseFlatStringObject(R"({"type":"a","requestId":"b"}x)", kAllowed).ok());
    PHPVPN_CHECK(
        !JsonScanner::parseFlatStringObject(R"({"type":1,"requestId":"b"})", kAllowed).ok());
    PHPVPN_CHECK(!JsonScanner::parseFlatStringObject("", kAllowed).ok());
}

PHPVPN_TEST(json_scanner, enforces_the_exact_order_of_a_compact_object) {
    static constexpr std::string_view kOrdered[] = {"a", "b"};

    auto parsed = JsonScanner::parseOrderedCompactStringObject(R"({"a":"1","b":"2"})", kOrdered);
    PHPVPN_REQUIRE_OK(parsed);
    PHPVPN_CHECK_EQ(parsed.value()[0], std::string_view("1"));
    PHPVPN_CHECK_EQ(parsed.value()[1], std::string_view("2"));

    // A different order and any whitespace would change the bytes that enter the
    // canonical string, so both are refused.
    PHPVPN_CHECK(
        !JsonScanner::parseOrderedCompactStringObject(R"({"b":"2","a":"1"})", kOrdered).ok());
    PHPVPN_CHECK(
        !JsonScanner::parseOrderedCompactStringObject(R"({"a":"1", "b":"2"})", kOrdered).ok());
    PHPVPN_CHECK(
        !JsonScanner::parseOrderedCompactStringObject(R"( {"a":"1","b":"2"})", kOrdered).ok());
}

}  // namespace phpvpn

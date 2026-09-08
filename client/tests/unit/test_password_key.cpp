#include "contracts_fixture.h"

namespace phpvpn {
using namespace contracts_test;
PHPVPN_TEST(PasswordKey, independent_hash_vectors_exact_utf8_and_no_normalization) {
    Crypto crypto;
    PasswordKeyModel model;
    PHPVPN_CHECK(!model.valid());
    struct Vector { std::u16string_view input; std::string_view utf8; std::string_view hash; };
    const Vector vectors[] = {
        {u"", "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {u"abc", "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {u" aBc ", " aBc ", "af288d7200c8c3e47b6c68b0076ca80c28fc97f67c5ebbd6ec6c6b243b7dc413"},
        {u"\u00e9", "\xc3\xa9", "4a99557e4033c3539de2eb65472017cad5f9557f7a0625a09f1c3f6e2ba69c4c"},
        {u"e\u0301", "e\xcc\x81", "bf12767b0f2a56b2190075bae8169f656e3ce8d6357d4aff184bc6c7ea48f9f6"},
        {u"\U0001f600", "\xf0\x9f\x98\x80", "f0443a342c5ef54783a111b51ba56c938e474c32324d90c3a60c9c8e3a37e2d9"},
        {u"\u07ff", "\xdf\xbf", "c57c6175c9087b0c1f7f849c840c8d1ab5b0c32163ffac03fb9f1913f222516d"},
        {u"\u0800", "\xe0\xa0\x80", "a36ae1af6063cc9281d4010e979a1e3fd3d9ee3e804b7093d2b00f6ee6388bec"},
        {u"\uffff", "\xef\xbf\xbf", "2c34e62119d9c5fc3aa11640bd7945d6bf309acc6e99d8054f54ae5110e343f6"},
        {u"\U0010ffff", "\xf4\x8f\xbf\xbf", "708b8add9f6b5b556a07b72973b10ff9a3ec30e002034ef24aa076fab4b40e50"}
    };
    for (const auto& v : vectors) {
        PHPVPN_CHECK_EQ(model.update(crypto, v.input), PasswordKeyError::none);
        PHPVPN_CHECK(model.valid());
        PHPVPN_CHECK_EQ(model.key(), v.hash);
        PHPVPN_CHECK_EQ(toString(crypto.lastInput), v.utf8);
    }
    model.clear();
    PHPVPN_CHECK(!model.valid() && model.key().empty() && model.scalarCount() == 0);
}
PHPVPN_TEST(PasswordKey, ascii_and_non_bmp_254_255_256_reject_entire_edit_keep_result) {
    Crypto crypto;
    PasswordKeyModel model;
    for (bool emoji : {false, true}) {
        for (const std::size_t count : {254u, 255u, 256u}) {
            std::u16string password;
            for (std::size_t i = 0; i < count; ++i) password += emoji ? u"\U0001f600" : u"a";
            const std::string previous(model.key());
            const auto calls = crypto.hashCalls;
            const auto result = model.update(crypto, password);
            if (count == 256) {
                PHPVPN_CHECK_EQ(result, PasswordKeyError::tooLong);
                PHPVPN_CHECK_EQ(model.key(), previous);
                PHPVPN_CHECK_EQ(model.scalarCount(), 255u);
                PHPVPN_CHECK_EQ(crypto.hashCalls, calls);
                PHPVPN_CHECK(model.valid());
            } else {
                PHPVPN_CHECK_EQ(result, PasswordKeyError::none);
                PHPVPN_CHECK_EQ(model.scalarCount(), count);
                PHPVPN_CHECK_EQ(crypto.lastInput.size(), count * (emoji ? 4u : 1u));
                const auto hash = emoji ? (count == 254 ?
                    "779da693eb6cfc62fc66c6f5962c7e6b294173998686b860f2f50a99cd523a2c" :
                    "2f1e8352efc292038be0e4df7930791024f96f5400ecc81fbbba8f4f5d0521b1") : (count == 254 ?
                    "136496c2a16a22b58bbd01529b66d8510cc5a3ec61711cbaa298a52580b000d6" :
                    "b0f3323e7a3cad8ae6778340cc2a17ae0cb31c818df3767cda7c3dd423725e90");
                PHPVPN_CHECK_EQ(model.key(), hash);
            }
        }
    }
    std::u16string combining(254, u'a');
    combining += u'\u0301';
    PHPVPN_CHECK_EQ(model.update(crypto, combining), PasswordKeyError::none);
    PHPVPN_CHECK_EQ(model.scalarCount(), 255u);
    combining += u'\u0301';
    PHPVPN_CHECK_EQ(model.update(crypto, combining), PasswordKeyError::tooLong);
    PHPVPN_CHECK_EQ(model.scalarCount(), 255u);
}
PHPVPN_TEST(PasswordKey, malformed_surrogates_invalidate_and_never_hash_prefix) {
    Crypto crypto;
    PasswordKeyModel model;
    const std::u16string malformed[] = {
        {0xd800}, {0xdbff}, {0xdc00}, {0xdfff}, {u'a', 0xd800},
        {0xd800, u'a'}, {0xd800, 0xd800}, {0xdc00, 0xd800},
        {0xd800, 0xdc00, 0xdc00}, {0xd800, 0xdc00, 0xd800}
    };
    for (const auto& bad : malformed) {
        PHPVPN_CHECK_EQ(model.update(crypto, u"valid"), PasswordKeyError::none);
        const auto calls = crypto.hashCalls;
        PHPVPN_CHECK_EQ(model.update(crypto, bad), PasswordKeyError::invalidUtf16);
        PHPVPN_CHECK(!model.valid() && model.key().empty());
        PHPVPN_CHECK_EQ(crypto.hashCalls, calls);
    }
    const std::u16string lower{0xd800, 0xdc00}, upper{0xdbff, 0xdfff};
    PHPVPN_CHECK_EQ(model.update(crypto, lower), PasswordKeyError::none);
    PHPVPN_CHECK_EQ(model.scalarCount(), 1u);
    PHPVPN_CHECK_EQ(toString(crypto.lastInput), "\xf0\x90\x80\x80");
    PHPVPN_CHECK_EQ(model.update(crypto, upper), PasswordKeyError::none);
    PHPVPN_CHECK_EQ(toString(crypto.lastInput), "\xf4\x8f\xbf\xbf");
}
PHPVPN_TEST(PasswordKey, single_line_control_set_explicit_length_and_crypto_failure) {
    Crypto crypto;
    PasswordKeyModel model;
    std::vector<char16_t> controls;
    for (char16_t c = 0; c < 0x20; ++c) controls.push_back(c);
    for (char16_t c = 0x7f; c <= 0x9f; ++c) controls.push_back(c);
    controls.push_back(0x2028); controls.push_back(0x2029);
    for (const auto c : controls) {
        PHPVPN_CHECK_EQ(model.update(crypto, u"valid"), PasswordKeyError::none);
        const auto calls = crypto.hashCalls;
        const std::u16string input{u'a', c, u'b'};
        PHPVPN_CHECK_EQ(model.update(crypto, input), PasswordKeyError::unsupportedControl);
        PHPVPN_CHECK(!model.valid() && model.key().empty());
        PHPVPN_CHECK_EQ(crypto.hashCalls, calls);
    }
    // Non-control combining/BOM/space characters remain exact input bytes.
    PHPVPN_CHECK_EQ(model.update(crypto, u"\ufeff\u00a0\u0301"), PasswordKeyError::none);
    PHPVPN_CHECK_EQ(toString(crypto.lastInput), "\xef\xbb\xbf\xc2\xa0\xcc\x81");
    crypto.failHash = true;
    PHPVPN_CHECK_EQ(model.update(crypto, u"new"), PasswordKeyError::cryptoFailed);
    PHPVPN_CHECK(!model.valid() && model.key().empty());
    crypto.failHash = false;
    PHPVPN_CHECK_EQ(model.update(crypto, u""), PasswordKeyError::none);
    PHPVPN_CHECK(model.valid());
}
} // namespace phpvpn

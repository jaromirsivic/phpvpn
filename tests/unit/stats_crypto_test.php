<?php

/**
 * Known answer tests for the pure PHP crypto behind the stats action.
 *
 * The local PHP has no OpenSSL, so correctness rests on published vectors plus
 * a round trip against the independently written client in tests/lib.
 */

declare(strict_types=1);

return function (Suite $suite): void {
    loadServerInProcess();

    /** AES-256 vectors of the GCM specification, test cases 13 to 16. */
    $gcm = [
        'case 13, empty text without additional data' => [
            'key' => str_repeat('00', 32),
            'iv' => str_repeat('00', 12),
            'aad' => '',
            'plaintext' => '',
            'ciphertext' => '',
            'tag' => '530f8afbc74536b9a963b4f1c4cb738b',
        ],
        'case 14, one block without additional data' => [
            'key' => str_repeat('00', 32),
            'iv' => str_repeat('00', 12),
            'aad' => '',
            'plaintext' => str_repeat('00', 16),
            'ciphertext' => 'cea7403d4d606b6e074ec5d3baf39d18',
            'tag' => 'd0d1c8a799996bf0265b98b5d48ab919',
        ],
        'case 15, four blocks without additional data' => [
            'key' => 'feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308',
            'iv' => 'cafebabefacedbaddecaf888',
            'aad' => '',
            'plaintext' => 'd9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72'
                . '1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255',
            'ciphertext' => '522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa'
                . '8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad',
            'tag' => 'b094dac5d93471bdec1a502270e3cc6c',
        ],
        'case 16, unaligned text with additional data' => [
            'key' => 'feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308',
            'iv' => 'cafebabefacedbaddecaf888',
            'aad' => 'feedfacedeadbeeffeedfacedeadbeefabaddad2',
            'plaintext' => 'd9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72'
                . '1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39',
            'ciphertext' => '522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa'
                . '8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662',
            'tag' => '76fc6ece0f4e1768cddf8853bb2d551b',
        ],
    ];

    $suite->test('AES-256-GCM matches the published test vectors', function () use ($gcm) {
        foreach ($gcm as $name => $vector) {
            $sealed = StatsCrypto::seal(
                (string) hex2bin($vector['plaintext']),
                (string) hex2bin($vector['key']),
                (string) hex2bin($vector['aad']),
                (string) hex2bin($vector['iv'])
            );

            Assert::same($vector['ciphertext'], bin2hex($sealed['ciphertext']), $name . ': ciphertext');
            Assert::same($vector['tag'], bin2hex($sealed['tag']), $name . ': authentication tag');
            Assert::same($vector['iv'], bin2hex($sealed['iv']), $name . ': the IV is returned unchanged');
        }
    });

    $suite->test('the independent client decrypts what the server produced', function () use ($gcm) {
        foreach ($gcm as $name => $vector) {
            $opened = StatsClient::decrypt(
                (string) hex2bin($vector['ciphertext']),
                (string) hex2bin($vector['key']),
                (string) hex2bin($vector['iv']),
                (string) hex2bin($vector['tag']),
                (string) hex2bin($vector['aad'])
            );

            Assert::same((string) hex2bin($vector['plaintext']), (string) $opened, $name . ': round trip');
        }
    });

    $suite->test('HKDF-SHA-256 matches RFC 5869', function () {
        // Test case 1: salt and info present.
        Assert::same(
            '3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865',
            bin2hex(StatsCrypto::hkdfSha256(
                (string) hex2bin(str_repeat('0b', 22)),
                (string) hex2bin('000102030405060708090a0b0c'),
                (string) hex2bin('f0f1f2f3f4f5f6f7f8f9'),
                42
            )),
            'RFC 5869 test case 1'
        );

        // Test case 3: neither salt nor info, which exercises the zero salt.
        Assert::same(
            '8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8',
            bin2hex(StatsCrypto::hkdfSha256((string) hex2bin(str_repeat('0b', 22)), '', '', 42)),
            'RFC 5869 test case 3'
        );
    });

    $suite->test('HKDF-SHA-256 agrees with the core implementation', function () {
        for ($i = 0; $i < 10; $i++) {
            $secret = random_bytes(random_int(1, 64));
            // An empty salt is included on purpose: both implementations have
            // to fall back to the same block of zero bytes.
            $saltLength = random_int(0, 40);
            $salt = $saltLength === 0 ? '' : random_bytes($saltLength);
            $info = $i % 3 === 0 ? '' : bin2hex(random_bytes(random_int(1, 8)));
            $length = random_int(1, 96);

            Assert::same(
                bin2hex(hash_hkdf('sha256', $secret, $length, $info, $salt)),
                bin2hex(StatsCrypto::hkdfSha256($secret, $salt, $info, $length)),
                'the hand written extract and expand must equal hash_hkdf()'
            );
        }
    });

    $suite->test('the derived key is neither the secret nor a concatenation', function () {
        $secret = Harness::SECRET;
        $canonical = "POST\nstats\n2026-08-08T14:20:00Z\no5TqZ0nS1w7bK3xR9dLp";
        $key = StatsCrypto::deriveKey($secret, $canonical);

        Assert::same(32, strlen($key), 'the key must be exactly 32 bytes');
        Assert::notSame($secret, $key, 'the key must not be the secret');
        Assert::notSame(substr($secret, 0, 32), $key, 'nor a truncation of it');
        Assert::notSame(hash('sha256', $secret, true), $key, 'nor a plain hash of it');
        Assert::notSame(hash('sha256', $secret . $canonical, true), $key, 'nor a plain concatenation');
        Assert::notSame(hash('sha256', $canonical . $secret, true), $key, 'nor the other concatenation');
        Assert::notContains($secret, $key, 'the secret must not survive inside the key');

        Assert::same(
            bin2hex($key),
            bin2hex(StatsClient::deriveKey($secret, $canonical)),
            'the independent client must derive the same key'
        );

        Assert::notSame(
            bin2hex($key),
            bin2hex(StatsCrypto::deriveKey($secret, $canonical . 'x')),
            'a different canonical request must give a different key'
        );
        Assert::notSame(
            bin2hex($key),
            bin2hex(StatsCrypto::deriveKey($secret . 'x', $canonical)),
            'a different secret must give a different key'
        );
    });

    $suite->test('every response gets its own IV', function () {
        $key = random_bytes(32);
        $seen = [];
        for ($i = 0; $i < 50; $i++) {
            $sealed = StatsCrypto::encrypt('{"10.99.99.2":"2026-08-08T14:20:00Z"}', $key, 'aad');
            Assert::same(12, strlen($sealed['iv']), 'the IV must be 12 bytes');
            Assert::same(16, strlen($sealed['tag']), 'the tag must be 16 bytes');
            $seen[bin2hex($sealed['iv'])] = true;
        }

        Assert::count(50, $seen, 'fifty responses must use fifty different IVs');
    });

    $suite->test('empty and ordinary statistics both survive a round trip', function () {
        $key = random_bytes(32);
        $canonical = "POST\nstats\n2026-08-08T14:20:00Z\nnonce-value-1234567890";

        $entries = [];
        for ($octet = 2; $octet <= 254; $octet++) {
            $entries['10.99.99.' . $octet] = '2026-08-08T14:20:00Z';
        }
        $payloads = ['{}', '{"10.99.99.2":"2026-08-08T14:20:00Z"}', '', (string) json_encode($entries)];

        foreach ($payloads as $payload) {
            $sealed = StatsCrypto::encrypt($payload, $key, $canonical);
            $opened = StatsClient::decrypt(
                $sealed['ciphertext'],
                $key,
                $sealed['iv'],
                $sealed['tag'],
                $canonical
            );

            Assert::same(strlen($payload), strlen($sealed['ciphertext']), 'GCM must not change the length');
            Assert::true(is_string($opened), 'the tag of a ' . strlen($payload) . ' byte payload must verify');
            Assert::same($payload, (string) $opened, 'the plaintext must come back intact');
        }
    });

    $suite->test('any change to the sealed response breaks the tag', function () {
        $key = random_bytes(32);
        $canonical = "POST\nstats\n2026-08-08T14:20:00Z\nnonce-value-1234567890";
        $payload = '{"10.99.99.2":"2026-08-08T14:20:00Z","10.99.99.4":"2026-08-08T14:19:02Z"}';
        $sealed = StatsCrypto::encrypt($payload, $key, $canonical);

        $flip = static function (string $value, int $offset): string {
            $value[$offset] = chr(ord($value[$offset]) ^ 0x01);

            return $value;
        };

        $tampered = [
            'ciphertext' => [$flip($sealed['ciphertext'], 3), $key, $sealed['iv'], $sealed['tag'], $canonical],
            'tag' => [$sealed['ciphertext'], $key, $sealed['iv'], $flip($sealed['tag'], 15), $canonical],
            'IV' => [$sealed['ciphertext'], $key, $flip($sealed['iv'], 0), $sealed['tag'], $canonical],
            'AAD' => [$sealed['ciphertext'], $key, $sealed['iv'], $sealed['tag'], $canonical . ' '],
            'key' => [$sealed['ciphertext'], $flip($key, 31), $sealed['iv'], $sealed['tag'], $canonical],
        ];

        foreach ($tampered as $what => $arguments) {
            Assert::same(
                null,
                StatsClient::decrypt(...$arguments),
                'a changed ' . $what . ' must yield no plaintext at all'
            );
        }

        Assert::same(
            $payload,
            (string) StatsClient::decrypt($sealed['ciphertext'], $key, $sealed['iv'], $sealed['tag'], $canonical),
            'the untouched response still opens'
        );
    });

    $suite->test('the shipped preflight vector passes on this host', function () {
        Assert::true(StatsCrypto::supported(), 'a 64 bit build is required for the stats action');
        Assert::true(StatsCrypto::selfCheck(), 'the built in known answer check must pass');
    });

    $suite->test('encrypting the largest possible statistics stays fast', function () {
        $entries = [];
        for ($octet = 2; $octet <= 254; $octet++) {
            $entries['10.99.99.' . $octet] = '2026-08-08T14:20:00Z';
        }
        $payload = (string) json_encode($entries);
        Assert::greaterOrEqual(9000, strlen($payload), 'the worst case payload must really be large');

        $key = random_bytes(32);
        $started = hrtime(true);
        StatsCrypto::encrypt($payload, $key, 'aad');
        $elapsedMs = (hrtime(true) - $started) / 1e6;

        // Generous, because a shared hosting CPU is slower than a test machine.
        Assert::lessOrEqual(250.0, $elapsedMs, 'pure PHP AES must stay usable for the whole table');
    });
};

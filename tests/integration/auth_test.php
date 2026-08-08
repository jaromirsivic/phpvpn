<?php

declare(strict_types=1);

return function (Suite $suite): void {
    $instance = null;
    $get = static function () use (&$instance): Instance {
        return $instance ??= Harness::instance();
    };

    $suite->test('an empty signed sync is accepted', function () use ($get) {
        $result = Vpn::resultOf(Vpn::sync($get(), Harness::CLIENT_A), 'empty sync');

        Assert::count(0, $result['acceptedPacketUuids'], 'nothing was sent');
        Assert::same(false, $result['responseTruncated'], 'nothing is waiting');
        Assert::greaterOrEqual(1, $result['recommendedRequestPackets'], 'the server advises a batch size');
    });

    $suite->test('a tampered body is rejected', function () use ($get) {
        $body = SignRequest::body(Harness::CLIENT_A, Harness::TOKENS[Harness::CLIENT_A], Harness::SECRET, 'sync');
        $tampered = $body . SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'injected') . "\n";

        Assert::same(401, Vpn::post($get(), $tampered)['status'], 'appending to a signed body must fail');
    });

    $suite->test('a signature for another action is rejected', function () use ($get) {
        $body = SignRequest::body(
            Harness::CLIENT_A,
            Harness::TOKENS[Harness::CLIENT_A],
            Harness::SECRET,
            'selftest'
        );

        Assert::same(401, Vpn::post($get(), $body)['status'], 'the action is part of the canonical request');
    });

    $suite->test('a wrong access token is rejected', function () use ($get) {
        $body = SignRequest::body(
            Harness::CLIENT_A,
            Harness::TOKENS[Harness::CLIENT_B],
            Harness::SECRET,
            'sync'
        );

        Assert::same(401, Vpn::post($get(), $body)['status'], 'the token must belong to the claimed client');
    });

    $suite->test('a wrong server secret is rejected', function () use ($get) {
        $body = SignRequest::body(
            Harness::CLIENT_A,
            Harness::TOKENS[Harness::CLIENT_A],
            'a-different-server-secret-0123456789',
            'sync'
        );

        Assert::same(401, Vpn::post($get(), $body)['status'], 'the signature binds the server secret too');
    });

    $suite->test('an unconfigured client is rejected like any other', function () use ($get) {
        $body = SignRequest::body(
            '10.99.99.200',
            str_repeat('c8', 32),
            Harness::SECRET,
            'sync'
        );
        $response = Vpn::post($get(), $body);

        Assert::same(401, $response['status'], 'unknown clients must not be treated differently');
        Assert::notContains('10.99.99.', $response['body'], 'the reply must not confirm any address');
    });

    $suite->test('a malformed client address is rejected', function () use ($get) {
        foreach (['10.99.99.1', '10.99.99.255', '192.168.0.1', '10.99.99.02', ''] as $client) {
            $body = SignRequest::body(
                Harness::CLIENT_A,
                Harness::TOKENS[Harness::CLIENT_A],
                Harness::SECRET,
                'sync',
                [],
                ['client' => $client]
            );
            Assert::same(401, Vpn::post($get(), $body)['status'], 'must reject client ' . var_export($client, true));
        }
    });

    $suite->test('a timestamp outside the allowed skew is rejected', function () use ($get) {
        foreach ([-3600, -120, 120, 3600] as $offset) {
            $body = SignRequest::body(
                Harness::CLIENT_A,
                Harness::TOKENS[Harness::CLIENT_A],
                Harness::SECRET,
                'sync',
                [],
                ['timestamp' => gmdate('Y-m-d\TH:i:s\Z', time() + $offset)]
            );
            Assert::same(401, Vpn::post($get(), $body)['status'], 'offset ' . $offset . 's must be refused');
        }
    });

    $suite->test('a timestamp inside the allowed skew is accepted', function () use ($get) {
        foreach ([-30, 0, 30] as $offset) {
            $body = SignRequest::body(
                Harness::CLIENT_A,
                Harness::TOKENS[Harness::CLIENT_A],
                Harness::SECRET,
                'sync',
                [],
                ['timestamp' => gmdate('Y-m-d\TH:i:s\Z', time() + $offset)]
            );
            Assert::same(200, Vpn::post($get(), $body)['status'], 'offset ' . $offset . 's must be accepted');
        }
    });

    $suite->test('a malformed nonce is rejected', function () use ($get) {
        foreach (['', 'short', str_repeat('n', 65), 'has spaces in it 1234', 'plus+slash/chars=='] as $nonce) {
            $body = SignRequest::body(
                Harness::CLIENT_A,
                Harness::TOKENS[Harness::CLIENT_A],
                Harness::SECRET,
                'sync',
                [],
                ['nonce' => $nonce]
            );
            Assert::same(401, Vpn::post($get(), $body)['status'], 'must reject nonce ' . var_export($nonce, true));
        }
    });

    $suite->test('a missing or misplaced auth record is a protocol error', function () use ($get) {
        Assert::same(400, Vpn::post($get(), '')['status'], 'an empty body has no auth record');

        $packet = SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'x');
        Assert::same(400, Vpn::post($get(), $packet . "\n")['status'], 'a data line cannot come first');

        $body = SignRequest::body(
            Harness::CLIENT_A,
            Harness::TOKENS[Harness::CLIENT_A],
            Harness::SECRET,
            'sync',
            [],
            ['bodyOverride' => '{"type":"auth","client":"10.99.99.2","timestamp":"2026-01-01T00:00:00Z",'
                . '"nonce":"aaaaaaaaaaaaaaaa","signature":"' . str_repeat('a', 64) . '"}' . "\n"]
        );
        Assert::same(400, Vpn::post($get(), $body)['status'], 'a second auth record is a protocol error');
    });

    $suite->test('a parse error never leaks before the signature is checked', function () use ($get) {
        $broken = '{"type":"data","uuid":"nope"' . "\n";

        $signed = SignRequest::body(
            Harness::CLIENT_A,
            Harness::TOKENS[Harness::CLIENT_A],
            Harness::SECRET,
            'sync',
            [],
            ['bodyOverride' => $broken]
        );
        Assert::same(400, Vpn::post($get(), $signed)['status'], 'an authenticated caller learns it sent junk');

        $unsigned = SignRequest::body(
            Harness::CLIENT_A,
            'ff' . substr(Harness::TOKENS[Harness::CLIENT_A], 2),
            Harness::SECRET,
            'sync',
            [],
            ['bodyOverride' => $broken]
        );
        Assert::same(
            401,
            Vpn::post($get(), $unsigned)['status'],
            'an unauthenticated caller must not get a parser oracle'
        );
    });

    $suite->test('a spoofed source address is rejected', function () use ($get) {
        $packet = SignRequest::packet(Harness::CLIENT_B, Harness::CLIENT_C, 'not mine');
        $response = Vpn::sync($get(), Harness::CLIENT_A, [$packet]);

        Assert::same(401, $response['status'], 'src must equal the authenticated client');
        Assert::count(0, $get()->queueFiles(Harness::CLIENT_C, '.data'), 'nothing may be queued');
    });

    $suite->test('nonces are not persisted when the switch is off', function () use ($get) {
        Vpn::sync($get(), Harness::CLIENT_A);

        Assert::false(
            is_dir($get()->queueRoot . '/_nonce'),
            'PERSIST_NONCES = false must not create nonce state'
        );
    });

    $suite->test('an identical request replays successfully when nonces are off', function () use ($get) {
        $body = SignRequest::body(Harness::CLIENT_A, Harness::TOKENS[Harness::CLIENT_A], Harness::SECRET, 'sync');

        Assert::same(200, Vpn::post($get(), $body)['status'], 'first attempt');
        Assert::same(200, Vpn::post($get(), $body)['status'], 'replay is an accepted trade off when nonces are off');
    });

    $suite->test('a replayed request is refused when nonces are persisted', function () {
        $strict = Harness::instance(['PERSIST_NONCES' => 'true']);
        $body = SignRequest::body(Harness::CLIENT_A, Harness::TOKENS[Harness::CLIENT_A], Harness::SECRET, 'sync');

        Assert::same(200, Vpn::post($strict, $body)['status'], 'the first use is fine');
        Assert::same(401, Vpn::post($strict, $body)['status'], 'the replay is refused');
        Assert::same(200, Vpn::sync($strict, Harness::CLIENT_A)['status'], 'a fresh nonce still works');

        $buckets = glob($strict->queueRoot . '/_nonce/002/*.bin') ?: [];
        Assert::count(1, $buckets, 'one bucket per client and minute, not one file per nonce');
        Assert::same(16, filesize($buckets[0]), 'two eight byte records for two accepted nonces');
    });

    $suite->test('the whole auth suite produced no PHP diagnostics', function () use ($get) {
        Assert::same('', trim($get()->phpErrorLog()), 'no warnings or notices');
    });
};

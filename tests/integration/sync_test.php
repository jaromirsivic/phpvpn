<?php

declare(strict_types=1);

return function (Suite $suite): void {
    $instance = null;
    $get = static function () use (&$instance): Instance {
        return $instance ??= Harness::instance();
    };

    $suite->test('a packet travels from one client to another', function () use ($get) {
        $uuid = SignRequest::uuid();
        $sent = Vpn::resultOf(
            Vpn::sync($get(), Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'hello neighbour', $uuid),
            ]),
            'send'
        );
        Assert::same([$uuid], $sent['acceptedPacketUuids'], 'the sender is told exactly what was stored');
        Assert::count(0, $sent['deferredPacketUuids'], 'nothing was deferred');

        $received = Vpn::sync($get(), Harness::CLIENT_B);
        $deliveries = SignRequest::deliveries($received['body']);

        Assert::count(1, $deliveries, 'an empty sync picks the packet up');
        Assert::same($uuid, $deliveries[0]['packet']['uuid'], 'uuid survives');
        Assert::same(Harness::CLIENT_A, $deliveries[0]['packet']['src'], 'source survives');
        Assert::same(Harness::CLIENT_B, $deliveries[0]['packet']['dst'], 'destination survives');
        Assert::same(
            'hello neighbour',
            base64_decode($deliveries[0]['packet']['data'], true),
            'the encrypted payload is relayed untouched'
        );
    });

    $suite->test('the sender never receives its own packet', function () use ($get) {
        Vpn::sync($get(), Harness::CLIENT_A, [
            SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'for B only'),
        ]);

        $back = Vpn::sync($get(), Harness::CLIENT_A);
        Assert::count(0, SignRequest::deliveries($back['body']), 'a sender queue must stay untouched');

        Vpn::sync($get(), Harness::CLIENT_B); // drain
    });

    $suite->test('one response carries batches from several senders', function () use ($get) {
        Vpn::sync($get(), Harness::CLIENT_A, [
            SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_C, 'from A'),
        ]);
        Vpn::sync($get(), Harness::CLIENT_B, [
            SignRequest::packet(Harness::CLIENT_B, Harness::CLIENT_C, 'from B'),
        ]);

        $deliveries = SignRequest::deliveries(Vpn::sync($get(), Harness::CLIENT_C)['body']);
        Assert::count(2, $deliveries, 'both batches arrive in one cycle');

        $sources = [$deliveries[0]['packet']['src'], $deliveries[1]['packet']['src']];
        sort($sources);
        Assert::same([Harness::CLIENT_A, Harness::CLIENT_B], $sources, 'from two different senders');
        Assert::notSame(
            $deliveries[0]['batchId'],
            $deliveries[1]['batchId'],
            'each sender produced its own batch'
        );
    });

    $suite->test('one batch can carry many packets', function () use ($get) {
        $lines = [];
        $uuids = [];
        for ($i = 0; $i < 120; $i++) {
            $uuid = SignRequest::uuid();
            $uuids[] = $uuid;
            $lines[] = SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'packet ' . $i, $uuid);
        }

        $result = Vpn::resultOf(Vpn::sync($get(), Harness::CLIENT_A, $lines), 'bulk send');
        Assert::count(120, $result['acceptedPacketUuids'], 'all packets are stored');
        Assert::same(120, $result['acceptedPackets'], 'the count matches');

        $deliveries = SignRequest::deliveries(Vpn::sync($get(), Harness::CLIENT_B)['body']);
        Assert::count(120, $deliveries, 'and all of them are delivered');

        $delivered = array_map(static fn (array $d): string => $d['packet']['uuid'], $deliveries);
        Assert::same($uuids, $delivered, 'in the order they were sent');
    });

    $suite->test('a claimed batch is only removed once it is acknowledged', function () {
        $fresh = Harness::instance();
        Vpn::sync($fresh, Harness::CLIENT_A, [
            SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'needs an ack'),
        ]);

        $deliveries = SignRequest::deliveries(Vpn::sync($fresh, Harness::CLIENT_B)['body']);
        Assert::count(1, $deliveries, 'delivered once');
        Assert::count(1, $fresh->queueFiles(Harness::CLIENT_B, '.sending'), 'the batch waits for an ack');

        Vpn::sync($fresh, Harness::CLIENT_B, [SignRequest::ack($deliveries[0]['batchId'])]);
        Assert::count(0, $fresh->queueFiles(Harness::CLIENT_B, '.sending'), 'the ack releases it');
        Assert::count(0, $fresh->queueFiles(Harness::CLIENT_B, '.data'), 'and nothing is left behind');
    });

    $suite->test('an unacknowledged batch is redelivered, never silently lost', function () {
        $fresh = Harness::instance();
        $uuid = SignRequest::uuid();
        Vpn::sync($fresh, Harness::CLIENT_A, [
            SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'at least once', $uuid),
        ]);

        $first = SignRequest::deliveries(Vpn::sync($fresh, Harness::CLIENT_B)['body']);
        Assert::count(1, $first, 'first delivery');

        // A client that never came back with an ack.
        Assert::same(1, $fresh->expireClaims(Harness::CLIENT_B, 120), 'one claim was aged');

        $second = SignRequest::deliveries(Vpn::sync($fresh, Harness::CLIENT_B)['body']);
        Assert::count(1, $second, 'the batch is offered again');
        Assert::same($uuid, $second[0]['packet']['uuid'], 'the client deduplicates on uuid');
        Assert::same($first[0]['batchId'], $second[0]['batchId'], 'and it is the very same batch');
    });

    $suite->test('record boundaries survive arbitrary TCP fragmentation', function () use ($get) {
        $lines = [];
        for ($i = 0; $i < 12; $i++) {
            $lines[] = SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_C, str_repeat('f', $i * 31 + 1));
        }
        $body = SignRequest::body(
            Harness::CLIENT_A,
            Harness::TOKENS[Harness::CLIENT_A],
            Harness::SECRET,
            'sync',
            $lines
        );

        $response = HttpClient::slowRequest(
            $get()->url() . '?action=sync',
            'POST',
            ['Content-Type' => Vpn::NDJSON],
            $body,
            7
        );
        $result = Vpn::resultOf($response, 'fragmented send');
        Assert::count(12, $result['acceptedPacketUuids'], 'every record is parsed');

        Assert::count(
            12,
            SignRequest::deliveries(Vpn::sync($get(), Harness::CLIENT_C)['body']),
            'and every record is delivered'
        );
    });

    $suite->test('a malformed data record fails the whole request', function () use ($get) {
        $bad = [
            '{"type":"data","uuid":"not-a-uuid","src":"10.99.99.2","dst":"10.99.99.3","data":"YWJj"}',
            '{"type":"data","uuid":"' . SignRequest::uuid() . '","src":"10.99.99.2","dst":"10.99.99.1","data":"YWJj"}',
            '{"type":"data","uuid":"' . SignRequest::uuid() . '","src":"10.99.99.2","dst":"10.99.99.3","data":""}',
            '{"type":"data","uuid":"' . SignRequest::uuid() . '","src":"10.99.99.2","dst":"10.99.99.3","data":"a b"}',
            '{"type":"data","uuid":"' . SignRequest::uuid() . '","src":"10.99.99.2","dst":"10.99.99.3"}',
            '{"type":"data","uuid":"' . SignRequest::uuid() . '","src":"10.99.99.2","dst":"10.99.99.3",'
                . '"data":"YWJj","extra":1}',
            '{"type":"unknown"}',
            '{"type":"ack","batchId":"../../etc/passwd"}',
            'not json at all',
        ];

        foreach ($bad as $line) {
            $response = Vpn::sync($get(), Harness::CLIENT_A, [$line]);
            Assert::true(
                $response['status'] === 400 || $response['status'] === 401,
                'must refuse ' . substr($line, 0, 60) . ' (got ' . $response['status'] . ')'
            );
        }
    });

    $suite->test('a valid packet next to a broken one is not stored', function () use ($get) {
        $good = SignRequest::uuid();
        $response = Vpn::sync($get(), Harness::CLIENT_A, [
            SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_C, 'valid', $good),
            '{"type":"data","uuid":"broken"}',
        ]);

        Assert::same(400, $response['status'], 'the whole request fails');
        Assert::count(0, $get()->queueFiles(Harness::CLIENT_C, '.data'), 'nothing is published');
    });

    $suite->test('a body without a final newline is a protocol error', function () use ($get) {
        $body = SignRequest::body(Harness::CLIENT_A, Harness::TOKENS[Harness::CLIENT_A], Harness::SECRET, 'sync');
        $truncated = rtrim($body . SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'x'), "\n");

        Assert::same(401, Vpn::post($get(), $truncated)['status'], 'the appended line also breaks the signature');

        $signed = SignRequest::body(
            Harness::CLIENT_A,
            Harness::TOKENS[Harness::CLIENT_A],
            Harness::SECRET,
            'sync',
            [],
            ['bodyOverride' => SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'x')]
        );
        Assert::same(400, Vpn::post($get(), $signed)['status'], 'every record must be newline terminated');
    });

    $suite->test('an oversized line is refused before it is parsed', function () use ($get) {
        $huge = SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, str_repeat('h', 200000));
        $response = Vpn::sync($get(), Harness::CLIENT_A, [$huge]);

        Assert::same(413, $response['status'], 'MAX_PACKET_LINE_BYTES is enforced');
    });

    $suite->test('the response headers describe the payload exactly', function () use ($get) {
        Vpn::sync($get(), Harness::CLIENT_A, [
            SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'measure me'),
        ]);
        $response = Vpn::sync($get(), Harness::CLIENT_B);

        Assert::contains('application/x-ndjson', $response['headers']['content-type'] ?? '', 'NDJSON content type');
        if (isset($response['headers']['content-length'])) {
            Assert::same(
                strlen($response['body']),
                (int) $response['headers']['content-length'],
                'Content-Length must match the body exactly'
            );
        }
        Assert::same("\n", substr($response['body'], -1), 'every record ends with a newline');
    });

    $suite->test('the sync suite produced no PHP diagnostics', function () use ($get) {
        Assert::same('', trim($get()->phpErrorLog()), 'no warnings or notices');
    });
};

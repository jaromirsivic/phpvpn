<?php

declare(strict_types=1);

return function (Suite $suite): void {
    $suite->test('a request above the operational cap is split into accepted and deferred', function () {
        $limited = Harness::instance([
            'MAX_ACCEPTED_REQUEST_PACKETS' => '5',
            'MAX_REQUEST_PACKETS' => '50',
        ]);

        $uuids = [];
        $lines = [];
        for ($i = 0; $i < 12; $i++) {
            $uuid = SignRequest::uuid();
            $uuids[] = $uuid;
            $lines[] = SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'p' . $i, $uuid);
        }

        $result = Vpn::resultOf(Vpn::sync($limited, Harness::CLIENT_A, $lines), 'over cap');

        Assert::count(5, $result['acceptedPacketUuids'], 'only the operational cap is stored');
        Assert::count(7, $result['deferredPacketUuids'], 'the rest is deferred, not lost');
        Assert::same(
            $uuids,
            array_merge($result['acceptedPacketUuids'], $result['deferredPacketUuids']),
            'accepted and deferred must partition the request exactly and keep order'
        );
        Assert::count(
            0,
            array_intersect($result['acceptedPacketUuids'], $result['deferredPacketUuids']),
            'no uuid may appear in both lists'
        );

        Assert::count(
            5,
            SignRequest::deliveries(Vpn::sync($limited, Harness::CLIENT_B)['body']),
            'exactly the accepted packets are delivered'
        );
    });

    $suite->test('the byte cap defers just as precisely as the packet cap', function () {
        $limited = Harness::instance([
            'MAX_ACCEPTED_REQUEST_BYTES' => '2048',
            'MAX_ACCEPTED_REQUEST_PACKETS' => '200',
            'MAX_REQUEST_PACKETS' => '200',
        ]);

        $lines = [];
        for ($i = 0; $i < 40; $i++) {
            $lines[] = SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, str_repeat('b', 120));
        }

        $result = Vpn::resultOf(Vpn::sync($limited, Harness::CLIENT_A, $lines), 'byte cap');
        $accepted = count($result['acceptedPacketUuids']);

        Assert::greaterOrEqual(1, $accepted, 'at least something is accepted');
        Assert::lessOrEqual(39, $accepted, 'the byte cap really bites');
        Assert::same(
            40,
            $accepted + count($result['deferredPacketUuids']),
            'every packet is accounted for'
        );
        Assert::lessOrEqual(2048, $result['acceptedBytes'], 'the cap is respected');
    });

    $suite->test('inconsistent limits stop the server from serving sync', function () {
        // MAX_ACCEPTED_REQUEST_PACKETS above MAX_REQUEST_PACKETS is nonsense,
        // so the server must refuse rather than pick an interpretation.
        $broken = Harness::instance(['MAX_REQUEST_PACKETS' => '10']);
        $response = Vpn::sync($broken, Harness::CLIENT_A);

        Assert::same(503, $response['status'], 'a contradictory configuration must not serve traffic');
        Assert::contains('server_misconfigured', $response['body'], 'and must say so generically');
    });

    $suite->test('the protocol packet ceiling is a hard error', function () {
        $limited = Harness::instance([
            'MAX_REQUEST_PACKETS' => '10',
            'MAX_ACCEPTED_REQUEST_PACKETS' => '10',
        ]);

        $lines = [];
        for ($i = 0; $i < 15; $i++) {
            $lines[] = SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'x');
        }

        $response = Vpn::sync($limited, Harness::CLIENT_A, $lines);
        Assert::same(413, $response['status'], 'past the protocol ceiling the request is refused');
        Assert::count(0, $limited->queueFiles(Harness::CLIENT_B, '.data'), 'and nothing is stored');
    });

    $suite->test('an oversized body is refused before it is read', function () {
        $small = Harness::instance([
            'MAX_REQUEST_BYTES' => '4096',
            'MAX_PACKET_LINE_BYTES' => '2048',
            'MAX_ACCEPTED_REQUEST_BYTES' => '2048',
        ]);

        $lines = [];
        for ($i = 0; $i < 60; $i++) {
            $lines[] = SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, str_repeat('o', 100));
        }

        $response = Vpn::sync($small, Harness::CLIENT_A, $lines);
        Assert::same(413, $response['status'], 'the Content-Length guard fires first');
    });

    $suite->test('a full destination queue is refused without damaging it', function () {
        $tight = Harness::instance(['MAX_BUFFERS' => '3']);

        for ($i = 0; $i < 3; $i++) {
            $result = Vpn::resultOf(
                Vpn::sync($tight, Harness::CLIENT_A, [
                    SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'batch ' . $i),
                ]),
                'batch ' . $i
            );
            Assert::count(1, $result['acceptedPacketUuids'], 'batch ' . $i . ' is stored');
        }

        $response = Vpn::sync($tight, Harness::CLIENT_A, [
            SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'one too many'),
        ]);
        Assert::same(429, $response['status'], 'a full queue is a controlled refusal');
        Assert::contains('queue_full', $response['body'], 'with a generic reason');
        Assert::same('1', $response['headers']['retry-after'] ?? '', 'and a Retry-After hint');

        Assert::count(3, $tight->queueFiles(Harness::CLIENT_B, '.data'), 'older batches are untouched');
        Assert::count(0, $tight->queueFiles(Harness::CLIENT_B, '.writing'), 'no partial write is left behind');

        Assert::count(
            3,
            SignRequest::deliveries(Vpn::sync($tight, Harness::CLIENT_B)['body']),
            'and all three are still delivered'
        );
    });

    $suite->test('a partly writable fan out stores what it can and defers the rest', function () {
        $tight = Harness::instance(['MAX_BUFFERS' => '2']);

        // Fill the queue of C so only B can accept anything.
        for ($i = 0; $i < 2; $i++) {
            Vpn::sync($tight, Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_C, 'filler ' . $i),
            ]);
        }

        $toB = SignRequest::uuid();
        $toC = SignRequest::uuid();
        $result = Vpn::resultOf(
            Vpn::sync($tight, Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'reaches B', $toB),
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_C, 'cannot reach C', $toC),
            ]),
            'mixed fan out'
        );

        Assert::same([$toB], $result['acceptedPacketUuids'], 'only the stored packet is claimed as accepted');
        Assert::same([$toC], $result['deferredPacketUuids'], 'the refused destination is deferred for a retry');
    });

    $suite->test('a large queue is drained over several cycles', function () {
        $narrow = Harness::instance(['MAX_RESPONSE_BYTES' => '1024']);

        $sent = [];
        for ($i = 0; $i < 6; $i++) {
            $uuid = SignRequest::uuid();
            $sent[] = $uuid;
            Vpn::sync($narrow, Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'cycle ' . $i, $uuid),
            ]);
        }

        $received = [];
        $cycles = 0;
        $sawTruncation = false;
        do {
            $cycles++;
            $response = Vpn::sync($narrow, Harness::CLIENT_B);
            $result = Vpn::resultOf($response, 'cycle ' . $cycles);
            $deliveries = SignRequest::deliveries($response['body']);

            Assert::lessOrEqual(
                1024 + 512,
                strlen($response['body']),
                'a response must respect MAX_RESPONSE_BYTES plus the sync-result line'
            );
            foreach ($deliveries as $delivery) {
                $received[] = $delivery['packet']['uuid'];
            }
            $sawTruncation = $sawTruncation || $result['responseTruncated'] === true;
        } while ($result['responseTruncated'] === true && $cycles < 10);

        Assert::true($sawTruncation, 'the client is told when more is waiting');
        Assert::greaterOrEqual(2, $cycles, 'the queue really needed more than one cycle');
        Assert::same($sent, $received, 'everything arrives, in order, exactly once');
    });

    $suite->test('the server advises a smaller batch when a queue fills up', function () {
        $tight = Harness::instance(['MAX_BUFFERS' => '8']);

        $relaxed = Vpn::resultOf(
            Vpn::sync($tight, Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'first'),
            ]),
            'empty queue'
        );

        for ($i = 0; $i < 6; $i++) {
            Vpn::sync($tight, Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'fill ' . $i),
            ]);
        }

        $pressured = Vpn::resultOf(
            Vpn::sync($tight, Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'under pressure'),
            ]),
            'full queue'
        );

        Assert::true(
            $pressured['recommendedRequestBytes'] < $relaxed['recommendedRequestBytes'],
            'the byte recommendation must drop under pressure'
        );
        Assert::true(
            $pressured['recommendedRequestPackets'] < $relaxed['recommendedRequestPackets'],
            'the packet recommendation must drop under pressure'
        );
        Assert::greaterOrEqual(1, $pressured['recommendedRequestPackets'], 'but never reach zero');
    });
};

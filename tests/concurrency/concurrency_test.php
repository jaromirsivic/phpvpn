<?php

/**
 * Concurrency is the requirement the whole design hangs on, so these tests run
 * against several independent PHP processes sharing one queue root.
 *
 * A single process cannot prove mutual exclusion. Windows offers neither
 * PHP_CLI_SERVER_WORKERS nor PHP_FCGI_CHILDREN, both of which need fork, so the
 * harness starts N separate built-in servers instead.
 */

declare(strict_types=1);

return function (Suite $suite): void {
    /** Drains one client completely and returns every delivery it saw. */
    $drain = static function (Instance $instance, string $client, int $maxCycles = 40): array {
        $all = [];
        $acks = [];
        for ($cycle = 0; $cycle < $maxCycles; $cycle++) {
            $response = Vpn::sync($instance, $client, $acks);
            Assert::same(200, $response['status'], 'drain cycle ' . $cycle . ' must succeed');
            $deliveries = SignRequest::deliveries($response['body']);
            $acks = [];
            foreach ($deliveries as $delivery) {
                $all[] = $delivery;
                $acks[] = SignRequest::ack($delivery['batchId']);
            }
            if ($deliveries === []) {
                break;
            }
        }

        return $all;
    };

    $suite->test('parallel senders to one destination never lose or corrupt a packet', function () use ($drain) {
        $instance = Harness::instance([], 4);
        Assert::same(4, $instance->serverCount(), 'four independent PHP processes');

        $expected = [];
        $requests = [];
        for ($i = 0; $i < 24; $i++) {
            $lines = [];
            for ($j = 0; $j < 3; $j++) {
                $uuid = SignRequest::uuid();
                $expected[] = $uuid;
                $lines[] = SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, "r{$i}p{$j}", $uuid);
            }
            $requests[] = Vpn::requestFor($instance, Harness::CLIENT_A, $lines, $i % 4);
        }

        $responses = HttpClient::parallel($requests, 60.0);

        $accepted = [];
        foreach ($responses as $index => $response) {
            $result = Vpn::resultOf($response, 'parallel send ' . $index);
            foreach ($result['acceptedPacketUuids'] as $uuid) {
                $accepted[] = $uuid;
            }
            Assert::count(0, $result['deferredPacketUuids'], 'nothing should need deferring at this volume');
        }

        Assert::count(72, $accepted, 'every packet is reported as stored');
        Assert::count(0, $instance->queueFiles(Harness::CLIENT_B, '.writing'), 'no partial write survives');

        $delivered = array_map(
            static fn (array $delivery): string => $delivery['packet']['uuid'],
            $drain($instance, Harness::CLIENT_B)
        );

        sort($accepted);
        $unique = array_unique($delivered);
        sort($unique);

        Assert::count(72, $delivered, 'every stored packet is delivered exactly once');
        Assert::same($accepted, $unique, 'the delivered set equals the accepted set');
    });

    $suite->test('parallel readers never receive the same batch twice', function () {
        $instance = Harness::instance([], 4);

        $sent = [];
        for ($i = 0; $i < 20; $i++) {
            $uuid = SignRequest::uuid();
            $sent[] = $uuid;
            Vpn::sync($instance, Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'batch ' . $i, $uuid),
            ], [], $i % 4);
        }

        $requests = [];
        for ($i = 0; $i < 8; $i++) {
            $requests[] = Vpn::requestFor($instance, Harness::CLIENT_B, [], $i % 4);
        }
        $responses = HttpClient::parallel($requests, 60.0);

        $batchIds = [];
        $uuids = [];
        $refused = 0;
        foreach ($responses as $index => $response) {
            if ($response['status'] === 503) {
                // A lock budget exhausted under contention is a controlled
                // outcome, never a corrupted or doubly served batch.
                $refused++;
                continue;
            }
            Vpn::resultOf($response, 'parallel read ' . $index);
            foreach (SignRequest::deliveries($response['body']) as $delivery) {
                $batchIds[] = $delivery['batchId'];
                $uuids[] = $delivery['packet']['uuid'];
            }
        }

        Assert::same(
            count($batchIds),
            count(array_unique($batchIds)),
            'two concurrent readers must never both be handed the same batch'
        );
        Assert::same(
            count($uuids),
            count(array_unique($uuids)),
            'and therefore never the same packet'
        );

        sort($sent);
        $seen = array_unique($uuids);
        sort($seen);
        Assert::same($sent, $seen, 'together the readers still saw everything exactly once');
        Assert::lessOrEqual(2, $refused, 'lock contention must stay rare at this volume');
    });

    $suite->test('a reader never observes a partially written batch', function () use ($drain) {
        $instance = Harness::instance([], 4);

        $requests = [];
        for ($i = 0; $i < 16; $i++) {
            if ($i % 2 === 0) {
                $lines = [];
                for ($j = 0; $j < 20; $j++) {
                    $lines[] = SignRequest::packet(
                        Harness::CLIENT_A,
                        Harness::CLIENT_B,
                        str_repeat('L', 900),
                        SignRequest::uuid()
                    );
                }
                $requests[] = Vpn::requestFor($instance, Harness::CLIENT_A, $lines, $i % 4);
            } else {
                $requests[] = Vpn::requestFor($instance, Harness::CLIENT_B, [], $i % 4);
            }
        }

        foreach (HttpClient::parallel($requests, 60.0) as $index => $response) {
            Assert::true(
                in_array($response['status'], [200, 429, 503], true),
                'request ' . $index . ' must end in a controlled way, got ' . $response['status']
            );
            if ($response['status'] !== 200) {
                continue;
            }
            foreach (SignRequest::deliveries($response['body']) as $delivery) {
                Assert::same(
                    900,
                    strlen((string) base64_decode($delivery['packet']['data'], true)),
                    'every delivered packet must be complete'
                );
            }
            Assert::same(
                "}\n",
                substr($response['body'], -2),
                'a response always ends with a complete sync-result line'
            );
        }

        $drain($instance, Harness::CLIENT_B);
        Assert::count(0, $instance->queueFiles(Harness::CLIENT_B, '.writing'), 'no partial write survives');
    });

    $suite->test('parallel traffic between many pairs stays isolated', function () use ($drain) {
        $instance = Harness::instance([], 4);

        $requests = [];
        $expected = [Harness::CLIENT_B => [], Harness::CLIENT_C => []];
        foreach ([Harness::CLIENT_B, Harness::CLIENT_C] as $destination) {
            for ($i = 0; $i < 10; $i++) {
                $uuid = SignRequest::uuid();
                $expected[$destination][] = $uuid;
                $requests[] = Vpn::requestFor(
                    $instance,
                    Harness::CLIENT_A,
                    [SignRequest::packet(Harness::CLIENT_A, $destination, 'to ' . $destination, $uuid)],
                    count($requests) % 4
                );
            }
        }

        foreach (HttpClient::parallel($requests, 60.0) as $index => $response) {
            Vpn::resultOf($response, 'fan out ' . $index);
        }

        foreach ($expected as $destination => $uuids) {
            $delivered = array_map(
                static fn (array $delivery): string => $delivery['packet']['uuid'],
                $drain($instance, $destination)
            );
            sort($uuids);
            sort($delivered);
            Assert::same($uuids, $delivered, 'client ' . $destination . ' receives exactly its own packets');
        }

        Assert::same('', trim($instance->phpErrorLog()), 'concurrency must not produce PHP diagnostics');
    });
};

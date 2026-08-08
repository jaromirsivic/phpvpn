<?php

declare(strict_types=1);

return function (Suite $suite): void {
    loadServerInProcess();

    $suite->test('octetOf accepts only the client address range', function () {
        Assert::same(2, Wire::octetOf('10.99.99.2'), 'lowest client address');
        Assert::same(254, Wire::octetOf('10.99.99.254'), 'highest client address');
        Assert::same(14, Wire::octetOf('10.99.99.14'), 'ordinary client address');

        foreach ([
            '10.99.99.1', '10.99.99.255', '10.99.99.0', '10.99.99.', '10.99.99.04',
            '10.99.99.2 ', '10.99.99.2.1', '10.99.98.2', '10.99.99.-2', '10.99.99.1e1',
            '', '../../etc', '10.99.99.2/../003',
        ] as $rejected) {
            Assert::same(0, Wire::octetOf($rejected), 'must reject ' . var_export($rejected, true));
        }
    });

    $suite->test('base64 validation rejects anything that would need escaping', function () {
        Assert::true(Wire::isBase64(base64_encode('hello world!')), 'plain base64 is accepted');
        Assert::true(Wire::isBase64(base64_encode(random_bytes(64))), 'binary base64 is accepted');

        foreach (['', 'abc', 'ab$=', 'a"bc', "ab\\c", "ab\nc", 'ab c', 'YWJj=', '====', 'a=bc'] as $rejected) {
            Assert::false(Wire::isBase64($rejected), 'must reject ' . var_export($rejected, true));
        }
    });

    $suite->test('timestamps are parsed strictly and round trip', function () {
        $epoch = Wire::parseTimestamp('2026-08-08T14:20:00Z');
        Assert::same(1786198800, $epoch, 'known UTC instant');
        Assert::same('2026-08-08T14:20:00Z', Wire::isoTime((int) $epoch), 'round trip');

        foreach ([
            '2026-08-08 14:20:00', '2026-08-08T14:20:00', '2026-08-08T14:20:00+02:00',
            '2026-8-8T14:20:00Z', '', 'now',
        ] as $rejected) {
            Assert::same(null, Wire::parseTimestamp($rejected), 'must reject ' . var_export($rejected, true));
        }
    });

    $suite->test('decodeLine rejects lists, scalars and broken JSON', function () {
        $decoded = Wire::decodeLine('{"type":"ack","batchId":"x"}');
        Assert::same('ack', $decoded['type'], 'objects decode');

        foreach (['[1,2]', '"text"', '5', 'null', '{}', '{"a":', '{"a":{"b":{"c":{"d":1}}}}'] as $rejected) {
            $threw = false;
            try {
                Wire::decodeLine($rejected);
            } catch (HttpError $error) {
                $threw = true;
                Assert::same(400, $error->status, 'malformed input is a 400');
            }
            Assert::true($threw, 'must reject ' . var_export($rejected, true));
        }
    });

    $suite->test('delivery payload length matches the precomputed size', function () {
        $fragments = [];
        for ($i = 0; $i < 25; $i++) {
            $fragments[] = Wire::buildPacketFragment(
                SignRequest::uuid(),
                '10.99.99.2',
                '10.99.99.4',
                base64_encode(random_bytes(($i + 1) * 37))
            );
        }

        $expected = 0;
        foreach ($fragments as $fragment) {
            $expected += strlen($fragment) + Wire::deliveryOverhead();
        }

        $id = Batch::newId($expected);
        $payload = Wire::buildBatchPayload($id, $fragments);

        Assert::same($expected, strlen($payload), 'the size embedded in a batch id must be exact');
        Assert::same(25, substr_count($payload, "\n"), 'one line per packet');
    });

    $suite->test('generated delivery lines are valid JSON', function () {
        $fragment = Wire::buildPacketFragment(
            '550e8400-e29b-41d4-a716-446655440000',
            '10.99.99.2',
            '10.99.99.4',
            base64_encode("binary \x00\x01\x02 payload")
        );
        $id = Batch::newId(strlen($fragment) + Wire::deliveryOverhead());
        $line = trim(Wire::buildBatchPayload($id, [$fragment]));

        $decoded = json_decode($line, true);
        Assert::true(is_array($decoded), 'the delivery line must decode');
        Assert::same('delivery', $decoded['type'], 'type');
        Assert::same($id, $decoded['batchId'], 'batch id');
        Assert::same('10.99.99.4', $decoded['packet']['dst'], 'destination survives');
    });

    $suite->test('sync-result is well formed with and without uuids', function () {
        $empty = Wire::buildSyncResult([], [], 0, 0, false, 65536, 100);
        $decoded = json_decode($empty, true);
        Assert::true(is_array($decoded), 'empty result decodes');
        Assert::count(0, $decoded['acceptedPacketUuids'], 'no accepted uuids');
        Assert::same(false, $decoded['responseTruncated'], 'not truncated');

        $uuids = [SignRequest::uuid(), SignRequest::uuid()];
        $filled = json_decode(Wire::buildSyncResult($uuids, [$uuids[0]], 2, 42, true, 1024, 8), true);
        Assert::same($uuids, $filled['acceptedPacketUuids'], 'accepted uuids survive');
        Assert::count(1, $filled['deferredPacketUuids'], 'deferred uuids survive');
        Assert::same(true, $filled['responseTruncated'], 'truncation flag');
    });
};

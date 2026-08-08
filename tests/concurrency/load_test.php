<?php

/**
 * Sustained load, matching the scenario in design2.md section 12.8: dozens of
 * clients polling the same server through the whole cycle of send, receive and
 * acknowledge.
 *
 * Timings on a developer machine are not a benchmark, so nothing here asserts
 * an absolute rate. What is asserted is that nothing is lost, nothing is
 * duplicated, nothing accumulates on disk and no request ends uncontrolled.
 */

declare(strict_types=1);

return function (Suite $suite): void {
    $suite->test('an idle client costs almost nothing and leaves no residue', function () {
        $instance = Harness::instance();
        $rounds = 120;

        $startedAt = microtime(true);
        for ($i = 0; $i < $rounds; $i++) {
            $response = Vpn::sync($instance, Harness::CLIENT_A);
            Assert::same(200, $response['status'], 'idle poll ' . $i);
        }
        $elapsed = microtime(true) - $startedAt;

        printf(
            "          %d idle polls in %.2fs (%.0f req/s including client overhead)\n",
            $rounds,
            $elapsed,
            $rounds / $elapsed
        );

        Assert::count(0, $instance->queueFiles(Harness::CLIENT_A), 'an idle queue stays completely empty');
        Assert::same('', trim($instance->phpErrorLog()), 'no diagnostics under sustained polling');
    });

    $suite->test('a mesh of twenty clients loses nothing over many cycles', function () {
        $clients = Harness::clients(20);
        $addresses = array_keys($clients);
        $instance = Harness::instance(['ACCESS_TOKENS' => Harness::tokenLiteral($clients)], 4);

        /** @var array<string, list<string>> $expected destination => uuids */
        $expected = [];
        foreach ($addresses as $address) {
            $expected[$address] = [];
        }

        $received = array_fill_keys($addresses, []);
        $acks = array_fill_keys($addresses, []);

        /**
         * A sync is bidirectional, so the very request that sends packets also
         * carries whatever was waiting for that client. Harvesting both halves
         * of every response is the only way to account for all traffic.
         */
        $harvest = static function (array $responses, string $context) use (
            $addresses,
            &$received,
            &$acks
        ): void {
            foreach ($addresses as $index => $address) {
                $response = $responses[$index];
                Assert::same(200, $response['status'], $context . ' for ' . $address);
                $acks[$address] = [];
                foreach (SignRequest::deliveries($response['body']) as $delivery) {
                    $received[$address][] = $delivery['packet']['uuid'];
                    $acks[$address][] = SignRequest::ack($delivery['batchId']);
                }
            }
        };

        $rounds = 4;
        for ($round = 0; $round < $rounds; $round++) {
            $requests = [];
            foreach ($addresses as $index => $sender) {
                $lines = $acks[$sender];
                for ($packet = 0; $packet < 3; $packet++) {
                    $destination = $addresses[($index + 1 + $packet) % count($addresses)];
                    $uuid = SignRequest::uuid();
                    $expected[$destination][] = $uuid;
                    $lines[] = SignRequest::packet($sender, $destination, "r{$round}", $uuid);
                }
                $requests[] = Vpn::requestFor($instance, $sender, $lines, $index % 4);
            }

            $responses = HttpClient::parallel($requests, 90.0);
            foreach ($responses as $index => $response) {
                $result = Vpn::resultOf($response, "round {$round} sender {$index}");
                Assert::count(3, $result['acceptedPacketUuids'], 'every packet of the round is stored');
            }
            $harvest($responses, "round {$round}");
        }

        for ($cycle = 0; $cycle < 12; $cycle++) {
            $requests = [];
            foreach ($addresses as $index => $address) {
                $requests[] = Vpn::requestFor($instance, $address, $acks[$address], $index % 4);
            }
            $responses = HttpClient::parallel($requests, 90.0);

            $before = 0;
            foreach ($received as $list) {
                $before += count($list);
            }
            $harvest($responses, "drain cycle {$cycle}");
            $after = 0;
            foreach ($received as $list) {
                $after += count($list);
            }

            $pending = false;
            foreach ($acks as $list) {
                $pending = $pending || $list !== [];
            }
            if ($after === $before && !$pending) {
                break;
            }
        }

        $totalExpected = 0;
        foreach ($addresses as $address) {
            $want = $expected[$address];
            $got = $received[$address];
            sort($want);
            sort($got);

            $totalExpected += count($want);
            Assert::same(count($got), count(array_unique($got)), $address . ' received no duplicate');
            Assert::same($want, $got, $address . ' received exactly what was addressed to it');
        }
        Assert::same($rounds * count($addresses) * 3, $totalExpected, 'the test really moved that many packets');

        foreach ($addresses as $address) {
            Assert::count(0, $instance->queueFiles($address), 'queue of ' . $address . ' is drained clean');
        }
        Assert::same('', trim($instance->phpErrorLog()), 'no diagnostics under mesh load');
    });
};

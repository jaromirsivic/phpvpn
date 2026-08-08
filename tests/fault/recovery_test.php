<?php

/**
 * Crash and corruption recovery.
 *
 * There is no cron, so every artefact left behind by a killed worker has to be
 * cleaned up by ordinary traffic. Each test plants the exact debris a crash at
 * one specific point would leave and then checks that traffic keeps flowing.
 */

declare(strict_types=1);

return function (Suite $suite): void {
    /** A syntactically valid batch id carrying a given payload size and age. */
    $batchId = static function (int $bytes, int $secondsAgo = 0): string {
        return gmdate('Ymd\THis', time() - $secondsAgo)
            . '.000000Z_' . bin2hex(random_bytes(8))
            . '.' . sprintf('%09d', $bytes);
    };

    /** Writes a well formed batch file directly into a queue, as the server would. */
    $plantBatch = static function (
        Instance $instance,
        string $destination,
        string $suffix,
        string $payload,
        ?int $ageSeconds = null
    ) use ($batchId): array {
        $dir = $instance->queueDir($destination);
        if (!is_dir($dir) && !mkdir($dir, 0700, true) && !is_dir($dir)) {
            throw new RuntimeException('Cannot create ' . $dir);
        }

        $uuid = SignRequest::uuid();
        $fragment = '{"type":"data","uuid":"' . $uuid . '","src":"' . Harness::CLIENT_A
            . '","dst":"' . $destination . '","data":"' . base64_encode($payload) . '"}';
        // 30 + 50 + 11 + 2 is the fixed delivery envelope around a fragment.
        $size = 30 + 50 + 11 + strlen($fragment) + 2;
        $id = $batchId($size);
        $path = $dir . '/' . $id . '.' . $suffix;

        file_put_contents(
            $path,
            '{"type":"delivery","batchId":"' . $id . '","packet":' . $fragment . "}\n"
        );
        if ($ageSeconds !== null) {
            touch($path, time() - $ageSeconds);
        }
        @unlink($dir . '/queue.stat');

        return ['id' => $id, 'uuid' => $uuid, 'path' => $path, 'size' => $size];
    };

    $suite->test('a crash before the rename leaves no visible batch', function () use ($plantBatch) {
        $instance = Harness::instance();
        $orphan = $plantBatch($instance, Harness::CLIENT_B, 'writing', 'never published', 300);

        $response = Vpn::sync($instance, Harness::CLIENT_B);
        Assert::same(200, $response['status'], 'the queue still serves traffic');
        Assert::count(0, SignRequest::deliveries($response['body']), 'a half written batch is never delivered');
        Assert::false(is_file($orphan['path']), 'and it is cleaned up');
    });

    $suite->test('a crash right after the rename still delivers the batch', function () use ($plantBatch) {
        $instance = Harness::instance();
        $survivor = $plantBatch($instance, Harness::CLIENT_B, 'data', 'published just in time');

        $deliveries = SignRequest::deliveries(Vpn::sync($instance, Harness::CLIENT_B)['body']);
        Assert::count(1, $deliveries, 'a published batch survives a crash');
        Assert::same($survivor['uuid'], $deliveries[0]['packet']['uuid'], 'with its content intact');
    });

    $suite->test('a crash while streaming a response redelivers the batch', function () use ($plantBatch) {
        $instance = Harness::instance();
        $abandoned = $plantBatch($instance, Harness::CLIENT_B, 'sending', 'was in flight', 300);

        $deliveries = SignRequest::deliveries(Vpn::sync($instance, Harness::CLIENT_B)['body']);
        Assert::count(1, $deliveries, 'the abandoned claim is offered again');
        Assert::same($abandoned['id'], $deliveries[0]['batchId'], 'as the very same batch');
    });

    $suite->test('an expired batch is dropped rather than delivered late', function () use ($plantBatch) {
        $instance = Harness::instance();
        $expired = $plantBatch($instance, Harness::CLIENT_B, 'data', 'far too old', 3600);

        Assert::count(
            0,
            SignRequest::deliveries(Vpn::sync($instance, Harness::CLIENT_B)['body']),
            'a batch past the TTL must not be delivered'
        );
        Assert::false(is_file($expired['path']), 'and it is removed');
    });

    $suite->test('foreign files in a queue directory never break delivery', function () use ($plantBatch) {
        $instance = Harness::instance();
        $good = $plantBatch($instance, Harness::CLIENT_B, 'data', 'still fine');

        $dir = $instance->queueDir(Harness::CLIENT_B);
        file_put_contents($dir . '/README.txt', 'someone poked around here');
        file_put_contents($dir . '/../../escape.data', 'not in a queue');
        file_put_contents($dir . '/nonsense.data', 'no valid batch id');
        file_put_contents($dir . '/' . str_repeat('x', 40) . '.000000123.data', 'wrong shape');
        @unlink($dir . '/queue.stat');

        $deliveries = SignRequest::deliveries(Vpn::sync($instance, Harness::CLIENT_B)['body']);
        Assert::count(1, $deliveries, 'only the real batch is delivered');
        Assert::same($good['uuid'], $deliveries[0]['packet']['uuid'], 'and it is the right one');
        Assert::true(is_file($dir . '/README.txt'), 'a non batch file is left alone');
    });

    $suite->test('a batch whose size disagrees with its name cannot corrupt a response', function () use ($plantBatch) {
        $instance = Harness::instance();
        $planted = $plantBatch($instance, Harness::CLIENT_B, 'data', 'honest content');
        file_put_contents($planted['path'], str_repeat("{\"junk\":1}\n", 50));

        $response = Vpn::sync($instance, Harness::CLIENT_B);
        Assert::same(200, $response['status'], 'the response is still produced');
        if (isset($response['headers']['content-length'])) {
            Assert::same(
                strlen($response['body']),
                (int) $response['headers']['content-length'],
                'Content-Length is measured from the real file, never from the name'
            );
        }
        Assert::same("}\n", substr($response['body'], -2), 'and the sync-result line still terminates it');
    });

    $suite->test('a deleted lock file is recreated', function () use ($plantBatch) {
        $instance = Harness::instance();
        $plantBatch($instance, Harness::CLIENT_B, 'data', 'after cleanup');
        @unlink($instance->queueDir(Harness::CLIENT_B) . '/queue.lock');

        Assert::count(
            1,
            SignRequest::deliveries(Vpn::sync($instance, Harness::CLIENT_B)['body']),
            'losing the lock file must not lose the queue'
        );
    });

    $suite->test('a deleted queue root is rebuilt on the next request', function () {
        $instance = Harness::instance();
        Vpn::sync($instance, Harness::CLIENT_A, [
            SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'before the wipe'),
        ]);

        Harness::removeTree($instance->queueRoot);
        Assert::false(is_dir($instance->queueRoot), 'the queue root really is gone');

        $result = Vpn::resultOf(
            Vpn::sync($instance, Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'after the wipe'),
            ]),
            'after wipe'
        );
        Assert::count(1, $result['acceptedPacketUuids'], 'the server rebuilds its storage');
        Assert::count(
            1,
            SignRequest::deliveries(Vpn::sync($instance, Harness::CLIENT_B)['body']),
            'and delivery works again'
        );
    });

    $suite->test('the queue root is protected against direct web access', function () {
        $instance = Harness::instance();
        Vpn::sync($instance, Harness::CLIENT_A);

        Assert::true(is_file($instance->queueRoot . '/.htaccess'), 'an Apache deny rule is written');
        Assert::contains(
            'Require all denied',
            (string) file_get_contents($instance->queueRoot . '/.htaccess'),
            'the deny rule must actually deny'
        );
        Assert::true(is_file($instance->queueRoot . '/index.html'), 'directory listings are blocked too');
    });

    $suite->test('an unusable queue root fails loudly but safely', function () {
        $blocker = Harness::tempRoot() . '/blocker-' . bin2hex(random_bytes(4));
        file_put_contents($blocker, 'this is a file, not a directory');

        $broken = Harness::instance(['QUEUE_ROOT' => var_export($blocker . '/queue', true)]);

        Assert::same(200, HttpClient::request($broken->url())['status'], 'the status page still answers');
        $response = Vpn::sync($broken, Harness::CLIENT_A);
        Assert::same(503, $response['status'], 'sync refuses rather than pretending to work');
        Assert::contains('queue_unavailable', $response['body'], 'with a generic reason');
        Assert::notContains($blocker, $response['body'], 'and without disclosing any path');
    });

    $suite->test('the global quota refuses new batches once it is reached', function () {
        $instance = Harness::instance();
        Vpn::sync($instance, Harness::CLIENT_A); // Creates the state directory.

        file_put_contents(
            $instance->queueRoot . '/_state/global.stat',
            "1\t999999\t999999999\t" . time() . "\n"
        );

        $response = Vpn::sync($instance, Harness::CLIENT_A, [
            SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'over quota'),
        ]);
        Assert::same(429, $response['status'], 'the installation wide ceiling is enforced');
        Assert::count(0, $instance->queueFiles(Harness::CLIENT_B, '.data'), 'and nothing is written');
    });

    $suite->test('a stale global quota fails open instead of blocking traffic', function () {
        $instance = Harness::instance();
        Vpn::sync($instance, Harness::CLIENT_A);

        file_put_contents(
            $instance->queueRoot . '/_state/global.stat',
            "1\t999999\t999999999\t" . (time() - 86400) . "\n"
        );

        $result = Vpn::resultOf(
            Vpn::sync($instance, Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'stale quota'),
            ]),
            'stale quota'
        );
        Assert::count(1, $result['acceptedPacketUuids'], 'an outdated aggregate must not stop the service');
    });

    $suite->test('a corrupted global quota file is ignored', function () {
        $instance = Harness::instance();
        Vpn::sync($instance, Harness::CLIENT_A);
        file_put_contents($instance->queueRoot . '/_state/global.stat', "garbage\x00\x01");

        $result = Vpn::resultOf(
            Vpn::sync($instance, Harness::CLIENT_A, [
                SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'corrupt quota'),
            ]),
            'corrupt quota'
        );
        Assert::count(1, $result['acceptedPacketUuids'], 'corruption must not stop the service');
    });

    $suite->test('recovery never produces PHP diagnostics', function () {
        $instance = Harness::instance();
        Harness::removeTree($instance->queueRoot);
        Vpn::sync($instance, Harness::CLIENT_A);
        Vpn::sync($instance, Harness::CLIENT_B);

        Assert::same('', trim($instance->phpErrorLog()), 'no warnings or notices during recovery');
    });
};

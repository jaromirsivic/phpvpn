<?php

/**
 * The stats action over real HTTP.
 *
 * Everything here goes through the wire format a real administrator would use,
 * and every decryption is done by the independent client in tests/lib, never by
 * the server's own code.
 */

declare(strict_types=1);

return function (Suite $suite): void {
    $instance = null;
    $get = static function () use (&$instance): Instance {
        return $instance ??= Harness::instance();
    };

    /** @return array{status:int,headers:array<string,string>,body:string,canonical:string} */
    $call = static function (
        Instance $target,
        array $options = [],
        string $contentType = Vpn::NDJSON,
        string $method = 'POST',
        ?string $secret = null
    ): array {
        $body = StatsClient::body($secret ?? Harness::SECRET, $options);
        $response = HttpClient::request(
            $target->url() . '?action=stats',
            $method,
            ['Content-Type' => $contentType],
            $body
        );
        $response['canonical'] = StatsClient::canonicalOf($body);

        return $response;
    };

    $statsPath = static function (Instance $target): string {
        return $target->queueRoot . '/stats.json';
    };

    /**
     * The record is written after the response has been flushed, so a test that
     * reads the file straight away would race the server.
     */
    $waitForStats = static function (Instance $target, string $client) use ($statsPath): array {
        $deadline = microtime(true) + 5.0;
        do {
            $raw = @file_get_contents($statsPath($target));
            $entries = is_string($raw) ? json_decode($raw, true) : null;
            if (is_array($entries) && isset($entries[$client])) {
                return $entries;
            }
            usleep(25000);
        } while (microtime(true) < $deadline);

        Assert::fail('the statistics record for ' . $client . ' never appeared');
    };

    $suite->test('stats returns an encrypted document to a caller that knows the secret', function () use ($call, $get) {
        $response = $call($get());

        Assert::same(200, $response['status'], 'a signed request must be answered');
        Assert::contains(
            'application/json',
            $response['headers']['content-type'] ?? '',
            'the envelope is JSON'
        );
        Assert::contains('no-store', $response['headers']['cache-control'] ?? '', 'never cache statistics');

        $decoded = json_decode($response['body'], true);
        Assert::same(
            ['alg', 'iv', 'ciphertext', 'tag'],
            array_keys((array) $decoded),
            'the documented field order'
        );
        Assert::same('AES-256-GCM', $decoded['alg'] ?? '', 'the announced algorithm');
        Assert::same(12, strlen((string) base64_decode((string) $decoded['iv'], true)), 'a 12 byte IV');
        Assert::same(16, strlen((string) base64_decode((string) $decoded['tag'], true)), 'a 16 byte tag');

        $plaintext = StatsClient::open($response['body'], Harness::SECRET, $response['canonical']);
        Assert::true(is_string($plaintext), 'the tag must verify and the payload must decrypt');
        Assert::true(is_array(json_decode((string) $plaintext, true)), 'the plaintext is a JSON object');
    });

    $suite->test('stats reports the last successful sync of every client', function () use ($call, $get, $waitForStats) {
        $target = $get();

        Assert::same(200, Vpn::sync($target, Harness::CLIENT_A)['status'], 'client A syncs');
        $waitForStats($target, Harness::CLIENT_A);
        Assert::same(200, Vpn::sync($target, Harness::CLIENT_B)['status'], 'client B syncs');
        $waitForStats($target, Harness::CLIENT_B);

        $response = $call($target);
        $plaintext = (string) StatsClient::open($response['body'], Harness::SECRET, $response['canonical']);
        $entries = (array) json_decode($plaintext, true);

        foreach ([Harness::CLIENT_A, Harness::CLIENT_B] as $client) {
            Assert::true(isset($entries[$client]), $client . ' must appear in the statistics');
            Assert::true(
                preg_match('/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/', (string) $entries[$client]) === 1,
                $client . ' must carry an ISO 8601 UTC time'
            );
            Assert::lessOrEqual(
                60,
                abs(time() - (int) strtotime((string) $entries[$client])),
                $client . ' must have been seen just now'
            );
        }
    });

    $suite->test('a sync inside the update window does not move the record', function () use ($call, $get, $waitForStats) {
        $fast = Harness::instance();

        Vpn::sync($fast, Harness::CLIENT_C);
        $first = $waitForStats($fast, Harness::CLIENT_C)[Harness::CLIENT_C];

        // Long enough that a second write would show a different second.
        usleep(1200000);
        Vpn::sync($fast, Harness::CLIENT_C);
        usleep(300000);

        $entries = $waitForStats($fast, Harness::CLIENT_C);
        Assert::same($first, $entries[Harness::CLIENT_C], 'the record is throttled, not rewritten every sync');
    });

    $suite->test('stats without valid authorisation is always a 401', function () use ($call, $get) {
        $target = $get();
        $refused = [
            'a wrong signature' => ['signature' => str_repeat('a', 64)],
            'a signature of the wrong length' => ['signature' => str_repeat('a', 63)],
            'a signature for the sync action' => ['action' => 'sync'],
            'a signature for the selftest action' => ['action' => 'selftest'],
            'a timestamp far in the past' => ['timestamp' => gmdate('Y-m-d\TH:i:s\Z', time() - 3600)],
            'a timestamp far in the future' => ['timestamp' => gmdate('Y-m-d\TH:i:s\Z', time() + 3600)],
            'a malformed timestamp' => ['timestamp' => '2026-08-08 14:20:00'],
            'a too short nonce' => ['nonce' => 'short'],
            'a nonce with forbidden characters' => ['nonce' => 'nonce with spaces...'],
        ];

        foreach ($refused as $what => $options) {
            $response = $call($target, $options);
            Assert::same(401, $response['status'], $what . ' must be refused');
            Assert::contains('unauthorized', $response['body'], $what . ': generic error body');
        }

        $foreign = $call($target, [], Vpn::NDJSON, 'POST', 'a-completely-different-server-secret');
        Assert::same(401, $foreign['status'], 'knowing another secret proves nothing');
    });

    $suite->test('a refused stats request never discloses a statistic', function () use ($call, $get, $waitForStats) {
        $target = $get();
        Vpn::sync($target, Harness::CLIENT_A);
        $waitForStats($target, Harness::CLIENT_A);

        foreach ([['signature' => str_repeat('b', 64)], ['nonce' => 'x'], ['timestamp' => 'nope']] as $options) {
            $response = $call($target, $options);

            Assert::notContains('10.99.99.', $response['body'], 'no address may appear in an error');
            Assert::notContains('202', $response['body'], 'no timestamp may appear in an error');
            Assert::notContains(Harness::SECRET, $response['body'], 'the secret may never appear');
            Assert::notContains('stats.json', $response['body'], 'no path may be disclosed');
            Assert::same(
                '{"status":"error","error":"unauthorized"}',
                $response['body'],
                'the body stays the small generic document'
            );
        }
    });

    $suite->test('a statistics response is never repeatable', function () use ($call, $get) {
        $target = $get();
        $first = json_decode($call($target)['body'], true);
        $second = json_decode($call($target)['body'], true);

        Assert::notSame($first['iv'], $second['iv'], 'each response gets its own IV');
        Assert::notSame($first['ciphertext'], $second['ciphertext'], 'and therefore its own ciphertext');
        Assert::notSame($first['tag'], $second['tag'], 'and its own tag');
    });

    $suite->test('a tampered statistics response yields nothing at all', function () use ($call, $get, $waitForStats) {
        $target = $get();
        Vpn::sync($target, Harness::CLIENT_A);
        $waitForStats($target, Harness::CLIENT_A);

        $response = $call($target);
        $envelope = (array) json_decode($response['body'], true);
        $canonical = $response['canonical'];

        Assert::true(
            is_string(StatsClient::open($response['body'], Harness::SECRET, $canonical)),
            'the untouched response opens'
        );

        $flip = static function (string $base64, int $offset): string {
            $raw = (string) base64_decode($base64, true);
            $raw[$offset] = chr(ord($raw[$offset]) ^ 0x01);

            return base64_encode($raw);
        };

        $tampered = [
            'the ciphertext' => [['ciphertext' => $flip((string) $envelope['ciphertext'], 2)], $canonical],
            'the tag' => [['tag' => $flip((string) $envelope['tag'], 0)], $canonical],
            'the IV' => [['iv' => $flip((string) $envelope['iv'], 11)], $canonical],
            'the additional data' => [[], $canonical . 'x'],
        ];

        foreach ($tampered as $what => [$override, $aad]) {
            $body = (string) json_encode(array_merge($envelope, $override));
            Assert::same(
                null,
                StatsClient::open($body, Harness::SECRET, $aad),
                'a change to ' . $what . ' must leave the client with no plaintext'
            );
        }
    });

    $suite->test('stats rejects a malformed request before it decides anything', function () use ($call, $get) {
        $target = $get();
        $url = $target->url() . '?action=stats';

        $wrongType = $call($target, [], 'application/json');
        Assert::same(415, $wrongType['status'], 'only the NDJSON content types are accepted');

        $alias = $call($target, [], 'application/jsonl; charset=utf-8');
        Assert::same(200, $alias['status'], 'the documented alias works here too');

        $empty = HttpClient::request($url, 'POST', ['Content-Type' => Vpn::NDJSON], '');
        Assert::same(400, $empty['status'], 'an empty body has no authorisation record');

        $broken = HttpClient::request($url, 'POST', ['Content-Type' => Vpn::NDJSON], "not json\n");
        Assert::same(400, $broken['status'], 'a line that is not JSON is a protocol error');

        $wrongType2 = HttpClient::request(
            $url,
            'POST',
            ['Content-Type' => Vpn::NDJSON],
            '{"type":"auth","timestamp":"' . gmdate('Y-m-d\TH:i:s\Z') . '","nonce":"nonce-value-1234","signature":"'
            . str_repeat('c', 64) . '"}' . "\n"
        );
        Assert::same(400, $wrongType2['status'], 'the sync auth record is not a stats authorisation');

        $extra = $call($target, ['extra' => '{"type":"ack","batchId":"x"}' . "\n"]);
        Assert::same(400, $extra['status'], 'the signature covers no body, so a second line is refused');

        $extraUnsigned = $call($target, [
            'extra' => '{"type":"ack"}' . "\n",
            'signature' => str_repeat('d', 64),
        ]);
        Assert::same(
            401,
            $extraUnsigned['status'],
            'an unauthenticated caller learns nothing but 401, even from a broken body'
        );
    });

    $suite->test('stats is POST only', function () use ($get) {
        foreach (['GET', 'HEAD'] as $method) {
            $response = HttpClient::request($get()->url() . '?action=stats', $method);
            Assert::same(405, $response['status'], $method . ' must be refused');
            Assert::same('POST', $response['headers']['allow'] ?? '', 'the Allow header names POST');
        }
    });

    $suite->test('neither stats nor health touches the statistics file', function () use ($call, $get, $statsPath, $waitForStats) {
        $target = $get();
        Vpn::sync($target, Harness::CLIENT_A);
        $waitForStats($target, Harness::CLIENT_A);

        $path = $statsPath($target);
        $before = (string) file_get_contents($path);
        touch($path, time() - 600);
        clearstatcache(true, $path);
        $modified = filemtime($path);

        Assert::same(200, $call($target)['status'], 'stats answers');
        Assert::same(200, HttpClient::request($target->url() . '?action=health')['status'], 'health answers');
        usleep(400000);
        clearstatcache(true, $path);

        Assert::same($before, (string) file_get_contents($path), 'the content is untouched');
        Assert::same($modified, filemtime($path), 'and so is the modification time');
        Assert::count(
            0,
            glob($target->queueRoot . '/stats.json.*') ?: [],
            'no temporary statistics file is left behind'
        );
    });

    $suite->test('a replayed stats request is refused when nonces are persisted', function () {
        $strict = Harness::instance(['PERSIST_NONCES' => 'true']);
        $body = StatsClient::body(Harness::SECRET);
        $url = $strict->url() . '?action=stats';

        $first = HttpClient::request($url, 'POST', ['Content-Type' => Vpn::NDJSON], $body);
        Assert::same(200, $first['status'], 'the first use of a nonce is fine');

        $second = HttpClient::request($url, 'POST', ['Content-Type' => Vpn::NDJSON], $body);
        Assert::same(401, $second['status'], 'the second use of the same nonce is a replay');

        $fresh = HttpClient::request($url, 'POST', ['Content-Type' => Vpn::NDJSON], StatsClient::body(Harness::SECRET));
        Assert::same(200, $fresh['status'], 'a fresh nonce still works');
    });

    $suite->test('a misconfigured server refuses stats without leaking why', function () {
        $broken = Harness::instance(['SERVER_SECRET' => var_export('too-short', true)]);
        $response = HttpClient::request(
            $broken->url() . '?action=stats',
            'POST',
            ['Content-Type' => Vpn::NDJSON],
            StatsClient::body('too-short')
        );

        Assert::same(503, $response['status'], 'a short secret cannot protect anything');
        Assert::contains('server_misconfigured', $response['body'], 'and says so generically');
    });

    $suite->test('the log keeps no secret, nonce, signature or ciphertext', function () {
        $chatty = Harness::instance(['LOG_LEVEL' => var_export('info', true)]);
        $nonce = 'log-probe-nonce-0123456789';
        $body = StatsClient::body(Harness::SECRET, ['nonce' => $nonce]);
        $canonical = StatsClient::canonicalOf($body);

        Vpn::sync($chatty, Harness::CLIENT_A);
        $response = HttpClient::request(
            $chatty->url() . '?action=stats',
            'POST',
            ['Content-Type' => Vpn::NDJSON],
            $body
        );
        Assert::same(200, $response['status'], 'the probe request must succeed');
        usleep(400000);

        $log = '';
        foreach (glob($chatty->queueRoot . '/_logs/*.log') ?: [] as $file) {
            $log .= (string) file_get_contents($file);
        }
        Assert::contains('"category":"stats"', $log, 'the call itself is worth an info record');

        $envelope = (array) json_decode($response['body'], true);
        $forbidden = [
            'the server secret' => Harness::SECRET,
            'the nonce' => $nonce,
            'the signature' => (string) json_decode(strstr($body, "\n", true) ?: '{}', true)['signature'],
            'the derived key' => bin2hex(StatsClient::deriveKey(Harness::SECRET, $canonical)),
            'the IV' => (string) $envelope['iv'],
            'the ciphertext' => (string) $envelope['ciphertext'],
            'the tag' => (string) $envelope['tag'],
        ];
        foreach ($forbidden as $what => $value) {
            Assert::notContains($value, $log, $what . ' must never reach the log');
        }
    });

    $suite->test('the stats suite produced no PHP diagnostics', function () use ($get) {
        Assert::same('', trim($get()->phpErrorLog()), 'the server must run without warnings or notices');
    });
};

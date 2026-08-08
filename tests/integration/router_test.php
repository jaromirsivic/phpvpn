<?php

declare(strict_types=1);

return function (Suite $suite): void {
    $instance = null;
    $get = static function () use (&$instance): Instance {
        return $instance ??= Harness::instance();
    };

    $suite->test('the default page reports that the server is running', function () use ($get) {
        $response = HttpClient::request($get()->url());

        Assert::same(200, $response['status'], 'the status page must be reachable');
        Assert::contains('<h1>Server is running</h1>', $response['body'], 'required heading');
        Assert::contains('text/html', $response['headers']['content-type'] ?? '', 'HTML content type');
        Assert::contains('no-store', $response['headers']['cache-control'] ?? '', 'responses must not be cached');
        Assert::same('nosniff', $response['headers']['x-content-type-options'] ?? '', 'nosniff header');
    });

    $suite->test('a weak secret is called out in red on the status page', function () {
        $weak = Harness::instance(['SERVER_SECRET' => var_export('too-short', true)]);
        $response = HttpClient::request($weak->url());

        Assert::contains('<h1>Server is running</h1>', $response['body'], 'the page still renders');
        Assert::contains('SERVER_SECRET is not configured correctly', $response['body'], 'warning text');
        Assert::contains('color:#c00', $response['body'], 'the warning must be red');
    });

    $suite->test('health returns the authoritative UTC time', function () use ($get) {
        $response = HttpClient::request($get()->url() . '?action=health');

        Assert::same(200, $response['status'], 'health must answer');
        $decoded = json_decode($response['body'], true);
        Assert::same('OK', $decoded['status'] ?? '', 'status field');
        Assert::true(
            preg_match('/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/', (string) ($decoded['serverTime'] ?? '')) === 1,
            'serverTime must be ISO 8601 UTC with a Z suffix'
        );
        Assert::lessOrEqual(5, abs(time() - (int) strtotime((string) $decoded['serverTime'])), 'clock agreement');
    });

    $suite->test('health does not leak configuration', function () use ($get) {
        $response = HttpClient::request($get()->url() . '?action=health', 'POST');

        Assert::same(200, $response['status'], 'health also accepts POST');
        Assert::notContains(Harness::SECRET, $response['body'], 'the secret must never appear');
        Assert::notContains('10.99.99.', $response['body'], 'no client address may be disclosed');
        Assert::notContains('queue', strtolower($response['body']), 'no queue detail may be disclosed');
    });

    $suite->test('an unknown action is a 404', function () use ($get) {
        $response = HttpClient::request($get()->url() . '?action=nope');

        Assert::same(404, $response['status'], 'unknown actions must 404');
        Assert::contains('not_found', $response['body'], 'generic error body');
    });

    $suite->test('a wrong method is a 405 with an Allow header', function () use ($get) {
        $sync = HttpClient::request($get()->url() . '?action=sync', 'GET');
        Assert::same(405, $sync['status'], 'sync is POST only');
        Assert::same('POST', $sync['headers']['allow'] ?? '', 'Allow header');

        $page = HttpClient::request($get()->url(), 'POST');
        Assert::same(405, $page['status'], 'the status page is GET only');
        Assert::contains('GET', $page['headers']['allow'] ?? '', 'Allow header lists GET');
    });

    $suite->test('sync refuses a wrong content type', function () use ($get) {
        $response = Vpn::sync($get(), Harness::CLIENT_A, [], [], 0, 'application/json');

        Assert::same(415, $response['status'], 'only NDJSON content types are accepted');
    });

    $suite->test('sync accepts the application/jsonl alias', function () use ($get) {
        $response = Vpn::sync($get(), Harness::CLIENT_A, [], [], 0, 'application/jsonl; charset=utf-8');

        Assert::same(200, $response['status'], 'the documented alias must work');
    });

    $suite->test('selftest stays disabled by default', function () use ($get) {
        $body = SignRequest::body(
            Harness::CLIENT_A,
            Harness::TOKENS[Harness::CLIENT_A],
            Harness::SECRET,
            'selftest'
        );
        $response = HttpClient::request(
            $get()->url() . '?action=selftest',
            'POST',
            ['Content-Type' => Vpn::NDJSON],
            $body
        );

        Assert::same(404, $response['status'], 'a disabled endpoint must be indistinguishable from a missing one');
    });

    $suite->test('an invalid configuration refuses sync but keeps the page up', function () {
        $broken = Harness::instance(['SERVER_SECRET' => var_export('short', true)]);

        Assert::same(200, HttpClient::request($broken->url())['status'], 'the page stays available');
        $response = Vpn::sync($broken, Harness::CLIENT_A);
        Assert::same(503, $response['status'], 'sync must refuse to run misconfigured');
        Assert::contains('server_misconfigured', $response['body'], 'and say so generically');
    });

    $suite->test('the transport never changes the answer', function () use ($get) {
        // One signed body replayed with different proxy headers. Nonces are off
        // by default, so the only thing that may differ is the transport, and
        // the transport must be invisible to the server. The success path and a
        // failure path are both checked: neither may become reachable, or
        // unreachable, because of how the request arrived.
        $signed = SignRequest::body(
            Harness::CLIENT_A,
            Harness::tokenFor(Harness::CLIENT_A),
            Harness::SECRET,
            'sync'
        );
        $tampered = $signed . SignRequest::packet(Harness::CLIENT_A, Harness::CLIENT_B, 'injected') . "\n";
        $bodies = [
            'a valid sync' => [200, $signed],
            'a tampered sync' => [401, $tampered],
        ];
        $transports = [
            'plain HTTP' => [],
            'a proxy reporting http' => ['X-Forwarded-Proto' => 'http'],
            'a proxy reporting https' => ['X-Forwarded-Proto' => 'https'],
            'a proxy reporting several hops' => [
                'X-Forwarded-Proto' => 'https, http',
                'X-Forwarded-For' => '203.0.113.7',
                'X-Forwarded-Port' => '443',
            ],
        ];
        $url = $get()->url() . '?action=sync';

        foreach ($bodies as $what => [$expected, $body]) {
            $reference = null;
            foreach ($transports as $how => $extra) {
                $answer = HttpClient::request($url, 'POST', ['Content-Type' => Vpn::NDJSON] + $extra, $body);
                $reference ??= $answer;

                Assert::same($expected, $answer['status'], $what . ' over ' . $how);
                Assert::same($reference['body'], $answer['body'], $what . ' over ' . $how . ' must answer identically');
            }
        }
    });

    $suite->test('no PHP diagnostics leak into any response', function () use ($get) {
        Assert::same('', trim($get()->phpErrorLog()), 'the server must run without warnings or notices');
    });
};

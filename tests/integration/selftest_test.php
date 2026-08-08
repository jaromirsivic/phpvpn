<?php

/**
 * The preflight endpoint exists to answer, on the real hosting, the questions
 * problems.md raises before production traffic is trusted to it.
 */

declare(strict_types=1);

return function (Suite $suite): void {
    $instance = null;
    $get = static function () use (&$instance): Instance {
        return $instance ??= Harness::instance(['SELFTEST_ENABLED' => 'true']);
    };

    $run = static function (Instance $instance, ?string $token = null): array {
        $body = SignRequest::body(
            Harness::CLIENT_A,
            $token ?? Harness::tokenFor(Harness::CLIENT_A),
            Harness::SECRET,
            'selftest'
        );

        return HttpClient::request(
            $instance->url() . '?action=selftest',
            'POST',
            ['Content-Type' => Vpn::NDJSON],
            $body
        );
    };

    /** @return array<string,array{result:string,detail:string}> */
    $checksOf = static function (array $response): array {
        Assert::same(200, $response['status'], 'the preflight must answer');
        $decoded = json_decode($response['body'], true);
        Assert::true(is_array($decoded) && isset($decoded['checks']), 'the reply must contain checks');

        $byName = [];
        foreach ($decoded['checks'] as $check) {
            $byName[$check['name']] = ['result' => $check['result'], 'detail' => $check['detail']];
        }

        return $byName;
    };

    $suite->test('the preflight requires the same signature as sync', function () use ($get, $run) {
        Assert::same(
            401,
            $run($get(), str_repeat('ab', 32))['status'],
            'an unsigned preflight must be refused'
        );
        Assert::same(
            405,
            HttpClient::request($get()->url() . '?action=selftest')['status'],
            'and it is POST only'
        );
    });

    $suite->test('the preflight confirms the runtime baseline', function () use ($get, $run, $checksOf) {
        $checks = $checksOf($run($get()));

        foreach (['php_version', 'sha256', 'csprng', 'configuration', 'required_functions'] as $name) {
            Assert::true(isset($checks[$name]), 'check ' . $name . ' must be reported');
            Assert::same('pass', $checks[$name]['result'], $name . ': ' . $checks[$name]['detail']);
        }
    });

    $suite->test('the preflight proves the storage primitives really work', function () use ($get, $run, $checksOf) {
        $checks = $checksOf($run($get()));

        Assert::same('pass', $checks['queue_root']['result'], 'the queue root must be writable');
        Assert::same('pass', $checks['atomic_publish']['result'], 'create, write, rename and unlink must work');
        Assert::same('pass', $checks['flock']['result'], 'advisory locking must work');
        Assert::contains(
            'two worker',
            $checks['flock']['detail'],
            'the reply must admit that a single process cannot prove mutual exclusion'
        );
    });

    $suite->test('the preflight reports the hosting limits it depends on', function () use ($get, $run, $checksOf) {
        $checks = $checksOf($run($get()));

        Assert::same('pass', $checks['post_max_size']['result'], 'MAX_REQUEST_BYTES must fit under post_max_size');
        Assert::true(isset($checks['memory_limit']), 'memory_limit is reported');
        Assert::true(isset($checks['max_execution_time']), 'max_execution_time is reported');
        Assert::true(isset($checks['disable_functions']), 'disable_functions is reported');
        Assert::true(isset($checks['optional_functions']), 'optional functions are reported');
        Assert::same(
            'pass',
            $checks['optional_functions']['result'],
            'a missing optional function is a note, never a failure'
        );
    });

    $suite->test('the preflight fails the run when HTTPS is missing', function () use ($get, $run, $checksOf) {
        $response = $run($get());
        $checks = $checksOf($response);
        $decoded = json_decode($response['body'], true);

        Assert::same('fail', $checks['https']['result'], 'plain HTTP must be reported as a failure');
        Assert::same('FAILED', $decoded['status'], 'and one failed check fails the whole preflight');
        Assert::greaterOrEqual(1, $decoded['failed'], 'the failure count is reported');
    });

    $suite->test('the preflight notices a queue root inside the document root', function () {
        $inside = Harness::instance([
            'SELFTEST_ENABLED' => 'true',
            'QUEUE_ROOT' => "'{{DOCROOT}}/queue'",
        ]);

        $body = SignRequest::body(
            Harness::CLIENT_A,
            Harness::tokenFor(Harness::CLIENT_A),
            Harness::SECRET,
            'selftest'
        );
        $response = HttpClient::request(
            $inside->url() . '?action=selftest',
            'POST',
            ['Content-Type' => Vpn::NDJSON],
            $body
        );

        $decoded = json_decode($response['body'], true);
        $result = '';
        $detail = '';
        foreach ($decoded['checks'] as $check) {
            if ($check['name'] === 'queue_outside_document_root') {
                $result = $check['result'];
                $detail = $check['detail'];
            }
        }

        Assert::notSame('pass', $result, 'a queue inside the document root must not be reported as fine');
        Assert::contains('document root', $detail, 'and the reason must be explained');
    });

    $suite->test('the preflight discloses nothing sensitive', function () use ($get, $run) {
        $body = $run($get())['body'];

        Assert::notContains(Harness::SECRET, $body, 'the server secret must never appear');
        Assert::notContains(Harness::tokenFor(Harness::CLIENT_A), $body, 'no access token may appear');
    });
};

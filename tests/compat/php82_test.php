<?php

/**
 * Static guards over phpvpn.php itself.
 *
 * The deployment target is a typical hosting on PHP 8.2, while this machine
 * runs 8.5. Nothing here executes the server, it only proves the source stays
 * inside the promised language and extension baseline.
 */

declare(strict_types=1);

return function (Suite $suite): void {
    $source = (string) file_get_contents(Harness::projectRoot() . '/phpvpn.php');

    /** Functions and syntax introduced after PHP 8.2. */
    $tooNew = [
        'json_validate' => '8.3',
        'mb_str_pad' => '8.3',
        'str_increment' => '8.3',
        'str_decrement' => '8.3',
        'array_find' => '8.4',
        'array_find_key' => '8.4',
        'array_any' => '8.4',
        'array_all' => '8.4',
        'request_parse_body' => '8.4',
        'mb_trim' => '8.4',
        'fpow' => '8.4',
        'array_first' => '8.5',
        'array_last' => '8.5',
        'get_error_handler' => '8.5',
        'get_exception_handler' => '8.5',
    ];

    /** Extensions the design forbids requiring. */
    $forbiddenPrefixes = [
        'mb_', 'curl_', 'openssl_', 'socket_', 'apcu_', 'apc_', 'gmp_', 'bc',
        'mysqli_', 'sqlite_', 'pg_', 'ldap_', 'ftp_', 'imap_', 'gd_', 'imagick',
        'ffi_', 'shmop_', 'sem_', 'msg_', 'xml_', 'simplexml_', 'iconv_',
    ];

    $suite->test('source uses no function newer than PHP 8.2', function () use ($source, $tooNew) {
        foreach ($tooNew as $function => $version) {
            Assert::false(
                preg_match('/\b' . preg_quote($function, '/') . '\s*\(/', $source) === 1,
                $function . '() was introduced in PHP ' . $version
            );
        }
    });

    $suite->test('source uses no syntax newer than PHP 8.2', function () use ($source) {
        Assert::notContains('#[\Override]', $source, 'the Override attribute needs PHP 8.3');
        Assert::false(
            preg_match('/\b(public|private|protected)\s+(readonly\s+)?[a-z]+\s+const\s/i', $source) === 1,
            'typed class constants need PHP 8.3'
        );
        Assert::false(
            preg_match('/\b(public|private|protected)\([sg]et\)/', $source) === 1,
            'asymmetric visibility needs PHP 8.4'
        );
        Assert::notContains('|>', $source, 'the pipe operator needs PHP 8.5');
    });

    $suite->test('source calls no optional extension', function () use ($source, $forbiddenPrefixes) {
        preg_match_all('/(?<![\$>:\w])([a-z_][a-z0-9_]*)\s*\(/i', $source, $matches);
        $called = array_unique($matches[1]);
        foreach ($called as $function) {
            foreach ($forbiddenPrefixes as $prefix) {
                Assert::false(
                    str_starts_with(strtolower($function), $prefix),
                    $function . '() belongs to an optional extension'
                );
            }
        }
    });

    $suite->test('source declares strict types and shells out to nothing', function () use ($source) {
        Assert::contains('declare(strict_types=1);', $source, 'strict_types must be enabled');
        foreach (['eval(', 'exec(', 'shell_exec(', 'system(', 'passthru(', 'popen('] as $forbidden) {
            Assert::notContains($forbidden, $source, $forbidden . ' must never appear');
        }
    });

    $suite->test('configuration block keeps its documented defaults', function () use ($source) {
        Assert::contains("const SERVER_SECRET = '';", $source, 'the shipped secret must stay empty');
        Assert::contains("const QUEUE_ROOT = '';", $source, 'the shipped queue root must stay empty');
        foreach ([
            'PERSIST_NONCES' => 'false',
            'SELFTEST_ENABLED' => 'false',
            'REQUIRE_HTTPS' => 'true',
            'FSYNC_ON_PUBLISH' => 'false',
        ] as $name => $value) {
            Assert::true(
                preg_match('/^const ' . $name . '\s*=\s*' . $value . ';$/m', $source) === 1,
                $name . ' must default to ' . $value
            );
        }
        Assert::false(
            preg_match('/const MAX_REQUEST_BYTES = (\d+);/', $source, $match) === 1 && (int) $match[1] >= 8388608,
            'MAX_REQUEST_BYTES must stay below the 8M post_max_size default'
        );
    });

    $suite->test('the runtime PHP is at least 8.2', function () {
        Assert::true(
            version_compare(PHP_VERSION, '8.2.0', '>='),
            'the suite itself needs PHP 8.2 or newer, found ' . PHP_VERSION
        );
    });
};

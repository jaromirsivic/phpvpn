<?php

/**
 * phpvpn v1 server test suite. CLI only, never deployed with the server.
 *
 * Usage: php server/php/tests/tests.php
 */

declare(strict_types=1);

namespace PhpVpn\Tests;

use PhpVpn\AesGcm;
use PhpVpn\Application;
use PhpVpn\Authenticator;
use PhpVpn\Base64Url;
use PhpVpn\BufferedResponseWriter;
use PhpVpn\Clock;
use PhpVpn\Config;
use PhpVpn\DiskStorage;
use PhpVpn\Iso8601;
use PhpVpn\JsonWire;
use PhpVpn\KeyDerivation;
use PhpVpn\Request;
use PhpVpn\StringInput;
use PhpVpn\Validators;

if (PHP_SAPI !== 'cli') {
    http_response_code(404);
    exit;
}

define('PHPVPN_NO_AUTORUN', true);
require dirname(__DIR__) . '/phpvpn.php';

// ---------------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------------

final class Runner
{
    private int $passed = 0;
    /** @var string[] */
    private array $failures = [];
    private string $group = '';
    private string $test = '';

    public function group(string $name): void
    {
        $this->group = $name;
        echo "\n== " . $name . "\n";
    }

    public function run(string $name, callable $body): void
    {
        $this->test = $name;
        try {
            $body($this);
            echo '  [ok] ' . $name . "\n";
        } catch (\Throwable $e) {
            $this->failures[] = $this->group . ' / ' . $name . ': ' . $e->getMessage();
            echo '  [FAIL] ' . $name . ': ' . $e->getMessage() . "\n";
        }
    }

    public function ok(bool $condition, string $message): void
    {
        if (!$condition) {
            throw new \RuntimeException($message);
        }
        $this->passed++;
    }

    public function same(mixed $expected, mixed $actual, string $message): void
    {
        if ($expected !== $actual) {
            throw new \RuntimeException(
                $message . ' (expected ' . self::show($expected) . ', got ' . self::show($actual) . ')'
            );
        }
        $this->passed++;
    }

    public function summary(): int
    {
        echo "\n----------------------------------------\n";
        echo 'assertions passed: ' . $this->passed . "\n";
        if ($this->failures === []) {
            echo "ALL TESTS PASSED\n";
            return 0;
        }
        echo 'FAILURES: ' . count($this->failures) . "\n";
        foreach ($this->failures as $failure) {
            echo '  - ' . $failure . "\n";
        }
        return 1;
    }

    private static function show(mixed $value): string
    {
        if (is_string($value)) {
            return strlen($value) > 120 ? '"' . substr($value, 0, 120) . '..."' : '"' . $value . '"';
        }
        return var_export($value, true);
    }
}

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

final class Harness
{
    public const SECRET = 'unit-test-server-secret-value-01';
    public const CLIENT_A = '10.99.99.2';
    public const CLIENT_B = '10.99.99.3';
    public const KEY_A = '1111111111111111111111111111111111111111111111111111111111111111';
    public const KEY_B = '2222222222222222222222222222222222222222222222222222222222222222';

    public readonly Config $config;
    public readonly string $root;
    public int $nowMs;

    public function __construct(?string $secret = null)
    {
        $this->root = sys_get_temp_dir() . DIRECTORY_SEPARATOR . 'phpvpn-test-' . bin2hex(random_bytes(6));
        $this->config = Config::fromConstants()
            ->withSecret($secret ?? self::SECRET)
            ->withAccessKeys([self::CLIENT_A => self::KEY_A, self::CLIENT_B => self::KEY_B])
            ->withBufferDirectory($this->root);
        $this->nowMs = (int) round(microtime(true) * 1000.0);
    }

    public function storage(): DiskStorage
    {
        return new DiskStorage($this->root);
    }

    public function call(
        string $action,
        string $method,
        ?string $contentType,
        string $body,
        ?int $nowMs = null
    ): BufferedResponseWriter {
        $writer = new BufferedResponseWriter();
        $request = new Request($method, $action === '' ? null : $action, $contentType, null, new StringInput($body));
        $application = new Application();
        if ($method !== 'POST' || $action === '' || !in_array($action, ['health', 'info', 'data'], true)) {
            $application->handle($request, $writer);
        } else {
            $application->dispatch(
                $this->config,
                $request,
                $writer,
                $action,
                new Clock($nowMs ?? $this->nowMs),
                $this->storage()
            );
        }
        HeaderCheck::assertAll($writer);
        return $writer;
    }

    public function timestamp(int $offsetMs = 0): string
    {
        return Iso8601::formatMs($this->nowMs + $offsetMs);
    }

    public function semiKey(): string
    {
        return KeyDerivation::semiKey($this->config->serverSecret);
    }

    public function fullKey(string $src, ?string $accessKeyHex = null): string
    {
        $hex = $accessKeyHex ?? ($src === self::CLIENT_A ? self::KEY_A : self::KEY_B);
        return KeyDerivation::fullAuthKey($this->config->serverSecret, (string) hex2bin($hex), $src);
    }

    public function authSemiBody(?string $timestamp = null, ?string $requestId = null, ?string $tag = null): string
    {
        $timestamp ??= $this->timestamp();
        $requestId ??= Validators::newUuidV4();
        $tag ??= Base64Url::encode(hash_hmac(
            'sha256',
            Authenticator::semiCanonical($timestamp, $requestId),
            $this->semiKey(),
            true
        ));
        return (string) json_encode([
            'type' => 'auth-semi',
            'timestamp' => $timestamp,
            'requestId' => $requestId,
            'tag' => $tag,
        ], JSON_UNESCAPED_SLASHES);
    }

    /** @param string[] $dataLines */
    public function dataBody(
        string $src,
        array $dataLines = [],
        ?string $timestamp = null,
        ?string $requestId = null,
        ?string $accessKeyHex = null
    ): array {
        $rest = $dataLines === [] ? '' : implode("\n", $dataLines) . "\n";
        return $this->rawBody($src, $rest, $timestamp, $requestId, $accessKeyHex);
    }

    /** Signs an arbitrary payload, needed to reach the framing checks behind authentication. */
    public function rawBody(
        string $src,
        string $rest,
        ?string $timestamp = null,
        ?string $requestId = null,
        ?string $accessKeyHex = null
    ): array {
        $timestamp ??= $this->timestamp();
        $requestId ??= Validators::newUuidV4();
        $canonical = Authenticator::fullCanonical($src, $timestamp, $requestId, hash('sha256', $rest));
        $tag = Base64Url::encode(hash_hmac('sha256', $canonical, $this->fullKey($src, $accessKeyHex), true));
        $auth = (string) json_encode([
            'type' => 'auth-full',
            'src' => $src,
            'timestamp' => $timestamp,
            'requestId' => $requestId,
            'tag' => $tag,
        ], JSON_UNESCAPED_SLASHES);
        return [$auth . "\n" . $rest, $requestId];
    }

    public static function dataLine(
        string $src,
        string $dst,
        string $payload = 'hello',
        ?string $messageId = null
    ): array {
        $messageId = $messageId ?? Validators::newUuidV4();
        $iv = random_bytes(12);
        $tag = random_bytes(16);
        $line = '{"type":"data","messageId":"' . $messageId . '","src":"' . $src . '","dst":"' . $dst
            . '","iv":"' . Base64Url::encode($iv) . '","tag":"' . Base64Url::encode($tag)
            . '","ciphertext":"' . Base64Url::encode($payload) . '"}';
        return [$line, $messageId, Base64Url::encode($tag)];
    }

    public function cleanup(): void
    {
        self::removeTree($this->root);
    }

    public static function removeTree(string $path): void
    {
        if (!is_dir($path)) {
            return;
        }
        foreach (scandir($path) ?: [] as $entry) {
            if ($entry === '.' || $entry === '..') {
                continue;
            }
            $full = $path . DIRECTORY_SEPARATOR . $entry;
            is_dir($full) ? self::removeTree($full) : @unlink($full);
        }
        @rmdir($path);
    }
}

final class HeaderCheck
{
    /** Mandatory on every response, chapter 4.1. Applied to every response the suite creates. */
    public static function assertAll(BufferedResponseWriter $writer): void
    {
        if (!$writer->isStarted()) {
            throw new \RuntimeException('response was never started');
        }
        $headers = $writer->headers;
        $required = [
            'Cache-Control' => 'no-store, no-cache, must-revalidate',
            'Pragma' => 'no-cache',
            'X-Content-Type-Options' => 'nosniff',
        ];
        foreach ($required as $name => $value) {
            if (($headers[$name] ?? null) !== $value) {
                throw new \RuntimeException('missing or wrong header ' . $name);
            }
        }
        if (!isset($headers['Content-Type'])) {
            throw new \RuntimeException('missing Content-Type');
        }
        if ($writer->status === 405 && !isset($headers['Allow'])) {
            throw new \RuntimeException('405 without Allow');
        }
        if ($writer->status === 503 && !isset($headers['Retry-After'])) {
            throw new \RuntimeException('503 without Retry-After');
        }
    }
}

/** Splits an NDJSON body into lines and validates the terminating LF of every record. */
function ndjsonLines(string $body): array
{
    if ($body === '' || substr($body, -1) !== "\n") {
        throw new \RuntimeException('ndjson body must end with LF');
    }
    return explode("\n", substr($body, 0, -1));
}

function errorCode(BufferedResponseWriter $writer): string
{
    $decoded = json_decode($writer->body, true);
    return is_array($decoded) && isset($decoded['code']) ? (string) $decoded['code'] : '';
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

$runner = new Runner();

// --- 1. Cryptography -------------------------------------------------------

$runner->group('Cryptography');

$runner->run('AES-256-GCM known answer self test', function (Runner $t): void {
    $t->ok(AesGcm::selfTest(), 'self test must pass');
});

$runner->run('AES-256-GCM rejects single bit changes and hides plaintext', function (Runner $t): void {
    $key = (string) hex2bin('feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308');
    $iv = (string) hex2bin('cafebabefacedbaddecaf888');
    $aad = 'PHPVPN-DATA';
    $gcm = new AesGcm($key);
    [$ciphertext, $tag] = $gcm->seal($iv, 'plaintext payload', $aad);
    $t->same('plaintext payload', $gcm->open($iv, $ciphertext, $tag, $aad), 'round trip');

    $badCiphertext = $ciphertext;
    $badCiphertext[0] = chr(ord($badCiphertext[0]) ^ 0x01);
    $t->same(null, $gcm->open($iv, $badCiphertext, $tag, $aad), 'modified ciphertext');
    $t->same(null, $gcm->open($iv, $ciphertext, $tag, $aad . 'x'), 'modified aad');

    $badIv = $iv;
    $badIv[11] = chr(ord($badIv[11]) ^ 0x01);
    $t->same(null, $gcm->open($badIv, $ciphertext, $tag, $aad), 'modified iv');

    $otherKey = $key;
    $otherKey[0] = chr(ord($otherKey[0]) ^ 0x01);
    $t->same(null, (new AesGcm($otherKey))->open($iv, $ciphertext, $tag, $aad), 'modified key');
});

$runner->run('GMAC over empty plaintext with non empty AAD', function (Runner $t): void {
    $gcm = new AesGcm(str_repeat("\x11", 32));
    $tag = $gcm->mac(str_repeat("\x22", 12), "PHPVPN-HEALTH-RESPONSE\nhealth");
    $t->same(16, strlen($tag), 'tag length');
    [$ciphertext, $sealTag] = $gcm->seal(str_repeat("\x22", 12), '', "PHPVPN-HEALTH-RESPONSE\nhealth");
    $t->same('', $ciphertext, 'empty ciphertext');
    $t->same($tag, $sealTag, 'mac equals seal tag');
});

$runner->run('HMAC-SHA-256 RFC 4231 vectors', function (Runner $t): void {
    $t->same(
        'b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7',
        hash_hmac('sha256', 'Hi There', (string) hex2bin('0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b')),
        'case 1'
    );
    $t->same(
        '5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843',
        hash_hmac('sha256', 'what do ya want for nothing?', 'Jefe'),
        'case 2'
    );
});

$runner->run('HKDF-SHA-256 RFC 5869 vectors', function (Runner $t): void {
    $ikm = str_repeat("\x0b", 22);
    $okm = hash_hkdf(
        'sha256',
        $ikm,
        42,
        (string) hex2bin('f0f1f2f3f4f5f6f7f8f9'),
        (string) hex2bin('000102030405060708090a0b0c')
    );
    $t->same(
        '3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865',
        bin2hex($okm),
        'case 1'
    );
    $okm = hash_hkdf('sha256', $ikm, 42, '', '');
    $t->same(
        '8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8',
        bin2hex($okm),
        'case 3'
    );
});

$runner->run('Key derivation separation and sensitivity', function (Runner $t): void {
    $secret = 'server-secret';
    $keyA = (string) hex2bin(Harness::KEY_A);
    $keyB = (string) hex2bin(Harness::KEY_B);

    $t->same(
        KeyDerivation::semiKey($secret),
        KeyDerivation::semiKey($secret),
        'semiKey deterministic'
    );
    $t->ok(
        KeyDerivation::semiKey($secret) !== KeyDerivation::semiKey($secret . 'x'),
        'semiKey depends on the secret'
    );

    $full = KeyDerivation::fullAuthKey($secret, $keyA, '10.99.99.2');
    $t->same(32, strlen($full), 'fullAuthKey length');
    $t->ok($full !== KeyDerivation::fullAuthKey($secret, $keyB, '10.99.99.2'), 'depends on access key');
    $t->ok($full !== KeyDerivation::fullAuthKey($secret . 'x', $keyA, '10.99.99.2'), 'depends on secret');
    $t->ok($full !== KeyDerivation::fullAuthKey($secret, $keyA, '10.99.99.3'), 'depends on src');

    $t->ok(
        KeyDerivation::healthKey($secret, 'a') !== KeyDerivation::healthKey($secret, 'b'),
        'healthKey depends on requestId'
    );
    $salt = str_repeat("\x05", 32);
    $t->ok(
        KeyDerivation::infoKey($secret, $salt, 'a') !== KeyDerivation::infoKey($secret, $salt, 'b'),
        'infoKey depends on requestId'
    );
});

$runner->run('SERVER_SECRET normalization', function (Runner $t): void {
    $empty = Config::fromConstants()->withSecret('');
    $zeros = Config::fromConstants()->withSecret('CHANGE_THIS_SERVER_SECRET');
    $t->same($zeros->serverSecret, $empty->serverSecret, 'empty normalizes to the current default');
    $t->same(
        KeyDerivation::semiKey($zeros->serverSecret),
        KeyDerivation::semiKey($empty->serverSecret),
        'same semiKey'
    );

    foreach ([str_repeat('0', 32), str_repeat('0', 64)] as $original) {
        $t->same($original, Config::fromConstants()->withSecret($original)->serverSecret, 'nonempty zeros preserved');
    }
    $single = Config::fromConstants()->withSecret('0');
    $t->same('0', $single->serverSecret, 'the string "0" is not normalized');
    $nulls = Config::fromConstants()->withSecret(str_repeat("\x00", 32));
    $t->same(str_repeat("\x00", 32), $nulls->serverSecret, 'null bytes are not normalized');
    $t->ok(
        KeyDerivation::semiKey($single->serverSecret) !== KeyDerivation::semiKey($empty->serverSecret),
        'different secrets give different keys'
    );

    $t->ok($empty->isInsecureSecret(), 'empty is insecure');
    $t->ok($zeros->isInsecureSecret(), 'ASCII zeros are insecure');
    $t->ok($nulls->isInsecureSecret(), 'null bytes are insecure');
    $t->ok(!Config::fromConstants()->withSecret('real-secret')->isInsecureSecret(), 'real secret is secure');
});

// --- 2. Wire format --------------------------------------------------------

$runner->group('Wire format');

$runner->run('Canonical base64url', function (Runner $t): void {
    $t->ok(Base64Url::isCanonical('_w'), '_w is canonical for ff');
    $t->ok(Base64Url::isCanonical('__8'), '__8 is canonical for ffff');
    $t->ok(!Base64Url::isCanonical('_x'), '_x is not canonical');
    $t->ok(!Base64Url::isCanonical('__9'), '__9 is not canonical');
    $t->ok(!Base64Url::isCanonical('A'), 'length modulo 4 of 1');
    $t->ok(!Base64Url::isCanonical('AAAA='), 'padding is rejected');
    $t->ok(!Base64Url::isCanonical('AA A'), 'whitespace is rejected');
    $t->ok(!Base64Url::isCanonical('+/=='), 'standard alphabet is rejected');
    $t->ok(Base64Url::isCanonical(''), 'empty is canonical');

    $t->same("\xff", Base64Url::decode('_w'), 'decode _w');
    $t->same(null, Base64Url::decode('_x'), 'decode _x fails');

    for ($length = 1; $length <= 48; $length++) {
        $raw = random_bytes($length);
        $text = Base64Url::encode($raw);
        $t->ok(Base64Url::isCanonical($text), 'round trip canonical ' . $length);
        $t->same($raw, Base64Url::decode($text), 'round trip decode ' . $length);
        $t->same(strlen($text), Base64Url::encodedLength($length), 'encoded length ' . $length);
    }
});

$runner->run('Strict millisecond timestamps', function (Runner $t): void {
    $t->same(0, Iso8601::parseMs('1970-01-01T00:00:00.000Z'), 'epoch');
    $t->same(1770000000123, Iso8601::parseMs(Iso8601::formatMs(1770000000123)), 'round trip');
    $t->same('2026-08-09T20:15:30.123Z', Iso8601::formatMs(Iso8601::parseMs('2026-08-09T20:15:30.123Z')), 'format');

    $invalid = [
        '2026-08-09T20:15:30Z',
        '2026-08-09T20:15:30.12Z',
        '2026-08-09T20:15:30.1234Z',
        '2026-08-09t20:15:30.123Z',
        '2026-08-09T20:15:30.123z',
        '2026-08-09T20:15:30.123+01:00',
        '2026-08-09T20:15:60.123Z',
        '2026-08-09T24:00:00.000Z',
        '2026-02-29T00:00:00.000Z',
        '2026-13-01T00:00:00.000Z',
        '2026-00-01T00:00:00.000Z',
        '2026-08-00T00:00:00.000Z',
        '2026-08-09 20:15:30.123Z',
        '',
    ];
    foreach ($invalid as $value) {
        $t->same(null, Iso8601::parseMs($value), 'rejects ' . $value);
    }
    $t->ok(Iso8601::parseMs('2024-02-29T00:00:00.000Z') !== null, 'accepts a leap day');
    $t->same('2026-08-12T14-37-05.123Z', Iso8601::formatFileNameMs(Iso8601::parseMs('2026-08-12T14:37:05.123Z')), 'file name form');
});

$runner->run('Address and UUID validation', function (Runner $t): void {
    $t->ok(Validators::isVpnAddress('10.99.99.2'), 'lowest address');
    $t->ok(Validators::isVpnAddress('10.99.99.254'), 'highest address');
    $t->ok(!Validators::isVpnAddress('10.99.99.1'), 'below range');
    $t->ok(!Validators::isVpnAddress('10.99.99.255'), 'above range');
    $t->ok(!Validators::isVpnAddress('10.99.99.02'), 'leading zero');
    $t->ok(!Validators::isVpnAddress('10.99.98.2'), 'other network');
    $t->ok(!Validators::isVpnAddress('10.99.99.'), 'missing host');
    $t->ok(!Validators::isVpnAddress('10.99.99.2 '), 'trailing space');

    $uuid = Validators::newUuidV4();
    $t->ok(Validators::isUuidV4($uuid), 'generated uuid is valid');
    $t->ok(!Validators::isUuidV4(strtoupper($uuid)), 'uppercase rejected');
    $t->ok(!Validators::isUuidV4('69e82c2a-2732-166f-b9b1-f891084df639'), 'version must be 4');
    $t->ok(!Validators::isUuidV4('69e82c2a-2732-466f-c9b1-f891084df639'), 'variant must be 8 9 a or b');
    $t->ok(!Validators::isUuidV4(substr($uuid, 0, 35)), 'length');
});

$runner->run('Flat JSON object scanner', function (Runner $t): void {
    $allowed = ['type', 'requestId'];
    $ok = JsonWire::parseFlatStringObject('{"type":"auth-none","requestId":"x"}', $allowed);
    $t->same('auth-none', $ok['type'] ?? null, 'parses members');

    $bad = [
        '{"type":"auth-none","requestId":"x","extra":"y"}' => 'unknown member',
        '{"type":"auth-none"}' => 'missing member',
        '{"type":"auth-none","type":"auth-none","requestId":"x"}' => 'duplicate member',
        '{"type":"auth-none","requestId":"x"} ' => 'trailing whitespace',
        '{"type":"auth-none","requestId":"x"}' . "\n" => 'trailing LF',
        ' {"type":"auth-none","requestId":"x"}' => 'leading whitespace',
        '{"type":"auth-none","requestId":1}' => 'non string value',
        '{"type":"auth-none","requestId":"a\u0062"}' => 'escape sequence',
        '{"type":"auth-none","requestId":{"a":"b"}}' => 'nested object',
        '{"type":"auth-none","requestId":"x"}{}' => 'trailing object',
        'null' => 'not an object',
    ];
    foreach ($bad as $input => $why) {
        $t->same(null, JsonWire::parseFlatStringObject($input, $allowed), 'rejects ' . $why);
    }
});

$runner->run('Strict data record scanner', function (Runner $t): void {
    [$line] = Harness::dataLine('10.99.99.2', '10.99.99.3');
    $record = JsonWire::parseDataRecord($line, '10.99.99.2');
    $t->ok(!is_int($record), 'valid record parses');
    $t->same('10.99.99.3', $record->dst, 'destination');

    $t->same(401, JsonWire::parseDataRecord($line, '10.99.99.4'), 'src mismatch is unauthorized');

    [$longer] = Harness::dataLine('10.99.99.24', '10.99.99.3');
    $t->same(401, JsonWire::parseDataRecord($longer, '10.99.99.2'), 'src prefix is unauthorized');

    $reordered = '{"type":"data","messageId":"' . Validators::newUuidV4() . '","src":"10.99.99.2","dst":"10.99.99.3"'
        . ',"tag":"' . Base64Url::encode(random_bytes(16)) . '","iv":"' . Base64Url::encode(random_bytes(12))
        . '","ciphertext":"AAAA"}';
    $t->same(400, JsonWire::parseDataRecord($reordered, '10.99.99.2'), 'member order is normative');

    $withSpace = str_replace('","src":"', '", "src":"', $line);
    $t->same(400, JsonWire::parseDataRecord($withSpace, '10.99.99.2'), 'whitespace is rejected');

    $extra = substr($line, 0, -1) . ',"x":"y"}';
    $t->same(400, JsonWire::parseDataRecord($extra, '10.99.99.2'), 'member after ciphertext');

    $emptyCiphertext = preg_replace('/"ciphertext":"[^"]*"/', '"ciphertext":""', $line);
    $t->same(400, JsonWire::parseDataRecord($emptyCiphertext, '10.99.99.2'), 'empty ciphertext');

    $shortIv = preg_replace('/"iv":"[^"]*"/', '"iv":"AAAA"', $line);
    $t->same(400, JsonWire::parseDataRecord($shortIv, '10.99.99.2'), 'iv length');

    $shortTag = preg_replace('/"tag":"[^"]*"/', '"tag":"AAAA"', $line);
    $t->same(400, JsonWire::parseDataRecord($shortTag, '10.99.99.2'), 'tag length');

    $badDst = str_replace('"dst":"10.99.99.3"', '"dst":"10.99.99.255"', $line);
    $t->same(400, JsonWire::parseDataRecord($badDst, '10.99.99.2'), 'destination range');

    $nonCanonical = preg_replace('/"ciphertext":"[^"]*"/', '"ciphertext":"_x"', $line);
    $t->same(400, JsonWire::parseDataRecord($nonCanonical, '10.99.99.2'), 'non canonical ciphertext');
});

// --- 3. Routing and status page -------------------------------------------

$runner->group('Routing');

$runner->run('Status page and method routing', function (Runner $t): void {
    $harness = new Harness();
    try {
        $page = $harness->call('', 'GET', null, '');
        $t->same(200, $page->status, 'status page');
        $t->ok(str_contains($page->body, '<h1>PHPVPN server is running</h1>'), 'H1 present');
        $t->ok(str_contains($page->headers['Content-Type'], 'text/html'), 'html content type');

        $post = $harness->call('', 'POST', null, '');
        $t->same(405, $post->status, 'POST on the status page');
        $t->same('GET, HEAD', $post->headers['Allow'], 'Allow header');

        $unknown = $harness->call('nope', 'POST', null, '');
        $t->same(404, $unknown->status, 'unknown action');
        $t->same('not_found', errorCode($unknown), 'not_found code');

        foreach (['health', 'info', 'data'] as $action) {
            foreach (['GET', 'HEAD', 'PUT', 'DELETE'] as $method) {
                $response = $harness->call($action, $method, null, '');
                $t->same(405, $response->status, $method . ' on ' . $action);
                $t->same('POST', $response->headers['Allow'], 'Allow POST for ' . $action);
            }
        }
    } finally {
        $harness->cleanup();
    }
});

$runner->run('Insecure secret warning', function (Runner $t): void {
    // The status page reads the configured constant, the default file ships with ASCII zeros.
    $harness = new Harness();
    try {
        $page = $harness->call('', 'GET', null, '');
        $t->ok(
            str_contains($page->body, '<aside><b>Warning:</b>') === (\PhpVpn\SERVER_SECRET === 'CHANGE_THIS_SERVER_SECRET'
                || \PhpVpn\SERVER_SECRET === ''),
            'warning matches the configured secret'
        );
    } finally {
        $harness->cleanup();
    }
});

$runner->run('Embedded status template and complete warning removal', function (Runner $t): void {
    $mockup = trim((string) file_get_contents(dirname(__DIR__, 3) . '/assets/index.html'));
    foreach ([true, false] as $insecure) {
        $writer = new BufferedResponseWriter();
        (new \PhpVpn\StatusPageHandler())->handle($writer, $insecure, []);
        $expected = $insecure ? $mockup : preg_replace('/<aside>.*?<\/aside>/s', '', $mockup);
        $t->same($expected, $writer->body, 'embedded mockup matches exactly');
        $t->same((string) strlen($writer->body), $writer->headers['Content-Length'], 'exact response length');
        $t->same($insecure, str_contains($writer->body, '<aside>'), 'whole warning panel is conditional');
        $t->ok(str_contains($writer->body, 'crypto.getRandomValues(new Uint8Array(32))'), 'Web Crypto generator retained');
        $t->ok(str_contains($writer->body, '<button id=s>Show HTTP Headers</button>'), 'Show HTTP Headers button');
        $t->ok(str_contains($writer->body, '<dialog id=d>'), 'headers dialog');
        $t->ok(str_contains($writer->body, '<a href=https://www.pilandrome.com>www.pilandrome.com</a> (MIT License)'), 'credits line');
    }
});

$runner->run('Status page shows received headers for the Configuration Wizard', function (Runner $t): void {
    $writer = new BufferedResponseWriter();
    (new \PhpVpn\StatusPageHandler())->handle($writer, false, [
        'Host' => 'example.test', 'User-Agent' => 'A<b>&"\'', 'content-type' => 'text/plain', 'Cookie' => 'a=1',
        'Connection' => 'close', 'Content-Length' => '0', 'Transfer-Encoding' => 'chunked', 'X-Test' => '',
    ]);
    $t->ok(
        str_contains($writer->body, "readonly>User-Agent: A&lt;b&gt;&amp;&quot;&#039;\nCookie: a=1\nX-Test: </textarea>"),
        'escaped headers without client managed names'
    );
    $t->same((string) strlen($writer->body), $writer->headers['Content-Length'], 'exact response length');

    // The CLI SAPI has no getallheaders(), so the $_SERVER fallback is exercised.
    $saved = $_SERVER;
    try {
        $_SERVER['HTTP_X_PHPVPN_PROBE'] = 'yes';
        $headers = \PhpVpn\StatusPageHandler::receivedHeaders();
        $t->same('yes', $headers['X-Phpvpn-Probe'] ?? null, 'header name rebuilt from $_SERVER');
    } finally {
        $_SERVER = $saved;
    }
});

// --- 4. health -------------------------------------------------------------

$runner->group('health');

$runner->run('Successful health response is authenticated', function (Runner $t): void {
    $harness = new Harness();
    try {
        $requestId = Validators::newUuidV4();
        $body = '{"type":"auth-none","requestId":"' . $requestId . '"}';
        $response = $harness->call('health', 'POST', 'application/json; charset=utf-8', $body);
        $t->same(200, $response->status, 'status');

        $decoded = json_decode($response->body, true);
        $t->same(
            ['serverVersion', 'status', 'description', 'timestamp', 'requestId', 'iv', 'tag'],
            array_keys($decoded),
            'exactly seven fields'
        );
        $t->same('running', $decoded['status'], 'status field');
        $t->same('OK', $decoded['description'], 'description field');
        $t->same($requestId, $decoded['requestId'], 'requestId echo');
        $t->ok(Iso8601::parseMs($decoded['timestamp']) !== null, 'millisecond timestamp');
        $t->same(12, strlen((string) Base64Url::decode($decoded['iv'])), 'iv length');
        $t->same(16, strlen((string) Base64Url::decode($decoded['tag'])), 'tag length');

        $key = KeyDerivation::healthKey($harness->config->serverSecret, $requestId);
        $aad = Authenticator::healthAad($requestId, $decoded['timestamp'], $decoded['iv']);
        $expected = (new AesGcm($key))->mac((string) Base64Url::decode($decoded['iv']), $aad);
        $t->same(Base64Url::encode($expected), $decoded['tag'], 'client side tag verification');

        $wrongAad = Authenticator::healthAad($requestId, $decoded['timestamp'], $decoded['iv']) . 'x';
        $t->ok(
            Base64Url::encode((new AesGcm($key))->mac((string) Base64Url::decode($decoded['iv']), $wrongAad))
                !== $decoded['tag'],
            'modified aad invalidates the tag'
        );

        $otherKey = KeyDerivation::healthKey($harness->config->serverSecret . 'x', $requestId);
        $t->ok(
            Base64Url::encode((new AesGcm($otherKey))->mac((string) Base64Url::decode($decoded['iv']), $aad))
                !== $decoded['tag'],
            'other secret invalidates the tag'
        );

        $second = $harness->call('health', 'POST', 'application/json; charset=utf-8', $body);
        $secondDecoded = json_decode($second->body, true);
        $t->ok($secondDecoded['iv'] !== $decoded['iv'], 'each response has a fresh iv');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('health rejects malformed auth-none', function (Runner $t): void {
    $harness = new Harness();
    try {
        $valid = Validators::newUuidV4();
        $cases = [
            '{"type":"auth-none","requestId":"' . $valid . '","timestamp":"2026-08-09T20:15:30.123Z"}',
            '{"type":"auth-none","requestId":"' . $valid . '","tag":"AAAA"}',
            '{"type":"auth-none","requestId":"' . $valid . '","src":"10.99.99.2"}',
            '{"type":"auth-none"}',
            '{"requestId":"' . $valid . '"}',
            '{"type":"auth-semi","requestId":"' . $valid . '"}',
            '{"type":"auth-none","requestId":"not-a-uuid"}',
            '{"type":"auth-none","requestId":"' . $valid . '"}' . "\n",
            'garbage',
        ];
        foreach ($cases as $body) {
            $response = $harness->call('health', 'POST', 'application/json', $body);
            $t->same(400, $response->status, 'rejects ' . substr($body, 0, 48));
            $t->same('bad_request', errorCode($response), 'bad_request code');
        }

        $wrongType = $harness->call('health', 'POST', 'text/plain', '{"type":"auth-none","requestId":"' . $valid . '"}');
        $t->same(415, $wrongType->status, 'wrong content type');

        $tooLarge = $harness->call('health', 'POST', 'application/json', str_repeat('a', 5000));
        $t->same(413, $tooLarge->status, 'oversized body');
    } finally {
        $harness->cleanup();
    }
});

// --- 5. info ---------------------------------------------------------------

$runner->group('info');

$runner->run('Successful info response decrypts', function (Runner $t): void {
    $harness = new Harness();
    try {
        $requestId = Validators::newUuidV4();
        $body = $harness->authSemiBody(null, $requestId);
        $response = $harness->call('info', 'POST', 'application/json; charset=utf-8', $body);
        $t->same(200, $response->status, 'status');

        $decoded = json_decode($response->body, true);
        $t->same(['type', 'requestId', 'salt', 'iv', 'ciphertext', 'tag'], array_keys($decoded), 'response fields');
        $t->same(32, strlen((string) Base64Url::decode($decoded['salt'])), 'salt length');
        $t->same(12, strlen((string) Base64Url::decode($decoded['iv'])), 'iv length');
        $t->same(16, strlen((string) Base64Url::decode($decoded['tag'])), 'tag length');

        $key = KeyDerivation::infoKey(
            $harness->config->serverSecret,
            (string) Base64Url::decode($decoded['salt']),
            $requestId
        );
        $aad = Authenticator::infoAad($requestId, $decoded['salt'], $decoded['iv']);
        $plaintext = (new AesGcm($key))->open(
            (string) Base64Url::decode($decoded['iv']),
            (string) Base64Url::decode($decoded['ciphertext']),
            (string) Base64Url::decode($decoded['tag']),
            $aad
        );
        $t->ok($plaintext !== null, 'tag verifies');
        $t->ok(str_starts_with($plaintext, "{\n    \"serverVersion\":"), 'authenticated plaintext is pretty JSON');
        $t->ok(strlen($plaintext) <= \PhpVpn\MAX_INFO_PLAINTEXT_BYTES, 'exact authenticated byte limit');
        $t->same(strlen($plaintext), strlen(Base64Url::decode($decoded['ciphertext'])), 'ciphertext preserves byte count');

        $info = json_decode((string) $plaintext, true);
        $t->same(['serverVersion', 'generatedAt', 'clients', 'limits'], array_keys($info), 'plaintext fields');
        $t->ok(Iso8601::parseMs($info['generatedAt']) !== null, 'generatedAt is a millisecond timestamp');
        $t->same(\PhpVpn\MAX_DATA_RECORDS_PER_REQUEST, $info['limits']['maxDataRecordsPerRequest'], 'limit value');
        $t->same([], $info['clients'], 'no clients before any data request');
        $t->ok(!str_contains((string) $plaintext, $harness->root), 'no storage path leaks');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('info authentication and skew', function (Runner $t): void {
    $harness = new Harness();
    try {
        $badTag = $harness->authSemiBody(null, null, Base64Url::encode(random_bytes(32)));
        $t->same(401, $harness->call('info', 'POST', 'application/json', $badTag)->status, 'wrong tag');

        $shortTag = $harness->authSemiBody(null, null, Base64Url::encode(random_bytes(16)));
        $t->same(400, $harness->call('info', 'POST', 'application/json', $shortTag)->status, 'tag length');

        $malformed = str_replace('"timestamp":"' . $harness->timestamp() . '"', '"timestamp":"2026-08-09T20:15:30Z"', $harness->authSemiBody());
        $t->same(400, $harness->call('info', 'POST', 'application/json', $malformed)->status, 'second precision timestamp');

        foreach ([-60000, 0, 60000] as $offset) {
            $body = $harness->authSemiBody($harness->timestamp($offset));
            $t->same(200, $harness->call('info', 'POST', 'application/json', $body)->status, 'skew ' . $offset);
        }
        foreach ([-60001, 60001] as $offset) {
            $body = $harness->authSemiBody($harness->timestamp($offset));
            $response = $harness->call('info', 'POST', 'application/json', $body);
            $t->same(401, $response->status, 'skew ' . $offset);
            $t->same('unauthorized', errorCode($response), 'unauthorized code');
        }
    } finally {
        $harness->cleanup();
    }
});

// --- 6. data ---------------------------------------------------------------

$runner->group('data');

/** Independent client side verification of a data response. */
function verifyDataResponse(Runner $t, Harness $harness, string $src, string $requestId, string $body): array
{
    $lines = ndjsonLines($body);
    $macLine = array_pop($lines);
    $resultLine = array_pop($lines);

    $mac = json_decode($macLine, true);
    $t->same('data-mac', $mac['type'], 'last line is data-mac');
    $t->same(['type', 'requestId', 'tag'], array_keys($mac), 'data-mac fields');
    $t->same($requestId, $mac['requestId'], 'data-mac requestId');

    $result = json_decode($resultLine, true);
    $t->same('data-result', $result['type'], 'result line');
    $t->same(
        ['type', 'requestId', 'acceptedMessageIds', 'deferredMessageIds', 'acceptedRecords', 'acceptedBytes'],
        array_keys($result),
        'data-result fields'
    );
    $t->same($requestId, $result['requestId'], 'data-result requestId');

    $canonical = "PHPVPN-DATA-RESPONSE\ndata\n" . $requestId . "\n" . count($lines) . "\n";
    foreach ($lines as $line) {
        $record = json_decode($line, true);
        $canonical .= $record['messageId'] . "\n" . $record['tag'] . "\n";
    }
    $canonical .= hash('sha256', $resultLine);
    $expected = hash_hmac('sha256', $canonical, $harness->fullKey($src), true);
    $t->same(Base64Url::encode($expected), $mac['tag'], 'data-mac verifies');

    return [$lines, $result];
}

$runner->run('Empty poll returns only result and mac', function (Runner $t): void {
    $harness = new Harness();
    try {
        [$body, $requestId] = $harness->dataBody(Harness::CLIENT_A);
        $response = $harness->call('data', 'POST', 'application/x-ndjson; charset=utf-8', $body);
        $t->same(200, $response->status, 'status');
        $t->same('application/x-ndjson; charset=utf-8', $response->headers['Content-Type'], 'content type');

        [$records, $result] = verifyDataResponse($t, $harness, Harness::CLIENT_A, $requestId, $response->body);
        $t->same(0, count($records), 'no data lines');
        $t->same([], $result['acceptedMessageIds'], 'nothing accepted');
        $t->same([], $result['deferredMessageIds'], 'nothing deferred');
        $t->same(0, $result['acceptedRecords'], 'zero records');
        $t->same(0, $result['acceptedBytes'], 'zero bytes');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('Store and forward between two clients', function (Runner $t): void {
    $harness = new Harness();
    try {
        [$line1, $id1, $tag1] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'first packet');
        [$line2, $id2, $tag2] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'second packet');
        [$body, $requestId] = $harness->dataBody(Harness::CLIENT_A, [$line1, $line2]);

        $send = $harness->call('data', 'POST', 'application/x-ndjson', $body);
        $t->same(200, $send->status, 'send status');
        [$records, $result] = verifyDataResponse($t, $harness, Harness::CLIENT_A, $requestId, $send->body);
        $t->same(0, count($records), 'sender receives nothing');
        $t->same([$id1, $id2], $result['acceptedMessageIds'], 'both accepted');
        $t->same(2, $result['acceptedRecords'], 'accepted records');
        $t->same(strlen($line1) + strlen($line2) + 2, $result['acceptedBytes'], 'accepted bytes include LF');

        [$pollBody, $pollId] = $harness->dataBody(Harness::CLIENT_B);
        $poll = $harness->call('data', 'POST', 'application/x-ndjson', $pollBody);
        [$received, $pollResult] = verifyDataResponse($t, $harness, Harness::CLIENT_B, $pollId, $poll->body);
        $t->same(2, count($received), 'two records delivered');
        $t->same($line1, $received[0], 'first line is byte identical');
        $t->same($line2, $received[1], 'second line is byte identical');
        $t->same([], $pollResult['acceptedMessageIds'], 'poll accepted nothing');

        [$againBody, $againId] = $harness->dataBody(Harness::CLIENT_B);
        $again = $harness->call('data', 'POST', 'application/x-ndjson', $againBody);
        [$empty] = verifyDataResponse($t, $harness, Harness::CLIENT_B, $againId, $again->body);
        $t->same(0, count($empty), 'at most once delivery');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('Records addressed to the sender are returned immediately', function (Runner $t): void {
    $harness = new Harness();
    try {
        [$line, $id] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_A, 'loopback');
        [$body, $requestId] = $harness->dataBody(Harness::CLIENT_A, [$line]);
        $response = $harness->call('data', 'POST', 'application/x-ndjson', $body);
        [$records, $result] = verifyDataResponse($t, $harness, Harness::CLIENT_A, $requestId, $response->body);
        $t->same(1, count($records), 'loopback record is in the same response');
        $t->same($line, $records[0], 'byte identical');
        $t->same([$id], $result['acceptedMessageIds'], 'accepted');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('data authentication failures', function (Runner $t): void {
    $harness = new Harness();
    try {
        [$body] = $harness->dataBody(Harness::CLIENT_A, [], null, null, Harness::KEY_B);
        $response = $harness->call('data', 'POST', 'application/x-ndjson', $body);
        $t->same(401, $response->status, 'wrong access key');

        [$unknown] = $harness->dataBody('10.99.99.9');
        $t->same(401, $harness->call('data', 'POST', 'application/x-ndjson', $unknown)->status, 'unknown client');

        [$skewed] = $harness->dataBody(Harness::CLIENT_A, [], $harness->timestamp(-60001));
        $t->same(401, $harness->call('data', 'POST', 'application/x-ndjson', $skewed)->status, 'stale timestamp');

        [$edge] = $harness->dataBody(Harness::CLIENT_A, [], $harness->timestamp(-60000));
        $t->same(200, $harness->call('data', 'POST', 'application/x-ndjson', $edge)->status, 'exact skew boundary');

        [$valid, $requestId] = $harness->dataBody(Harness::CLIENT_A);
        $tampered = str_replace('"src":"10.99.99.2"', '"src":"10.99.99.3"', $valid);
        $t->same(401, $harness->call('data', 'POST', 'application/x-ndjson', $tampered)->status, 'tampered src');

        [$line] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B);
        [$withData] = $harness->dataBody(Harness::CLIENT_A, [$line]);
        $bodyTampered = $withData . '{"type":"data"}' . "\n";
        $t->same(401, $harness->call('data', 'POST', 'application/x-ndjson', $bodyTampered)->status, 'body hash covered');

        [$spoofLine] = Harness::dataLine(Harness::CLIENT_B, Harness::CLIENT_A);
        [$spoof] = $harness->dataBody(Harness::CLIENT_A, [$spoofLine]);
        $t->same(401, $harness->call('data', 'POST', 'application/x-ndjson', $spoof)->status, 'spoofed record src');

        $t->same(415, $harness->call('data', 'POST', 'application/json', $valid)->status, 'wrong content type');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('NDJSON framing and limits', function (Runner $t): void {
    $harness = new Harness();
    try {
        [$line] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B);

        // Framing is checked after authentication, so every malformed payload has to be signed.
        [$noFinalLf] = $harness->rawBody(Harness::CLIENT_A, $line);
        $t->same(400, $harness->call('data', 'POST', 'application/x-ndjson', $noFinalLf)->status, 'missing final LF');

        [$emptyLine] = $harness->rawBody(Harness::CLIENT_A, $line . "\n\n");
        $t->same(400, $harness->call('data', 'POST', 'application/x-ndjson', $emptyLine)->status, 'empty line');

        [$onlyLf] = $harness->rawBody(Harness::CLIENT_A, "\n");
        $t->same(400, $harness->call('data', 'POST', 'application/x-ndjson', $onlyLf)->status, 'single empty record');

        $lines = [];
        for ($i = 0; $i < \PhpVpn\MAX_DATA_RECORDS_PER_REQUEST; $i++) {
            [$lines[]] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'packet ' . $i);
        }
        [$sixteen, $sixteenId] = $harness->dataBody(Harness::CLIENT_A, $lines);
        $response = $harness->call('data', 'POST', 'application/x-ndjson', $sixteen);
        $t->same(200, $response->status, 'sixteen records accepted');
        [, $result] = verifyDataResponse($t, $harness, Harness::CLIENT_A, $sixteenId, $response->body);
        $t->same(16, $result['acceptedRecords'], 'all sixteen accepted');

        [$seventeenth] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'overflow');
        $lines[] = $seventeenth;
        [$tooMany] = $harness->dataBody(Harness::CLIENT_A, $lines);
        $overflow = $harness->call('data', 'POST', 'application/x-ndjson', $tooMany);
        $t->same(413, $overflow->status, 'seventeen records rejected');
        $t->same('payload_too_large', errorCode($overflow), 'payload_too_large code');

        [$duplicateLine, $duplicateId] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'a');
        [$duplicateLine2] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'b', $duplicateId);
        [$duplicate] = $harness->dataBody(Harness::CLIENT_A, [$duplicateLine, $duplicateLine2]);
        $t->same(400, $harness->call('data', 'POST', 'application/x-ndjson', $duplicate)->status, 'duplicate messageId');

        [$crlf] = $harness->rawBody(Harness::CLIENT_A, $line . "\r\n");
        $t->same(400, $harness->call('data', 'POST', 'application/x-ndjson', $crlf)->status, 'CRLF framing');

        $longAuth = '{"type":"auth-full","src":"10.99.99.2","timestamp":"' . $harness->timestamp()
            . '","requestId":"' . str_repeat('a', 5000) . '","tag":"AAAA"}' . "\n";
        $t->same(413, $harness->call('data', 'POST', 'application/x-ndjson', $longAuth)->status, 'oversized auth line');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('Deferred delivery when the destination queue is full', function (Runner $t): void {
    $harness = new Harness();
    try {
        $storage = $harness->storage();
        $storage->isUsable();
        $directory = $storage->clientDirectory(Harness::CLIENT_B);
        @mkdir($directory, 0o770, true);

        // The logical size lives in the file name, so a full queue can be simulated cheaply.
        $expires = Iso8601::formatFileNameMs($harness->nowMs + 30000);
        $full = $directory . DIRECTORY_SEPARATOR . $expires . '_' . \PhpVpn\MAX_QUEUE_BYTES_PER_CLIENT
            . '_10.99.99.4_' . Validators::newUuidV4() . '.data';
        file_put_contents($full, "{\"v\":1,\"bytes\":0,\"records\":[]}\n");

        [$line, $id] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'deferred');
        [$body, $requestId] = $harness->dataBody(Harness::CLIENT_A, [$line]);
        $response = $harness->call('data', 'POST', 'application/x-ndjson', $body);
        $t->same(200, $response->status, 'capacity shortage is not an error');
        [, $result] = verifyDataResponse($t, $harness, Harness::CLIENT_A, $requestId, $response->body);
        $t->same([$id], $result['deferredMessageIds'], 'message is deferred');
        $t->same([], $result['acceptedMessageIds'], 'nothing accepted');
        $t->same(0, $result['acceptedBytes'], 'no accepted bytes');

        // The shortage triggers exactly one targeted sweep, proven by the expired batch disappearing.
        $expired = $directory . DIRECTORY_SEPARATOR . Iso8601::formatFileNameMs($harness->nowMs - 1000) . '_100_'
            . '10.99.99.4_' . Validators::newUuidV4() . '.data';
        file_put_contents($expired, "{\"v\":1,\"bytes\":0,\"records\":[]}\n");

        [$line2, $id2] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'still blocked');
        [$body2, $requestId2] = $harness->dataBody(Harness::CLIENT_A, [$line2]);
        $response2 = $harness->call('data', 'POST', 'application/x-ndjson', $body2);
        [, $result2] = verifyDataResponse($t, $harness, Harness::CLIENT_A, $requestId2, $response2->body);
        $t->same([$id2], $result2['deferredMessageIds'], 'a live blocker keeps the queue full');
        $t->ok(!file_exists($expired), 'the targeted sweep removed the expired batch');
        $t->ok(file_exists($full), 'the live batch survives the sweep');

        // Once the blocker expires, the queue accepts again and the expired blocker is not counted.
        rename($full, $directory . DIRECTORY_SEPARATOR . Iso8601::formatFileNameMs($harness->nowMs - 1000) . '_'
            . \PhpVpn\MAX_QUEUE_BYTES_PER_CLIENT . '_10.99.99.4_' . Validators::newUuidV4() . '.data');
        [$line3, $id3] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'after expiry');
        [$body3, $requestId3] = $harness->dataBody(Harness::CLIENT_A, [$line3]);
        $response3 = $harness->call('data', 'POST', 'application/x-ndjson', $body3);
        [, $result3] = verifyDataResponse($t, $harness, Harness::CLIENT_A, $requestId3, $response3->body);
        $t->same([$id3], $result3['acceptedMessageIds'], 'expired capacity is reusable');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('A full destination does not affect another destination', function (Runner $t): void {
    $harness = new Harness();
    try {
        $storage = $harness->storage();
        $storage->isUsable();
        $directory = $storage->clientDirectory(Harness::CLIENT_B);
        @mkdir($directory, 0o770, true);
        $full = $directory . DIRECTORY_SEPARATOR . Iso8601::formatFileNameMs($harness->nowMs + 30000) . '_'
            . \PhpVpn\MAX_QUEUE_BYTES_PER_CLIENT . '_10.99.99.4_' . Validators::newUuidV4() . '.data';
        file_put_contents($full, "{\"v\":1,\"bytes\":0,\"records\":[]}\n");

        [$blocked, $blockedId] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'blocked');
        [$loopback, $loopbackId] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_A, 'accepted');
        [$body, $requestId] = $harness->dataBody(Harness::CLIENT_A, [$blocked, $loopback]);
        $response = $harness->call('data', 'POST', 'application/x-ndjson', $body);
        [$records, $result] = verifyDataResponse($t, $harness, Harness::CLIENT_A, $requestId, $response->body);

        $t->same([$loopbackId], $result['acceptedMessageIds'], 'the healthy destination is accepted');
        $t->same([$blockedId], $result['deferredMessageIds'], 'the full destination is deferred');
        $t->same(1, count($records), 'the loopback record is delivered in the same response');
    } finally {
        $harness->cleanup();
    }
});

// --- 7. Storage contract ---------------------------------------------------

$runner->group('Storage');

$runner->run('Atomic claim, expiry and sweep', function (Runner $t): void {
    $harness = new Harness();
    try {
        $storage = $harness->storage();
        $t->ok($storage->isUsable(), 'root is usable');

        $now = $harness->nowMs;
        $lines = ['{"type":"data","messageId":"a","src":"10.99.99.2","dst":"10.99.99.3"}'];
        $pairs = [['11111111-1111-4111-8111-111111111111', 'AAAA']];
        $t->ok($storage->enqueue(Harness::CLIENT_B, Harness::CLIENT_A, $lines, $pairs, $now + 60000), 'enqueue');
        $t->same(strlen($lines[0]) + 1, $storage->usage(Harness::CLIENT_B, $now), 'usage counts the logical bytes');

        $first = $storage->drainAll(Harness::CLIENT_B, $now);
        $t->same(1, count($first), 'first drain claims the batch');
        $second = $storage->drainAll(Harness::CLIENT_B, $now);
        $t->same(0, count($second), 'a claimed batch is never handed out twice');
        $t->same(0, $storage->usage(Harness::CLIENT_B, $now), 'claimed batches do not count towards capacity');
        $t->same($pairs[0][0], $first[0]->pairs[0][0], 'the index survives the round trip');
        // The data handler deletes the claimed file once it is written to the response.
        unlink($first[0]->path);

        // Expired batches are neither delivered nor counted.
        $storage->enqueue(Harness::CLIENT_B, Harness::CLIENT_A, $lines, $pairs, $now - 1);
        $t->same(0, count($storage->drainAll(Harness::CLIENT_B, $now)), 'expired batch is not delivered');
        $t->same(0, $storage->usage(Harness::CLIENT_B, $now), 'expired batch does not consume capacity');

        $directory = $storage->clientDirectory(Harness::CLIENT_B);
        $marker = $directory . DIRECTORY_SEPARATOR . 'info.json';
        touch($marker);
        foreach (['receiving', 'sending'] as $state) {
            $name = Iso8601::formatFileNameMs($now - 5000) . '_10_10.99.99.2_' . Validators::newUuidV4() . '.' . $state;
            file_put_contents($directory . DIRECTORY_SEPARATOR . $name, 'x');
        }
        $storage->sweepQueue(Harness::CLIENT_B, $now);
        $remaining = array_values(array_diff(scandir($directory) ?: [], ['.', '..']));
        sort($remaining);
        $t->same(['.lock', 'info.json'], $remaining, 'sweep removes every expired state and keeps the marker');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('Best effort lastSeen marker', function (Runner $t): void {
    $harness = new Harness();
    try {
        $storage = $harness->storage();
        $storage->isUsable();
        $now = $harness->nowMs;
        $marker = $storage->clientDirectory(Harness::CLIENT_A) . DIRECTORY_SEPARATOR . 'info.json';

        $storage->touchLastSeenBestEffort(Harness::CLIENT_A, $now);
        $t->ok(file_exists($marker), 'missing marker is created');
        $t->same(0, filesize($marker), 'marker is an empty file');

        $exactly = (int) floor($now / 1000) - 30;
        touch($marker, $exactly);
        $storage->touchLastSeenBestEffort(Harness::CLIENT_A, $exactly * 1000 + 30000);
        clearstatcache(true, $marker);
        $t->same($exactly, filemtime($marker), 'exactly 30 seconds does not refresh');

        $older = (int) floor($now / 1000) - 31;
        touch($marker, $older);
        $storage->touchLastSeenBestEffort(Harness::CLIENT_A, $now);
        clearstatcache(true, $marker);
        $t->ok(filemtime($marker) > $older, '31 seconds refreshes');

        $future = (int) floor($now / 1000) + 600;
        touch($marker, $future);
        $storage->touchLastSeenBestEffort(Harness::CLIENT_A, $now);
        clearstatcache(true, $marker);
        $t->same($future, filemtime($marker), 'a future mtime is left alone');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('info reports lastSeen after a data request', function (Runner $t): void {
    $harness = new Harness();
    try {
        [$body] = $harness->dataBody(Harness::CLIENT_A);
        $harness->call('data', 'POST', 'application/x-ndjson', $body);

        $requestId = Validators::newUuidV4();
        $response = $harness->call('info', 'POST', 'application/json', $harness->authSemiBody(null, $requestId));
        $decoded = json_decode($response->body, true);
        $plaintext = (new AesGcm(KeyDerivation::infoKey(
            $harness->config->serverSecret,
            (string) Base64Url::decode($decoded['salt']),
            $requestId
        )))->open(
            (string) Base64Url::decode($decoded['iv']),
            (string) Base64Url::decode($decoded['ciphertext']),
            (string) Base64Url::decode($decoded['tag']),
            Authenticator::infoAad($requestId, $decoded['salt'], $decoded['iv'])
        );
        $info = json_decode((string) $plaintext, true);
        $t->same(1, count($info['clients']), 'only the client with a marker is listed');
        $t->same(Harness::CLIENT_A, $info['clients'][0]['address'], 'address');
        $t->ok(Iso8601::parseMs($info['clients'][0]['lastSeen']) !== null, 'lastSeen has millisecond form');
        $t->ok(str_ends_with($info['clients'][0]['lastSeen'], '.000Z'), 'filesystem second precision shows as .000');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('Storage failures never break the protocol result', function (Runner $t): void {
    $harness = new Harness();
    try {
        $storage = $harness->storage();
        $storage->isUsable();

        // A destination path blocked by a regular file makes the group deferred, never a 5xx.
        $blocked = $storage->clientDirectory(Harness::CLIENT_B);
        file_put_contents($blocked, 'not a directory');

        [$line, $id] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'undeliverable');
        [$body, $requestId] = $harness->dataBody(Harness::CLIENT_A, [$line]);
        $response = $harness->call('data', 'POST', 'application/x-ndjson', $body);
        $t->same(200, $response->status, 'still a success');
        [, $result] = verifyDataResponse($t, $harness, Harness::CLIENT_A, $requestId, $response->body);
        $t->same([$id], $result['deferredMessageIds'], 'group deferred');
    } finally {
        $harness->cleanup();
    }
});

// --- 8. Replay -------------------------------------------------------------

$runner->group('Replay');

$runner->run('A replayed request inside the window is processed again', function (Runner $t): void {
    $harness = new Harness();
    try {
        [$line, $id] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'replayed');
        [$body, $requestId] = $harness->dataBody(Harness::CLIENT_A, [$line]);

        $first = $harness->call('data', 'POST', 'application/x-ndjson', $body);
        $second = $harness->call('data', 'POST', 'application/x-ndjson', $body);
        $t->same(200, $first->status, 'first copy');
        $t->same(200, $second->status, 'second copy');

        [$pollBody, $pollId] = $harness->dataBody(Harness::CLIENT_B);
        $poll = $harness->call('data', 'POST', 'application/x-ndjson', $pollBody);
        [$records] = verifyDataResponse($t, $harness, Harness::CLIENT_B, $pollId, $poll->body);
        $t->same(2, count($records), 'the replay created a second copy');
    } finally {
        $harness->cleanup();
    }
});

$runner->run('No replay or nonce state is persisted', function (Runner $t): void {
    $harness = new Harness();
    try {
        [$line] = Harness::dataLine(Harness::CLIENT_A, Harness::CLIENT_B, 'state check');
        [$body] = $harness->dataBody(Harness::CLIENT_A, [$line]);
        $harness->call('data', 'POST', 'application/x-ndjson', $body);
        [$pollBody] = $harness->dataBody(Harness::CLIENT_B);
        $harness->call('data', 'POST', 'application/x-ndjson', $pollBody);

        $unexpected = [];
        $walk = function (string $path) use (&$walk, &$unexpected, $harness): void {
            foreach (scandir($path) ?: [] as $entry) {
                if ($entry === '.' || $entry === '..') {
                    continue;
                }
                $full = $path . DIRECTORY_SEPARATOR . $entry;
                if (is_dir($full)) {
                    $walk($full);
                    continue;
                }
                $allowed = ['info.json', '.lock', '.sweep', '.htaccess', 'web.config', 'index.html'];
                if (in_array($entry, $allowed, true) || DiskStorage::parseBatchName($entry) !== null) {
                    continue;
                }
                $unexpected[] = $entry;
            }
        };
        $walk($harness->root);
        $t->same([], $unexpected, 'storage holds no nonce or replay artifacts');
    } finally {
        $harness->cleanup();
    }
});

require __DIR__ . '/tests-info-extensions.php';
exit($runner->summary());

<?php
// PHP VPN 1.0.1 (MIT License, Copyright (c) www.pilandrome.com)

/**
 * VPN Server
 *
 * Single deployable file implementing the packet handoff protocol described in protocol.md.
 * Requires PHP 8.X with no optional extensions.
 *
 * Deployment notes:
 *  - post_max_size and any proxy body limit must be strictly higher than MAX_REQUEST_BODY_BYTES.
 *  - memory_limit should be at least 64M.
 *  - BUFFER_DIRECTORY must not be reachable over HTTP. The server writes best effort
 *    .htaccess and web.config guards, nginx must be configured manually, for example:
 *        location ^~ /phpvpn_data/ { deny all; return 404; }
 *
 * MIT licensed.
 */

declare(strict_types=1);

namespace PhpVpn;

// @wp-skip-begin
// Regions between @wp-skip-begin and @wp-skip-end belong to this standalone file only.
// tools/build-wordpress-plugin.py copies everything else into the WordPress plugin core.

if (PHP_INT_SIZE < 8) {
    echo '<h1>This PHP script is supported only on 64-bit PHP.</h1>';
    exit;
}
// @wp-skip-end

// ---------------------------------------------------------------------------
// 1. Configuration header. This is the only part an administrator edits.
// ---------------------------------------------------------------------------

/** Informative SemVer version of this server application. */
const SERVER_VERSION = '1.0.1';

// @wp-skip-begin The WordPress plugin keeps these four values in its settings.
/**
 * Single server secret shared with authorized clients.
 * Replace the default with a strong random value. An empty string is normalized
 * at runtime to CHANGE_THIS_SERVER_SECRET and the status page warns about it.
 */
const SERVER_SECRET = 'CHANGE_THIS_SERVER_SECRET';

/**
 * Per client access keys, keyed by the client VPN address.
 * Every value is exactly 64 lowercase hexadecimal characters, that is 32 raw bytes.
 * Replace all placeholder values before production use.
 */
const ACCESS_KEYS = [
    '10.99.99.2' => '0202020202020202020202020202020202020202020202020202020202020202',
    '10.99.99.3' => '0303030303030303030303030303030303030303030303030303030303030303',
    '10.99.99.4' => '0404040404040404040404040404040404040404040404040404040404040404',
    '10.99.99.5' => '0505050505050505050505050505050505050505050505050505050505050505',
    '10.99.99.6' => '0606060606060606060606060606060606060606060606060606060606060606',
    '10.99.99.7' => '0707070707070707070707070707070707070707070707070707070707070707',
    '10.99.99.8' => '0808080808080808080808080808080808080808080808080808080808080808',
    '10.99.99.9' => '0909090909090909090909090909090909090909090909090909090909090909',
    '10.99.99.10' => '0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a',
    '10.99.99.11' => '0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b',
    '10.99.99.12' => '0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c',
    '10.99.99.13' => '0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d',
    '10.99.99.14' => '0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e',
    '10.99.99.15' => '0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f',
];

/** Storage root, relative to this file. Old ./phpvpn data is NOT migrated automatically. */
const BUFFER_DIRECTORY = './phpvpn_data';

/** true serves the root status page. false returns 404 not_found for GET/HEAD,
 * without HTML. All REST API actions remain available. */
const STATUS_PAGE_ENABLED = true;
// @wp-skip-end

/** Storage backend. Only 'disk' exists in v1. */
const STORAGE_DRIVER = 'disk';

/** VPN address space. Changing it requires the same change on every participant. */
const VPN_ADDRESS_PREFIX = '10.99.99.';
const VPN_HOST_MIN = 2;
const VPN_HOST_MAX = 254;

/** Protocol limits, see chapter 11.1 of protocol.md. */
const MAX_DATA_RECORDS_PER_REQUEST = 16;
const MAX_AUTH_RECORD_BYTES = 4096;
const MAX_REQUEST_BODY_BYTES = 983040;
const MAX_INFO_PLAINTEXT_BYTES = 262144;
const MAX_INFO_REQUEST_BODY_BYTES = 65536;
const MAX_MASKED_COMMUNICATION_CONTRACTS = 252;
const INFO_TEMP_TTL_SECONDS = 300;
const MAX_QUEUE_BYTES_PER_CLIENT = 16777216;
const AUTH_TIMESTAMP_SKEW_SECONDS = 60;
const QUEUE_TTL_SECONDS = 60;
const LOCK_TIMEOUT_MS = 200;
const MIN_DATA_POLL_INTERVAL_MS = 50;
const MAX_DATA_POLL_INTERVAL_MS = 3200;

/** Best effort lastSeen marker is refreshed only when it is older than this. */
const CLIENT_STATS_MIN_UPDATE_SECONDS = 30;

/** Opportunistic maintenance budget. */
const SWEEP_MIN_INTERVAL_SECONDS = 5;
const SWEEP_MAX_DIRECTORIES = 8;
const SWEEP_MAX_FILES = 256;
const SWEEP_MAX_MILLISECONDS = 20;

/** Create .htaccess, web.config and index.html guards inside the storage root. */
const PROTECT_BUFFER_DIRECTORY = true;

/** Client download links shown on the status page. */
const CLIENT_WINDOWS_DOWNLOAD_URL = 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-win-x64.zip';
const CLIENT_MACOS_DOWNLOAD_URL = 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-macos.zip';
const CLIENT_LINUX_DOWNLOAD_URL = 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-linux.zip';

// ---------------------------------------------------------------------------
// 2. Runtime hardening
// ---------------------------------------------------------------------------

// @wp-skip-begin Process wide settings are allowed only in the standalone file.
final class Runtime
{
    private static bool $initialized = false;
    private static ?ResponseWriter $writer = null;
    /** @var string[] */
    private static array $notices = [];

    public static function init(): void
    {
        if (self::$initialized) {
            return;
        }
        self::$initialized = true;

        @ini_set('display_errors', '0');
        @ini_set('html_errors', '0');
        @ini_set('zlib.output_compression', '0');
        error_reporting(E_ALL);
        ignore_user_abort(true);
        @set_time_limit(30);
        @header_remove('X-Powered-By');

        set_error_handler(static function (int $severity, string $message, string $file = '', int $line = 0): bool {
            // Warnings must never reach the response body. Only a short internal note is kept.
            self::note('e' . $severity . '@' . basename($file) . ':' . $line);
            return true;
        });

        set_exception_handler(static function (\Throwable $e): void {
            // Only a location, never a message, an argument or any request content.
            self::log('exception@' . basename($e->getFile()) . ':' . $e->getLine());
            $writer = self::$writer;
            if ($writer !== null && !$writer->isStarted()) {
                Http::error($writer, 500, 'internal_error');
            }
        });

        register_shutdown_function(static function (): void {
            $last = error_get_last();
            if ($last === null) {
                return;
            }
            $fatal = [E_ERROR, E_PARSE, E_CORE_ERROR, E_COMPILE_ERROR, E_USER_ERROR];
            if (!in_array($last['type'], $fatal, true)) {
                return;
            }
            self::log('fatal@' . basename($last['file']) . ':' . $last['line']);
            $writer = self::$writer;
            if ($writer !== null && !$writer->isStarted()) {
                Http::error($writer, 500, 'internal_error');
            }
        });
    }

    public static function bindWriter(ResponseWriter $writer): void
    {
        self::$writer = $writer;
    }

    public static function note(string $code): void
    {
        if (count(self::$notices) < 32) {
            self::$notices[] = $code;
        }
    }

    /** Chapter 4.1, the log never receives a secret, a tag, a ciphertext or a request. */
    public static function log(string $code): void
    {
        self::note($code);
        @error_log('phpvpn: ' . $code);
    }

    /** @return string[] */
    public static function takeNotices(): array
    {
        $n = self::$notices;
        self::$notices = [];
        return $n;
    }
}
// @wp-skip-end

// ---------------------------------------------------------------------------
// 3. Filesystem helpers. None of them ever throws or emits output.
// ---------------------------------------------------------------------------

// The queue needs flock(), exclusive creation, fsync and atomic rename() on its own storage
// root, which WP_Filesystem does not provide. Paths are built only from the configured root and
// validated names, never from request text.
// phpcs:disable WordPress.WP.AlternativeFunctions
final class Fs
{
    public static function isDir(string $path): bool
    {
        return @is_dir($path);
    }

    public static function ensureDir(string $path): bool
    {
        if (@is_dir($path)) {
            return true;
        }
        @mkdir($path, 0o770, true);
        return @is_dir($path);
    }

    public static function unlink(string $path): bool
    {
        return @unlink($path);
    }

    public static function rename(string $from, string $to): bool
    {
        return @rename($from, $to);
    }

    public static function mtime(string $path): ?int
    {
        clearstatcache(true, $path);
        $t = @filemtime($path);
        return $t === false ? null : $t;
    }

    public static function touch(string $path, ?int $unixSeconds = null): bool
    {
        return $unixSeconds === null ? @touch($path) : @touch($path, $unixSeconds);
    }

    public static function writeFileIfMissing(string $path, string $contents): void
    {
        if (@file_exists($path)) {
            return;
        }
        $handle = @fopen($path, 'xb');
        if ($handle === false) {
            return;
        }
        @fwrite($handle, $contents);
        @fclose($handle);
    }

    /** @return string[]|null */
    public static function listDir(string $path): ?array
    {
        $handle = @opendir($path);
        if ($handle === false) {
            return null;
        }
        $entries = [];
        while (($entry = @readdir($handle)) !== false) {
            if ($entry === '.' || $entry === '..') {
                continue;
            }
            $entries[] = $entry;
        }
        @closedir($handle);
        return $entries;
    }
}

// phpcs:enable WordPress.WP.AlternativeFunctions

// ---------------------------------------------------------------------------
// 4. Time
// ---------------------------------------------------------------------------

final class Clock
{
    private int $nowMs;

    public function __construct(?int $nowMs = null)
    {
        $this->nowMs = $nowMs ?? (int) round(microtime(true) * 1000.0);
    }

    public function nowMs(): int
    {
        return $this->nowMs;
    }
}

final class Iso8601
{
    /** Strict parser for exactly YYYY-MM-DDTHH:MM:SS.sssZ, returns epoch milliseconds. */
    public static function parseMs(string $text): ?int
    {
        if (strlen($text) !== 24) {
            return null;
        }
        if ($text[4] !== '-' || $text[7] !== '-' || $text[10] !== 'T'
            || $text[13] !== ':' || $text[16] !== ':' || $text[19] !== '.' || $text[23] !== 'Z') {
            return null;
        }
        $digits = substr($text, 0, 4) . substr($text, 5, 2) . substr($text, 8, 2)
            . substr($text, 11, 2) . substr($text, 14, 2) . substr($text, 17, 2) . substr($text, 20, 3);
        if (strspn($digits, '0123456789') !== 17) {
            return null;
        }
        $year = (int) substr($text, 0, 4);
        $month = (int) substr($text, 5, 2);
        $day = (int) substr($text, 8, 2);
        $hour = (int) substr($text, 11, 2);
        $minute = (int) substr($text, 14, 2);
        $second = (int) substr($text, 17, 2);
        $milli = (int) substr($text, 20, 3);

        if ($month < 1 || $month > 12 || $day < 1 || $hour > 23 || $minute > 59 || $second > 59) {
            return null;
        }
        if ($day > self::daysInMonth($year, $month)) {
            return null;
        }
        $days = self::daysFromCivil($year, $month, $day);
        return ((($days * 86400) + ($hour * 3600) + ($minute * 60) + $second) * 1000) + $milli;
    }

    public static function formatMs(int $epochMs): string
    {
        $seconds = (int) floor($epochMs / 1000);
        $milli = $epochMs - ($seconds * 1000);
        return gmdate('Y-m-d\TH:i:s', $seconds) . '.' . str_pad((string) $milli, 3, '0', STR_PAD_LEFT) . 'Z';
    }

    /** Filename safe variant, colons replaced by dashes. */
    public static function formatFileNameMs(int $epochMs): string
    {
        $seconds = (int) floor($epochMs / 1000);
        $milli = $epochMs - ($seconds * 1000);
        return gmdate('Y-m-d\TH-i-s', $seconds) . '.' . str_pad((string) $milli, 3, '0', STR_PAD_LEFT) . 'Z';
    }

    public static function isLeapYear(int $year): bool
    {
        return ($year % 4 === 0 && $year % 100 !== 0) || $year % 400 === 0;
    }

    public static function daysInMonth(int $year, int $month): int
    {
        static $lengths = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
        if ($month === 2 && self::isLeapYear($year)) {
            return 29;
        }
        return $lengths[$month - 1];
    }

    /** Days from 1970-01-01, Howard Hinnant civil algorithm. */
    public static function daysFromCivil(int $year, int $month, int $day): int
    {
        $year -= $month <= 2 ? 1 : 0;
        $era = intdiv($year >= 0 ? $year : $year - 399, 400);
        $yoe = $year - $era * 400;
        $doy = intdiv(153 * ($month + ($month > 2 ? -3 : 9)) + 2, 5) + $day - 1;
        $doe = $yoe * 365 + intdiv($yoe, 4) - intdiv($yoe, 100) + $doy;
        return $era * 146097 + $doe - 719468;
    }
}

// ---------------------------------------------------------------------------
// 5. Canonical base64url
// ---------------------------------------------------------------------------

final class Base64Url
{
    private const ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
    /** @var array<string,int>|null */
    private static ?array $values = null;

    public static function encode(string $raw): string
    {
        return rtrim(strtr(base64_encode($raw), '+/', '-_'), '=');
    }

    /**
     * Canonicity check, equivalent to the decode and re-encode round trip required by chapter 6.1.
     * Verifies the alphabet, the length modulo rule and the zero unused bits of the last character.
     */
    public static function isCanonical(string $text): bool
    {
        $length = strlen($text);
        $mod = $length % 4;
        if ($mod === 1) {
            return false;
        }
        if (strspn($text, self::ALPHABET) !== $length) {
            return false;
        }
        if ($mod === 0) {
            return true;
        }
        if (self::$values === null) {
            $values = [];
            for ($i = 0; $i < 64; $i++) {
                $values[self::ALPHABET[$i]] = $i;
            }
            self::$values = $values;
        }
        $last = self::$values[$text[$length - 1]];
        $mask = $mod === 2 ? 0x0F : 0x03;
        return ($last & $mask) === 0;
    }

    public static function isCanonicalOfByteLength(string $text, int $bytes): bool
    {
        return strlen($text) === self::encodedLength($bytes) && self::isCanonical($text);
    }

    public static function encodedLength(int $bytes): int
    {
        return intdiv($bytes, 3) * 4 + [0 => 0, 1 => 2, 2 => 3][$bytes % 3];
    }

    /** Decodes a value that already passed isCanonical, returns null on any failure. */
    public static function decode(string $text): ?string
    {
        if (!self::isCanonical($text)) {
            return null;
        }
        $padded = strtr($text, '-_', '+/');
        $pad = strlen($padded) % 4;
        if ($pad !== 0) {
            $padded .= str_repeat('=', 4 - $pad);
        }
        $raw = base64_decode($padded, true);
        if ($raw === false) {
            return null;
        }
        // Defense in depth, the re-encoded value must match the original text byte for byte.
        return self::encode($raw) === $text ? $raw : null;
    }
}

// ---------------------------------------------------------------------------
// 6. Value validators
// ---------------------------------------------------------------------------

final class Validators
{
    public static function isVpnAddress(string $address): bool
    {
        $prefix = VPN_ADDRESS_PREFIX;
        $prefixLength = strlen($prefix);
        if (strncmp($address, $prefix, $prefixLength) !== 0) {
            return false;
        }
        $host = substr($address, $prefixLength);
        $length = strlen($host);
        if ($length < 1 || $length > 3 || strspn($host, '0123456789') !== $length) {
            return false;
        }
        if ($length > 1 && $host[0] === '0') {
            return false;
        }
        $value = (int) $host;
        return $value >= VPN_HOST_MIN && $value <= VPN_HOST_MAX;
    }

    public static function isUuidV4(string $uuid): bool
    {
        return preg_match('/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/', $uuid) === 1;
    }

    public static function newUuidV4(): string
    {
        $bytes = random_bytes(16);
        $bytes[6] = chr((ord($bytes[6]) & 0x0F) | 0x40);
        $bytes[8] = chr((ord($bytes[8]) & 0x3F) | 0x80);
        $hex = bin2hex($bytes);
        return substr($hex, 0, 8) . '-' . substr($hex, 8, 4) . '-' . substr($hex, 12, 4)
            . '-' . substr($hex, 16, 4) . '-' . substr($hex, 20, 12);
    }

    public static function addressToDirectory(string $address): string
    {
        return str_replace('.', '_', $address);
    }

    public static function addressSortKey(string $address): int
    {
        $parts = explode('.', $address);
        if (count($parts) !== 4) {
            return 0;
        }
        return ((int) $parts[0] << 24) | ((int) $parts[1] << 16) | ((int) $parts[2] << 8) | (int) $parts[3];
    }
}

// ---------------------------------------------------------------------------
// 7. Strict JSON and NDJSON wire layer
// ---------------------------------------------------------------------------

final class DataRecord
{
    public function __construct(
        public readonly string $messageId,
        public readonly string $dst,
        public readonly string $gcmTag,
        public readonly string $line,
    ) {
    }
}

final class JsonWire
{
    /** Bounded JSON reader preserving object/array identity and rejecting duplicate decoded keys. */
    public static function parseInfoObject(string $text): ?\stdClass
    {
        if (strlen($text) > MAX_INFO_REQUEST_BODY_BYTES) return null;
        $i = 0;
        try {
            $value = self::infoValue($text, $i, 0);
            $i = self::skipWhitespace($text, $i, strlen($text));
            return $i === strlen($text) && $value instanceof \stdClass ? $value : null;
        } catch (\UnexpectedValueException | \JsonException $e) {
            return null;
        }
    }

    private static function infoValue(string $text, int &$i, int $depth): mixed
    {
        $length = strlen($text);
        $i = self::skipWhitespace($text, $i, $length);
        if ($depth > 4 || $i >= $length) throw new \UnexpectedValueException();
        $c = $text[$i++];
        if ($c === '"') {
            $start = $i - 1;
            while ($i < $length) {
                $c = $text[$i++];
                if ($c === '\\') { $i++; continue; }
                if ($c === '"') return json_decode(substr($text, $start, $i - $start), false, 8, JSON_THROW_ON_ERROR);
            }
            throw new \UnexpectedValueException();
        }
        if ($c === '{' || $c === '[') {
            $object = $c === '{';
            $end = $object ? '}' : ']';
            $result = $object ? new \stdClass() : [];
            $i = self::skipWhitespace($text, $i, $length);
            if (($text[$i] ?? '') === $end) { $i++; return $result; }
            while (true) {
                if ($object) {
                    $key = self::infoValue($text, $i, $depth + 1);
                    if (!is_string($key) || str_contains($key, "\0") || property_exists($result, $key)) throw new \UnexpectedValueException();
                    $i = self::skipWhitespace($text, $i, $length);
                    if (($text[$i++] ?? '') !== ':') throw new \UnexpectedValueException();
                    $result->{$key} = self::infoValue($text, $i, $depth + 1);
                } else {
                    $result[] = self::infoValue($text, $i, $depth + 1);
                }
                $i = self::skipWhitespace($text, $i, $length);
                $separator = $text[$i++] ?? '';
                if ($separator === $end) return $result;
                if ($separator !== ',') throw new \UnexpectedValueException();
            }
        }
        $i--;
        foreach (['true' => true, 'false' => false, 'null' => null] as $literal => $value) {
            if (substr($text, $i, strlen($literal)) === $literal) { $i += strlen($literal); return $value; }
        }
        throw new \UnexpectedValueException();
    }

    /** Returns normalized member order, or a protocol error status. Never normalizes values. */
    public static function maskedContracts(mixed $value, string $src): array|int
    {
        if (!is_array($value)) return 400;
        if (count($value) > min(MAX_MASKED_COMMUNICATION_CONTRACTS, VPN_HOST_MAX - VPN_HOST_MIN)) return 413;
        $out = [];
        $seen = [];
        foreach ($value as $item) {
            if (!$item instanceof \stdClass || count(get_object_vars($item)) !== 2 ||
                !isset($item->peer, $item->hash) || !is_string($item->peer) || !is_string($item->hash) ||
                !Validators::isVpnAddress($item->peer) || $item->peer === $src || isset($seen[$item->peer]) ||
                preg_match('/\A[0-9a-f]{64}\z/D', $item->hash) !== 1) return 400;
            $seen[$item->peer] = true;
            $out[] = ['peer' => $item->peer, 'hash' => $item->hash];
        }
        return $out;
    }

    private const DATA_HEAD = '{"type":"data","messageId":"';

    /**
     * Scans a flat JSON object whose members are all plain ASCII strings.
     * Rejects duplicate keys, unknown keys, nested values, escape sequences and trailing bytes.
     *
     * @param string[] $allowedKeys
     * @return array<string,string>|null
     */
    public static function parseFlatStringObject(string $text, array $allowedKeys): ?array
    {
        $allowed = array_flip($allowedKeys);
        $length = strlen($text);
        $i = 0;
        if ($i >= $length || $text[$i] !== '{') {
            return null;
        }
        $i++;
        $result = [];
        $i = self::skipWhitespace($text, $i, $length);
        if ($i < $length && $text[$i] === '}') {
            $i++;
            return self::atEnd($text, $i, $length) ? $result : null;
        }
        while (true) {
            $i = self::skipWhitespace($text, $i, $length);
            $key = self::readString($text, $i, $length);
            if ($key === null) {
                return null;
            }
            if (!isset($allowed[$key]) || array_key_exists($key, $result)) {
                return null;
            }
            $i = self::skipWhitespace($text, $i, $length);
            if ($i >= $length || $text[$i] !== ':') {
                return null;
            }
            $i++;
            $i = self::skipWhitespace($text, $i, $length);
            $value = self::readString($text, $i, $length);
            if ($value === null) {
                return null;
            }
            $result[$key] = $value;
            $i = self::skipWhitespace($text, $i, $length);
            if ($i >= $length) {
                return null;
            }
            if ($text[$i] === ',') {
                $i++;
                continue;
            }
            if ($text[$i] === '}') {
                $i++;
                break;
            }
            return null;
        }
        if (!self::atEnd($text, $i, $length)) {
            return null;
        }
        return count($result) === count($allowedKeys) ? $result : null;
    }

    /**
     * Strict scanner for a data record line, chapter 9.3. The member order is normative,
     * so parsing is a sequence of literal comparisons at known offsets.
     *
     * @return DataRecord|int DataRecord on success, otherwise the HTTP status to return
     */
    public static function parseDataRecord(string $line, string $expectedSrc): DataRecord|int
    {
        $position = 0;
        if (!self::literal($line, $position, self::DATA_HEAD)) {
            return 400;
        }
        $position += strlen(self::DATA_HEAD);

        if (strlen($line) < $position + 36) {
            return 400;
        }
        $messageId = substr($line, $position, 36);
        if (!Validators::isUuidV4($messageId)) {
            return 400;
        }
        $position += 36;

        if (!self::literal($line, $position, '","src":"')) {
            return 400;
        }
        $position += 9;
        $srcLength = strlen($expectedSrc);
        if (!self::literal($line, $position, $expectedSrc)) {
            return 401;
        }
        $position += $srcLength;
        if (!self::literal($line, $position, '","dst":"')) {
            // A longer src that merely starts with the authenticated address is a spoofed source.
            return 401;
        }
        $position += 9;

        $quote = strpos($line, '"', $position);
        if ($quote === false) {
            return 400;
        }
        $dst = substr($line, $position, $quote - $position);
        if (!Validators::isVpnAddress($dst)) {
            return 400;
        }
        $position = $quote;

        if (!self::literal($line, $position, '","iv":"')) {
            return 400;
        }
        $position += 8;
        $ivLength = Base64Url::encodedLength(12);
        if (strlen($line) < $position + $ivLength) {
            return 400;
        }
        $iv = substr($line, $position, $ivLength);
        if (!Base64Url::isCanonical($iv)) {
            return 400;
        }
        $position += $ivLength;

        if (!self::literal($line, $position, '","tag":"')) {
            return 400;
        }
        $position += 9;
        $tagLength = Base64Url::encodedLength(16);
        if (strlen($line) < $position + $tagLength) {
            return 400;
        }
        $tag = substr($line, $position, $tagLength);
        if (!Base64Url::isCanonical($tag)) {
            return 400;
        }
        $position += $tagLength;

        if (!self::literal($line, $position, '","ciphertext":"')) {
            return 400;
        }
        $position += 16;

        $end = strlen($line);
        if ($end < $position + 3 || substr_compare($line, '"}', $end - 2, 2) !== 0) {
            return 400;
        }
        $ciphertext = substr($line, $position, $end - 2 - $position);
        if ($ciphertext === '' || !Base64Url::isCanonical($ciphertext)) {
            return 400;
        }

        return new DataRecord($messageId, $dst, $tag, $line);
    }

    private static function literal(string $text, int $position, string $literal): bool
    {
        $length = strlen($literal);
        if ($position < 0 || $position + $length > strlen($text)) {
            return false;
        }
        return substr_compare($text, $literal, $position, $length) === 0;
    }

    private static function skipWhitespace(string $text, int $i, int $length): int
    {
        while ($i < $length) {
            $c = $text[$i];
            if ($c !== ' ' && $c !== "\t" && $c !== "\n" && $c !== "\r") {
                break;
            }
            $i++;
        }
        return $i;
    }

    /** Chapters 5.2 and 8.1, the body is exactly one JSON object and no further bytes. */
    private static function atEnd(string $text, int $i, int $length): bool
    {
        return $i === $length;
    }

    /** Reads a plain ASCII JSON string, escape sequences are rejected. */
    private static function readString(string $text, int &$i, int $length): ?string
    {
        if ($i >= $length || $text[$i] !== '"') {
            return null;
        }
        $i++;
        $start = $i;
        while ($i < $length) {
            $c = $text[$i];
            if ($c === '"') {
                $value = substr($text, $start, $i - $start);
                $i++;
                return $value;
            }
            $code = ord($c);
            if ($c === '\\' || $code < 0x20 || $code > 0x7E) {
                return null;
            }
            $i++;
        }
        return null;
    }
}

// ---------------------------------------------------------------------------
// 8. Pure PHP AES-256-GCM, chapter 7.4. No crypto extension is used anywhere.
// ---------------------------------------------------------------------------

final class AesGcm
{
    private const SBOX_HEX =
        '637c777bf26b6fc53001672bfed7ab76' .
        'ca82c97dfa5947f0add4a2af9ca472c0' .
        'b7fd9326363ff7cc34a5e5f171d83115' .
        '04c723c31896059a071280e2eb27b275' .
        '09832c1a1b6e5aa0523bd6b329e32f84' .
        '53d100ed20fcb15b6acbbe394a4c58cf' .
        'd0efaafb434d338545f9027f503c9fa8' .
        '51a3408f929d38f5bcb6da2110fff3d2' .
        'cd0c13ec5f974417c4a77e3d645d1973' .
        '60814fdc222a908846eeb814de5e0bdb' .
        'e0323a0a4906245cc2d3ac629195e479' .
        'e7c8376d8dd54ea96c56f4ea657aae08' .
        'ba78252e1ca6b4c6e8dd741f4bbd8b8a' .
        '703eb5664803f60e613557b986c11d9e' .
        'e1f8981169d98e949b1e87e9ce5528df' .
        '8ca1890dbfe6426841992d0fb054bb16';

    private const RCON = [0, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40];

    private const LAST4 = [
        0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
        0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0,
    ];

    /** @var int[]|null */
    private static ?array $sbox = null;
    /** @var int[]|null */
    private static ?array $te0 = null;
    private static ?bool $selfTestResult = null;
    private static bool $selfTestRunning = false;

    /** @var int[] */
    private array $roundKeys;
    /** @var array<int,int[]> */
    private array $ghashTable;

    public function __construct(string $key)
    {
        if (strlen($key) !== 32) {
            throw new \InvalidArgumentException('aes key length');
        }
        self::initTables();
        if (self::$selfTestResult === null && !self::$selfTestRunning) {
            self::selfTest();
        }
        $this->roundKeys = self::expandKey($key);
        $this->ghashTable = $this->buildGhashTable();
    }

    /** Runs the mandatory known answer test once per process. */
    public static function selfTest(): bool
    {
        if (self::$selfTestResult !== null) {
            return self::$selfTestResult;
        }
        self::$selfTestRunning = true;
        $ok = true;
        try {
            // NIST GCM test case 13, AES-256, empty plaintext and empty AAD.
            $gcm = new self(str_repeat("\x00", 32));
            [$ciphertext, $tag] = $gcm->seal(str_repeat("\x00", 12), '', '');
            $ok = $ok && $ciphertext === '' && bin2hex($tag) === '530f8afbc74536b9a963b4f1c4cb738b';

            // NIST GCM test case 14, AES-256, single zero block, empty AAD.
            [$ciphertext, $tag] = $gcm->seal(str_repeat("\x00", 12), str_repeat("\x00", 16), '');
            $ok = $ok && bin2hex($ciphertext) === 'cea7403d4d606b6e074ec5d3baf39d18'
                && bin2hex($tag) === 'd0d1c8a799996bf0265b98b5d48ab919';

            // NIST GCM test case 16, AES-256, multi block plaintext with AAD.
            $gcm = new self(hex2bin('feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308'));
            $plaintext = hex2bin(
                'd9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72'
                . '1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39'
            );
            $aad = hex2bin('feedfacedeadbeeffeedfacedeadbeefabaddad2');
            [$ciphertext, $tag] = $gcm->seal(hex2bin('cafebabefacedbaddecaf888'), $plaintext, $aad);
            $ok = $ok && bin2hex($ciphertext) === '522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa'
                . '8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662'
                && bin2hex($tag) === '76fc6ece0f4e1768cddf8853bb2d551b';

            // Authenticated decryption must round trip and must reject a modified tag.
            $ok = $ok && $gcm->open(hex2bin('cafebabefacedbaddecaf888'), $ciphertext, $tag, $aad) === $plaintext;
            $broken = $tag;
            $broken[0] = chr(ord($broken[0]) ^ 0x01);
            $ok = $ok && $gcm->open(hex2bin('cafebabefacedbaddecaf888'), $ciphertext, $broken, $aad) === null;
        } catch (\Throwable) {
            $ok = false;
        }
        self::$selfTestRunning = false;
        self::$selfTestResult = $ok;
        return $ok;
    }

    /**
     * @return array{0:string,1:string} ciphertext and 16 byte tag
     */
    public function seal(string $iv, string $plaintext, string $aad): array
    {
        $counter = $this->initialCounter($iv);
        $ciphertext = $plaintext === '' ? '' : $this->gctr($counter, $plaintext);
        $s = $this->ghash($aad, $ciphertext);
        $ek = $this->encryptWords($counter[0], $counter[1], $counter[2], $counter[3]);
        $tag = pack('N4', $s[0] ^ $ek[0], $s[1] ^ $ek[1], $s[2] ^ $ek[2], $s[3] ^ $ek[3]);
        return [$ciphertext, $tag];
    }

    /** Returns the plaintext only after the whole tag verified, otherwise null. */
    public function open(string $iv, string $ciphertext, string $tag, string $aad): ?string
    {
        if (strlen($tag) !== 16) {
            return null;
        }
        $counter = $this->initialCounter($iv);
        $s = $this->ghash($aad, $ciphertext);
        $ek = $this->encryptWords($counter[0], $counter[1], $counter[2], $counter[3]);
        $expected = pack('N4', $s[0] ^ $ek[0], $s[1] ^ $ek[1], $s[2] ^ $ek[2], $s[3] ^ $ek[3]);
        if (!hash_equals($expected, $tag)) {
            return null;
        }
        return $ciphertext === '' ? '' : $this->gctr($counter, $ciphertext);
    }

    /** GMAC, AES-GCM over an empty plaintext. */
    public function mac(string $iv, string $aad): string
    {
        return $this->seal($iv, '', $aad)[1];
    }

    /** @return int[] */
    private function initialCounter(string $iv): array
    {
        if (strlen($iv) !== 12) {
            throw new \InvalidArgumentException('iv length');
        }
        $words = unpack('N3', $iv);
        return [$words[1], $words[2], $words[3], 1];
    }

    /** @param int[] $j0 */
    private function gctr(array $j0, string $data): string
    {
        $length = strlen($data);
        $c0 = $j0[0];
        $c1 = $j0[1];
        $c2 = $j0[2];
        $c3 = $j0[3];
        $out = '';
        for ($offset = 0; $offset < $length; $offset += 16) {
            $c3 = ($c3 + 1) & 0xFFFFFFFF;
            $ks = $this->encryptWords($c0, $c1, $c2, $c3);
            $keyStream = pack('N4', $ks[0], $ks[1], $ks[2], $ks[3]);
            $out .= substr($data, $offset, 16) ^ $keyStream;
        }
        return $out;
    }

    /** @return int[] */
    private function ghash(string $aad, string $ciphertext): array
    {
        $y = [0, 0, 0, 0];
        $y = $this->ghashBlocks($y, $aad);
        $y = $this->ghashBlocks($y, $ciphertext);
        $aadBits = strlen($aad) * 8;
        $ctBits = strlen($ciphertext) * 8;
        $y[0] ^= ($aadBits >> 32) & 0xFFFFFFFF;
        $y[1] ^= $aadBits & 0xFFFFFFFF;
        $y[2] ^= ($ctBits >> 32) & 0xFFFFFFFF;
        $y[3] ^= $ctBits & 0xFFFFFFFF;
        return $this->multiplyH($y);
    }

    /**
     * @param int[] $y
     * @return int[]
     */
    private function ghashBlocks(array $y, string $data): array
    {
        $length = strlen($data);
        for ($offset = 0; $offset < $length; $offset += 16) {
            $chunk = substr($data, $offset, 16);
            if (strlen($chunk) < 16) {
                $chunk = str_pad($chunk, 16, "\x00");
            }
            $w = unpack('N4', $chunk);
            $y[0] ^= $w[1];
            $y[1] ^= $w[2];
            $y[2] ^= $w[3];
            $y[3] ^= $w[4];
            $y = $this->multiplyH($y);
        }
        return $y;
    }

    /**
     * Multiplies the 128 bit value by H in GF(2^128) using a 4 bit table.
     *
     * @param int[] $x
     * @return int[]
     */
    private function multiplyH(array $x): array
    {
        $table = $this->ghashTable;
        $bytes = [];
        for ($w = 0; $w < 4; $w++) {
            $v = $x[$w];
            $bytes[] = ($v >> 24) & 0xFF;
            $bytes[] = ($v >> 16) & 0xFF;
            $bytes[] = ($v >> 8) & 0xFF;
            $bytes[] = $v & 0xFF;
        }
        $entry = $table[$bytes[15] & 0x0F];
        $z0 = $entry[0];
        $z1 = $entry[1];
        $z2 = $entry[2];
        $z3 = $entry[3];
        for ($i = 15; $i >= 0; $i--) {
            $lo = $bytes[$i] & 0x0F;
            $hi = ($bytes[$i] >> 4) & 0x0F;
            if ($i !== 15) {
                $rem = $z3 & 0x0F;
                $z3 = (($z3 >> 4) | (($z2 & 0x0F) << 28)) & 0xFFFFFFFF;
                $z2 = (($z2 >> 4) | (($z1 & 0x0F) << 28)) & 0xFFFFFFFF;
                $z1 = (($z1 >> 4) | (($z0 & 0x0F) << 28)) & 0xFFFFFFFF;
                $z0 = ($z0 >> 4) ^ (self::LAST4[$rem] << 16);
                $entry = $table[$lo];
                $z0 ^= $entry[0];
                $z1 ^= $entry[1];
                $z2 ^= $entry[2];
                $z3 ^= $entry[3];
            }
            $rem = $z3 & 0x0F;
            $z3 = (($z3 >> 4) | (($z2 & 0x0F) << 28)) & 0xFFFFFFFF;
            $z2 = (($z2 >> 4) | (($z1 & 0x0F) << 28)) & 0xFFFFFFFF;
            $z1 = (($z1 >> 4) | (($z0 & 0x0F) << 28)) & 0xFFFFFFFF;
            $z0 = ($z0 >> 4) ^ (self::LAST4[$rem] << 16);
            $entry = $table[$hi];
            $z0 ^= $entry[0];
            $z1 ^= $entry[1];
            $z2 ^= $entry[2];
            $z3 ^= $entry[3];
        }
        return [$z0, $z1, $z2, $z3];
    }

    /** @return array<int,int[]> */
    private function buildGhashTable(): array
    {
        $h = $this->encryptWords(0, 0, 0, 0);
        $table = array_fill(0, 16, [0, 0, 0, 0]);
        $table[8] = $h;
        for ($i = 4; $i > 0; $i >>= 1) {
            $v = $table[$i * 2];
            $carry = $v[3] & 1;
            $n3 = (($v[3] >> 1) | (($v[2] & 1) << 31)) & 0xFFFFFFFF;
            $n2 = (($v[2] >> 1) | (($v[1] & 1) << 31)) & 0xFFFFFFFF;
            $n1 = (($v[1] >> 1) | (($v[0] & 1) << 31)) & 0xFFFFFFFF;
            $n0 = ($v[0] >> 1);
            if ($carry === 1) {
                $n0 ^= 0xE1000000;
            }
            $table[$i] = [$n0, $n1, $n2, $n3];
        }
        for ($i = 2; $i <= 8; $i <<= 1) {
            $base = $table[$i];
            for ($j = 1; $j < $i; $j++) {
                $table[$i + $j] = [
                    $base[0] ^ $table[$j][0],
                    $base[1] ^ $table[$j][1],
                    $base[2] ^ $table[$j][2],
                    $base[3] ^ $table[$j][3],
                ];
            }
        }
        return $table;
    }

    /** @return int[] */
    private function encryptWords(int $s0, int $s1, int $s2, int $s3): array
    {
        $rk = $this->roundKeys;
        $te = self::$te0;
        $sb = self::$sbox;

        $s0 ^= $rk[0];
        $s1 ^= $rk[1];
        $s2 ^= $rk[2];
        $s3 ^= $rk[3];

        $k = 4;
        for ($round = 1; $round < 14; $round++) {
            $a = $te[($s0 >> 24) & 0xFF];
            $b = $te[($s1 >> 16) & 0xFF];
            $c = $te[($s2 >> 8) & 0xFF];
            $d = $te[$s3 & 0xFF];
            $t0 = ($a ^ (($b >> 8) | ($b << 24)) ^ (($c >> 16) | ($c << 16)) ^ (($d >> 24) | ($d << 8))) & 0xFFFFFFFF;

            $a = $te[($s1 >> 24) & 0xFF];
            $b = $te[($s2 >> 16) & 0xFF];
            $c = $te[($s3 >> 8) & 0xFF];
            $d = $te[$s0 & 0xFF];
            $t1 = ($a ^ (($b >> 8) | ($b << 24)) ^ (($c >> 16) | ($c << 16)) ^ (($d >> 24) | ($d << 8))) & 0xFFFFFFFF;

            $a = $te[($s2 >> 24) & 0xFF];
            $b = $te[($s3 >> 16) & 0xFF];
            $c = $te[($s0 >> 8) & 0xFF];
            $d = $te[$s1 & 0xFF];
            $t2 = ($a ^ (($b >> 8) | ($b << 24)) ^ (($c >> 16) | ($c << 16)) ^ (($d >> 24) | ($d << 8))) & 0xFFFFFFFF;

            $a = $te[($s3 >> 24) & 0xFF];
            $b = $te[($s0 >> 16) & 0xFF];
            $c = $te[($s1 >> 8) & 0xFF];
            $d = $te[$s2 & 0xFF];
            $t3 = ($a ^ (($b >> 8) | ($b << 24)) ^ (($c >> 16) | ($c << 16)) ^ (($d >> 24) | ($d << 8))) & 0xFFFFFFFF;

            $s0 = $t0 ^ $rk[$k];
            $s1 = $t1 ^ $rk[$k + 1];
            $s2 = $t2 ^ $rk[$k + 2];
            $s3 = $t3 ^ $rk[$k + 3];
            $k += 4;
        }

        $o0 = (($sb[($s0 >> 24) & 0xFF] << 24) | ($sb[($s1 >> 16) & 0xFF] << 16)
            | ($sb[($s2 >> 8) & 0xFF] << 8) | $sb[$s3 & 0xFF]) ^ $rk[56];
        $o1 = (($sb[($s1 >> 24) & 0xFF] << 24) | ($sb[($s2 >> 16) & 0xFF] << 16)
            | ($sb[($s3 >> 8) & 0xFF] << 8) | $sb[$s0 & 0xFF]) ^ $rk[57];
        $o2 = (($sb[($s2 >> 24) & 0xFF] << 24) | ($sb[($s3 >> 16) & 0xFF] << 16)
            | ($sb[($s0 >> 8) & 0xFF] << 8) | $sb[$s1 & 0xFF]) ^ $rk[58];
        $o3 = (($sb[($s3 >> 24) & 0xFF] << 24) | ($sb[($s0 >> 16) & 0xFF] << 16)
            | ($sb[($s1 >> 8) & 0xFF] << 8) | $sb[$s2 & 0xFF]) ^ $rk[59];

        return [$o0 & 0xFFFFFFFF, $o1 & 0xFFFFFFFF, $o2 & 0xFFFFFFFF, $o3 & 0xFFFFFFFF];
    }

    /** @return int[] */
    private static function expandKey(string $key): array
    {
        $words = array_values(unpack('N8', $key));
        $sb = self::$sbox;
        for ($i = 8; $i < 60; $i++) {
            $t = $words[$i - 1];
            if ($i % 8 === 0) {
                $t = (($t << 8) | ($t >> 24)) & 0xFFFFFFFF;
                $t = ($sb[($t >> 24) & 0xFF] << 24) | ($sb[($t >> 16) & 0xFF] << 16)
                    | ($sb[($t >> 8) & 0xFF] << 8) | $sb[$t & 0xFF];
                $t ^= self::RCON[intdiv($i, 8)] << 24;
            } elseif ($i % 8 === 4) {
                $t = ($sb[($t >> 24) & 0xFF] << 24) | ($sb[($t >> 16) & 0xFF] << 16)
                    | ($sb[($t >> 8) & 0xFF] << 8) | $sb[$t & 0xFF];
            }
            $words[$i] = ($words[$i - 8] ^ $t) & 0xFFFFFFFF;
        }
        return $words;
    }

    private static function initTables(): void
    {
        if (self::$te0 !== null) {
            return;
        }
        $raw = hex2bin(self::SBOX_HEX);
        $sbox = [];
        $te0 = [];
        for ($i = 0; $i < 256; $i++) {
            $s = ord($raw[$i]);
            $sbox[$i] = $s;
            $s2 = ($s << 1) & 0xFF;
            if (($s & 0x80) !== 0) {
                $s2 ^= 0x1B;
            }
            $s3 = $s2 ^ $s;
            $te0[$i] = (($s2 << 24) | ($s << 16) | ($s << 8) | $s3) & 0xFFFFFFFF;
        }
        self::$sbox = $sbox;
        self::$te0 = $te0;
    }
}

// ---------------------------------------------------------------------------
// 9. Configuration
// ---------------------------------------------------------------------------

final class ConfigException extends \RuntimeException
{
}

final class Config
{
    /**
     * @param array<string,string> $accessKeys address to raw 32 byte key
     */
    private function __construct(
        public readonly string $configuredSecret,
        public readonly string $serverSecret,
        public readonly array $accessKeys,
        public readonly string $bufferDirectory,
    ) {
    }

    // @wp-skip-begin Standalone configuration header, the WordPress plugin passes values.
    public static function fromConstants(): self
    {
        return self::fromValues(SERVER_SECRET, ACCESS_KEYS, self::resolveBufferDirectory(BUFFER_DIRECTORY));
    }
    // @wp-skip-end

    /**
     * Validates one configuration. Embedding hosts, for example the WordPress plugin, call it directly.
     *
     * @param array<mixed,mixed> $hexAccessKeys address to 64 lowercase hexadecimal characters
     * @param string $bufferDirectory absolute storage root
     */
    public static function fromValues(string $configured, array $hexAccessKeys, string $bufferDirectory): self
    {
        // Chapter 3.2, only an exactly empty string is normalized.
        $secret = $configured;
        if ($secret === '') {
            $secret = 'CHANGE_THIS_SERVER_SECRET';
        }

        if (PHP_INT_SIZE < 8) {
            throw new ConfigException('int size');
        }
        if (STORAGE_DRIVER !== 'disk') {
            throw new ConfigException('storage driver');
        }
        if (MIN_DATA_POLL_INTERVAL_MS <= 0 || MIN_DATA_POLL_INTERVAL_MS > MAX_DATA_POLL_INTERVAL_MS) {
            throw new ConfigException('poll interval');
        }
        if (QUEUE_TTL_SECONDS <= 0 || AUTH_TIMESTAMP_SKEW_SECONDS <= 0 || LOCK_TIMEOUT_MS <= 0) {
            throw new ConfigException('timing');
        }
        if (MAX_DATA_RECORDS_PER_REQUEST < 1 || MAX_AUTH_RECORD_BYTES < 256) {
            throw new ConfigException('record limits');
        }
        // The body must hold the auth line and the maximum number of minimal data records.
        if (MAX_REQUEST_BODY_BYTES < MAX_AUTH_RECORD_BYTES + MAX_DATA_RECORDS_PER_REQUEST * 160) {
            throw new ConfigException('body limit');
        }
        if (MAX_QUEUE_BYTES_PER_CLIENT < 1 || MAX_INFO_PLAINTEXT_BYTES < 1024) {
            throw new ConfigException('capacity limits');
        }

        $keys = [];
        foreach ($hexAccessKeys as $address => $hex) {
            if (!is_string($address) || !Validators::isVpnAddress($address)) {
                throw new ConfigException('access key address');
            }
            if (!is_string($hex) || strlen($hex) !== 64 || strspn($hex, '0123456789abcdef') !== 64) {
                throw new ConfigException('access key value');
            }
            $keys[$address] = (string) hex2bin($hex);
        }

        return new self($configured, $secret, $keys, rtrim($bufferDirectory, '\\/'));
    }

    public function withBufferDirectory(string $directory): self
    {
        return new self($this->configuredSecret, $this->serverSecret, $this->accessKeys, $directory);
    }

    public function withSecret(string $configured): self
    {
        $secret = $configured === '' ? 'CHANGE_THIS_SERVER_SECRET' : $configured;
        return new self($configured, $secret, $this->accessKeys, $this->bufferDirectory);
    }

    /** @param array<string,string> $hexKeys */
    public function withAccessKeys(array $hexKeys): self
    {
        $keys = [];
        foreach ($hexKeys as $address => $hex) {
            $keys[$address] = (string) hex2bin($hex);
        }
        return new self($this->configuredSecret, $this->serverSecret, $keys, $this->bufferDirectory);
    }

    public function accessKey(string $address): ?string
    {
        return $this->accessKeys[$address] ?? null;
    }

    /** @return string[] configured client addresses ordered by numeric value */
    public function clientAddresses(): array
    {
        $addresses = array_keys($this->accessKeys);
        usort($addresses, static fn(string $a, string $b): int
            => Validators::addressSortKey($a) <=> Validators::addressSortKey($b));
        return $addresses;
    }

    /** True when the effective secret is publicly predictable, chapter 5.1. */
    public function isInsecureSecret(): bool
    {
        if ($this->configuredSecret === '') {
            return true;
        }
        $secret = $this->serverSecret;
        return $secret === 'CHANGE_THIS_SERVER_SECRET' || $secret === str_repeat("\x00", strlen($secret)) || $secret === str_repeat('0', strlen($secret));
    }

    // @wp-skip-begin
    private static function resolveBufferDirectory(string $path): string
    {
        if (preg_match('~^([A-Za-z]:[\\\\/]|\\\\\\\\|/)~', $path) === 1) {
            return rtrim($path, '\\/');
        }
        $relative = preg_replace('~^\./~', '', $path) ?? $path;
        return rtrim(__DIR__ . DIRECTORY_SEPARATOR . $relative, '\\/');
    }
    // @wp-skip-end
}

// ---------------------------------------------------------------------------
// 10. Key derivation and request authentication
// ---------------------------------------------------------------------------

final class KeyDerivation
{
    public const DUMMY_ACCESS_KEY = "\x9b\x1d\x36\xc7\x54\xa2\x0e\xf8\x63\x41\xd9\x25\x7c\x0b\xae\x52"
        . "\x18\x77\xf3\x4c\x2a\x90\x6d\xbb\x05\xe4\x39\x81\xca\x6f\x17\x28";

    public static function semiKey(string $serverSecret): string
    {
        $salt = hash('sha256', 'phpvpn/auth-semi/salt', true);
        return hash_hkdf('sha256', $serverSecret, 32, 'phpvpn/auth-semi/key', $salt);
    }

    public static function fullAuthKey(string $serverSecret, string $accessKey, string $src): string
    {
        $salt = hash_hmac('sha256', "phpvpn/auth-full/salt\x00" . $src, $serverSecret, true);
        return hash_hkdf('sha256', $accessKey, 32, "phpvpn/auth-full/key\x00" . $src, $salt);
    }

    public static function healthKey(string $serverSecret, string $requestId): string
    {
        $salt = hash('sha256', "phpvpn/health-response/salt\x00" . $requestId, true);
        return hash_hkdf('sha256', $serverSecret, 32, "phpvpn/health-response/key\x00" . $requestId, $salt);
    }

    public static function infoKey(string $serverSecret, string $salt, string $requestId): string
    {
        return hash_hkdf('sha256', $serverSecret, 32, 'phpvpn/info-response/' . $requestId, $salt);
    }
}

final class Authenticator
{
    public static function infoFullCanonical(string $src, string $timestamp, string $requestId, string $extensionHash): string
    {
        return "PHPVPN-AUTH-FULL\nPOST\ninfo\n$src\n$timestamp\n$requestId\n$extensionHash";
    }
    public static function semiCanonical(string $timestamp, string $requestId): string
    {
        return "PHPVPN-AUTH-SEMI\nPOST\ninfo\n" . $timestamp . "\n" . $requestId;
    }

    public static function fullCanonical(string $src, string $timestamp, string $requestId, string $bodyHashHex): string
    {
        return "PHPVPN-AUTH-FULL\nPOST\ndata\n" . $src . "\n" . $timestamp . "\n" . $requestId . "\n" . $bodyHashHex;
    }

    public static function healthAad(string $requestId, string $timestamp, string $ivBase64): string
    {
        return "PHPVPN-HEALTH-RESPONSE\nhealth\n" . $requestId . "\n" . SERVER_VERSION
            . "\nrunning\nOK\n" . $timestamp . "\n" . $ivBase64;
    }

    public static function infoAad(string $requestId, string $saltBase64, string $ivBase64): string
    {
        return "PHPVPN-INFO\ninfo\n" . $requestId . "\n" . $saltBase64 . "\n" . $ivBase64;
    }

    /** True when the timestamp is inside the allowed window, compared in milliseconds. */
    public static function withinSkew(int $timestampMs, int $nowMs): bool
    {
        return abs($nowMs - $timestampMs) <= AUTH_TIMESTAMP_SKEW_SECONDS * 1000;
    }
}

// ---------------------------------------------------------------------------
// 11. Storage
// ---------------------------------------------------------------------------

final class ClaimedBatch
{
    /**
     * @param array<int,array{0:string,1:string}> $pairs messageId and gcm tag pairs
     */
    public function __construct(
        public readonly string $path,
        public readonly int $offset,
        public readonly int $bytes,
        public readonly array $pairs,
    ) {
    }
}

/** The storage lock wait expired, distinct from a full destination queue. */
final class QueueBusy extends \RuntimeException
{
}

interface Storage
{
    public function isUsable(): bool;

    /**
     * @param string[] $lines serialized data lines without the terminating LF
     * @param array<int,array{0:string,1:string}> $pairs
     */
    public function enqueue(string $dst, string $src, array $lines, array $pairs, int $expiresAtMs): bool;

    /** @return ClaimedBatch[] */
    public function drainAll(string $client, int $nowMs): array;

    public function usage(string $client, int $nowMs): int;

    public function sweepQueue(string $dst, int $nowMs): void;

    public function sweep(int $nowMs, int $budgetMs): void;

    public function readLastSeenMs(string $client): ?int;

    public function publishMaskedContracts(string $client, array $contracts, int $nowMs): bool;

    public function readClientInfoSnapshot(string $client): array;

    public function touchLastSeenBestEffort(string $client, int $nowMs): void;
}

// Queue files need locks and atomic renames, see section 3. The HTTP layer below streams
// php://input, php://output and php://temp, which are not files of the site.
// phpcs:disable WordPress.WP.AlternativeFunctions
final class DiskStorage implements Storage
{
    private const BATCH_PATTERN =
        '/^(\d{4})-(\d{2})-(\d{2})T(\d{2})-(\d{2})-(\d{2})\.(\d{3})Z_(\d{1,12})_([0-9.]{7,15})_'
        . '([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})\.(receiving|data|sending)$/';

    private const MARKER = 'info.json';
    private const LOCK = '.lock';
    private const SWEEP_MARKER = '.sweep';

    public function __construct(private readonly string $root)
    {
    }

    public function isUsable(): bool
    {
        if (Fs::isDir($this->root)) {
            return true;
        }
        if (!Fs::ensureDir($this->root)) {
            return false;
        }
        // Guards are written once, when the root is created, never on the request hot path.
        if (PROTECT_BUFFER_DIRECTORY) {
            $this->writeGuards();
        }
        return true;
    }

    public function enqueue(string $dst, string $src, array $lines, array $pairs, int $expiresAtMs): bool
    {
        if ($lines === []) {
            return true;
        }
        $directory = $this->clientDirectory($dst);
        if (!Fs::ensureDir($directory)) {
            return false;
        }

        $bytes = 0;
        foreach ($lines as $line) {
            $bytes += strlen($line) + 1;
        }

        $lock = $this->acquireLock($directory);
        if ($lock === null) {
            return false;
        }
        try {
            $nowMs = $expiresAtMs - QUEUE_TTL_SECONDS * 1000;
            if ($this->usage($dst, $nowMs) + $bytes > MAX_QUEUE_BYTES_PER_CLIENT) {
                // Chapter 10.2, exactly one capacity driven sweep and one retry per destination.
                $this->sweepQueue($dst, $nowMs);
                if ($this->usage($dst, $nowMs) + $bytes > MAX_QUEUE_BYTES_PER_CLIENT) {
                    return false;
                }
            }
            return $this->writeBatch($directory, $src, $lines, $pairs, $bytes, $expiresAtMs);
        } finally {
            $this->releaseLock($lock);
        }
    }

    public function drainAll(string $client, int $nowMs): array
    {
        $directory = $this->clientDirectory($client);
        if (!Fs::ensureDir($directory)) {
            return [];
        }
        $entries = Fs::listDir($directory);
        if ($entries === null) {
            return [];
        }

        $candidates = [];
        foreach ($entries as $entry) {
            $parsed = self::parseBatchName($entry);
            if ($parsed === null || $parsed['state'] !== 'data' || $parsed['expiresAtMs'] <= $nowMs) {
                continue;
            }
            $candidates[] = $entry;
        }
        sort($candidates, SORT_STRING);

        $claimed = [];
        foreach ($candidates as $entry) {
            $from = $directory . DIRECTORY_SEPARATOR . $entry;
            $base = substr($entry, 0, -strlen('.data'));
            $to = $directory . DIRECTORY_SEPARATOR . $base . '.sending';
            if (!Fs::rename($from, $to)) {
                continue;
            }
            $batch = $this->readClaimed($to);
            if ($batch === null) {
                Fs::unlink($to);
                continue;
            }
            $claimed[] = $batch;
        }
        return $claimed;
    }

    public function usage(string $client, int $nowMs): int
    {
        $entries = Fs::listDir($this->clientDirectory($client));
        if ($entries === null) {
            return 0;
        }
        $total = 0;
        foreach ($entries as $entry) {
            $parsed = self::parseBatchName($entry);
            if ($parsed === null || $parsed['expiresAtMs'] <= $nowMs) {
                continue;
            }
            if ($parsed['state'] === 'data' || $parsed['state'] === 'receiving') {
                $total += $parsed['bytes'];
            }
        }
        return $total;
    }

    public function sweepQueue(string $dst, int $nowMs): void
    {
        $directory = $this->clientDirectory($dst);
        $entries = Fs::listDir($directory);
        if ($entries === null) {
            return;
        }
        foreach ($entries as $entry) {
            $parsed = self::parseBatchName($entry);
            if ($parsed === null || $parsed['expiresAtMs'] > $nowMs) {
                continue;
            }
            Fs::unlink($directory . DIRECTORY_SEPARATOR . $entry);
        }
    }

    public function sweep(int $nowMs, int $budgetMs): void
    {
        $marker = $this->root . DIRECTORY_SEPARATOR . self::SWEEP_MARKER;
        $mtime = Fs::mtime($marker);
        if ($mtime !== null && ($nowMs - $mtime * 1000) < SWEEP_MIN_INTERVAL_SECONDS * 1000) {
            return;
        }
        if (!Fs::touch($marker)) {
            return;
        }

        $directories = Fs::listDir($this->root);
        if ($directories === null || $directories === []) {
            return;
        }
        $count = count($directories);
        $start = random_int(0, $count - 1);
        $deadline = microtime(true) + ($budgetMs / 1000.0);
        $visited = 0;
        $files = 0;

        for ($i = 0; $i < $count && $visited < SWEEP_MAX_DIRECTORIES; $i++) {
            $name = $directories[($start + $i) % $count];
            $path = $this->root . DIRECTORY_SEPARATOR . $name;
            if (!Fs::isDir($path)) {
                continue;
            }
            $visited++;
            $entries = Fs::listDir($path);
            if ($entries === null) {
                continue;
            }
            foreach ($entries as $entry) {
                if ($files >= SWEEP_MAX_FILES || microtime(true) > $deadline) {
                    return;
                }
                $files++;
                if (preg_match('/\A\.info-([0-9a-f-]{36})\.tmp\z/D', $entry, $match) === 1 && Validators::isUuidV4($match[1])) {
                    $tempPath = $path . DIRECTORY_SEPARATOR . $entry;
                    $tempMtime = Fs::mtime($tempPath);
                    if ($tempMtime !== null && $nowMs - $tempMtime * 1000 > INFO_TEMP_TTL_SECONDS * 1000) Fs::unlink($tempPath);
                    continue;
                }
                $parsed = self::parseBatchName($entry);
                if ($parsed === null || $parsed['expiresAtMs'] > $nowMs) {
                    continue;
                }
                Fs::unlink($path . DIRECTORY_SEPARATOR . $entry);
            }
        }
    }

    public function readLastSeenMs(string $client): ?int
    {
        $marker = $this->clientDirectory($client) . DIRECTORY_SEPARATOR . self::MARKER;
        $mtime = Fs::mtime($marker);
        return $mtime === null ? null : $mtime * 1000;
    }

    public function publishMaskedContracts(string $client, array $contracts, int $nowMs): bool
    {
        $json = json_encode(['maskedCommunicationContracts' => $contracts], JSON_UNESCAPED_SLASHES);
        if ($json === false || strlen($json) > MAX_INFO_REQUEST_BODY_BYTES) return false;
        $directory = $this->clientDirectory($client);
        if (!Fs::ensureDir($directory)) return false;
        $temp = $directory . DIRECTORY_SEPARATOR . '.info-' . Validators::newUuidV4() . '.tmp';
        $handle = @fopen($temp, 'xb');
        if ($handle === false) return false;
        $ok = self::writeAll($handle, $json) && fflush($handle) && Fs::touch($temp, intdiv($nowMs, 1000));
        $ok = fclose($handle) && $ok;
        if ($ok) $ok = Fs::rename($temp, $directory . DIRECTORY_SEPARATOR . self::MARKER);
        if (!$ok) Fs::unlink($temp);
        return $ok;
    }

    public function readClientInfoSnapshot(string $client): array
    {
        $empty = ['lastSeen' => null, 'contracts' => []];
        $handle = @fopen($this->clientDirectory($client) . DIRECTORY_SEPARATOR . self::MARKER, 'rb');
        if ($handle === false) return $empty;
        $stat = fstat($handle);
        $body = stream_get_contents($handle, MAX_INFO_REQUEST_BODY_BYTES + 1);
        fclose($handle);
        if ($stat === false || !isset($stat['mtime'])) return $empty;
        $empty['lastSeen'] = $stat['mtime'] * 1000;
        if ($body === false) return $empty;
        $object = JsonWire::parseInfoObject($body);
        if ($object === null || count(get_object_vars($object)) !== 1 || !property_exists($object, 'maskedCommunicationContracts')) return $empty;
        $contracts = JsonWire::maskedContracts($object->maskedCommunicationContracts, $client);
        if (is_array($contracts)) $empty['contracts'] = $contracts;
        return $empty;
    }

    public function touchLastSeenBestEffort(string $client, int $nowMs): void
    {
        // Chapter 10.6, at most one metadata read and at most one touch, never a lock or a retry.
        $directory = $this->clientDirectory($client);
        Fs::ensureDir($directory);
        $marker = $directory . DIRECTORY_SEPARATOR . self::MARKER;
        $mtime = Fs::mtime($marker);
        if ($mtime !== null) {
            $ageMs = $nowMs - $mtime * 1000;
            if ($ageMs <= CLIENT_STATS_MIN_UPDATE_SECONDS * 1000) {
                return;
            }
        }
        Fs::touch($marker);
    }

    public function clientDirectory(string $address): string
    {
        return $this->root . DIRECTORY_SEPARATOR . Validators::addressToDirectory($address);
    }

    /**
     * @param string[] $lines
     * @param array<int,array{0:string,1:string}> $pairs
     */
    private function writeBatch(
        string $directory,
        string $src,
        array $lines,
        array $pairs,
        int $bytes,
        int $expiresAtMs
    ): bool {
        $base = Iso8601::formatFileNameMs($expiresAtMs) . '_' . $bytes . '_' . $src . '_' . Validators::newUuidV4();
        $temporary = $directory . DIRECTORY_SEPARATOR . $base . '.receiving';
        $final = $directory . DIRECTORY_SEPARATOR . $base . '.data';

        $index = json_encode(['v' => 1, 'bytes' => $bytes, 'records' => $pairs], JSON_UNESCAPED_SLASHES);
        if ($index === false) {
            return false;
        }

        $handle = @fopen($temporary, 'xb');
        if ($handle === false) {
            return false;
        }
        $ok = self::writeAll($handle, $index . "\n");
        foreach ($lines as $line) {
            if (!$ok) {
                break;
            }
            $ok = self::writeAll($handle, $line . "\n");
        }
        if ($ok) {
            $ok = @fflush($handle);
        }
        @fclose($handle);
        if (!$ok || !Fs::rename($temporary, $final)) {
            Fs::unlink($temporary);
            return false;
        }
        return true;
    }

    private static function writeAll($handle, string $data): bool
    {
        $length = strlen($data);
        $written = 0;
        while ($written < $length) {
            $result = @fwrite($handle, $written === 0 ? $data : substr($data, $written));
            if ($result === false || $result === 0) {
                return false;
            }
            $written += $result;
        }
        return true;
    }

    private function readClaimed(string $path): ?ClaimedBatch
    {
        $handle = @fopen($path, 'rb');
        if ($handle === false) {
            return null;
        }
        $header = @fgets($handle, 1024 * 1024);
        if ($header === false) {
            @fclose($handle);
            return null;
        }
        $offset = @ftell($handle);
        @fclose($handle);
        if ($offset === false) {
            return null;
        }

        $decoded = json_decode(rtrim($header, "\n"), true);
        if (!is_array($decoded) || !isset($decoded['records']) || !is_array($decoded['records'])) {
            return null;
        }
        $pairs = [];
        foreach ($decoded['records'] as $pair) {
            if (!is_array($pair) || count($pair) !== 2 || !is_string($pair[0]) || !is_string($pair[1])) {
                return null;
            }
            $pairs[] = [$pair[0], $pair[1]];
        }
        $parsed = self::parseBatchName(basename($path));
        if ($parsed === null) {
            return null;
        }
        return new ClaimedBatch($path, $offset, $parsed['bytes'], $pairs);
    }

    /** @return resource|null */
    private function acquireLock(string $directory)
    {
        $handle = @fopen($directory . DIRECTORY_SEPARATOR . self::LOCK, 'cb');
        if ($handle === false) {
            return null;
        }
        $deadline = microtime(true) + (LOCK_TIMEOUT_MS / 1000.0);
        $waitUs = 1000;
        while (true) {
            if (@flock($handle, LOCK_EX | LOCK_NB)) {
                return $handle;
            }
            $remainingUs = (int) (($deadline - microtime(true)) * 1000000);
            if ($remainingUs <= 0) {
                @fclose($handle);
                throw new QueueBusy();
            }
            usleep(min($waitUs, $remainingUs));
            $waitUs = min($waitUs * 2, 25000);
        }
    }

    /** @param resource $handle */
    private function releaseLock($handle): void
    {
        @flock($handle, LOCK_UN);
        @fclose($handle);
    }

    /** @return array{expiresAtMs:int,bytes:int,src:string,state:string}|null */
    public static function parseBatchName(string $name): ?array
    {
        if (preg_match(self::BATCH_PATTERN, $name, $m) !== 1) {
            return null;
        }
        $days = Iso8601::daysFromCivil((int) $m[1], (int) $m[2], (int) $m[3]);
        $expires = ((($days * 86400) + ((int) $m[4]) * 3600 + ((int) $m[5]) * 60 + (int) $m[6]) * 1000) + (int) $m[7];
        return [
            'expiresAtMs' => $expires,
            'bytes' => (int) $m[8],
            'src' => $m[9],
            'state' => $m[11],
        ];
    }

    /** Missing guards only, an existing file is never replaced. Embedding hosts call it for a folder that already exists. */
    public function writeGuards(): void
    {
        Fs::writeFileIfMissing(
            $this->root . DIRECTORY_SEPARATOR . '.htaccess',
            "Require all denied\n<IfModule !mod_authz_core.c>\nOrder allow,deny\nDeny from all\n</IfModule>\n"
        );
        Fs::writeFileIfMissing(
            $this->root . DIRECTORY_SEPARATOR . 'web.config',
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<configuration><system.webServer><security>"
            . "<requestFiltering><hiddenSegments><add segment=\".\" /></hiddenSegments></requestFiltering>"
            . "</security></system.webServer></configuration>\n"
        );
        Fs::writeFileIfMissing($this->root . DIRECTORY_SEPARATOR . 'index.html', '');
    }
}

// ---------------------------------------------------------------------------
// 12. HTTP layer. Every response is produced here and nowhere else.
// ---------------------------------------------------------------------------

interface ResponseWriter
{
    /** @param array<string,string> $extraHeaders */
    public function begin(int $status, string $contentType, array $extraHeaders = []): void;

    public function write(string $data): void;

    /** @param resource $stream */
    public function copyFrom($stream): int;

    public function isStarted(): bool;
}

final class HttpResponseWriter implements ResponseWriter
{
    private bool $started = false;
    /** @var resource|null */
    private $output = null;

    public function __construct(private readonly bool $suppressBody = false)
    {
    }

    public function begin(int $status, string $contentType, array $extraHeaders = []): void
    {
        if ($this->started) {
            return;
        }
        $this->started = true;
        while (ob_get_level() > 0) {
            @ob_end_clean();
        }
        http_response_code($status);
        header('Content-Type: ' . $contentType);
        // Chapter 4.1, mandatory on every response without exception.
        header('Cache-Control: no-store, no-cache, must-revalidate');
        header('Pragma: no-cache');
        header('X-Content-Type-Options: nosniff');
        foreach ($extraHeaders as $name => $value) {
            header($name . ': ' . $value);
        }
    }

    public function write(string $data): void
    {
        if ($this->suppressBody || $data === '') {
            return;
        }
        // Exact protocol bytes: JSON and NDJSON covered by HMAC tags, or HTML escaped by its producer.
        echo $data; // phpcs:ignore WordPress.Security.EscapeOutput.OutputNotEscaped
    }

    public function copyFrom($stream): int
    {
        if ($this->suppressBody) {
            return 0;
        }
        if ($this->output === null) {
            $handle = @fopen('php://output', 'wb');
            if ($handle === false) {
                return -1;
            }
            $this->output = $handle;
        }
        $copied = @stream_copy_to_stream($stream, $this->output);
        return $copied === false ? -1 : $copied;
    }

    public function isStarted(): bool
    {
        return $this->started;
    }
}

final class BufferedResponseWriter implements ResponseWriter
{
    public int $status = 0;
    /** @var array<string,string> */
    public array $headers = [];
    public string $body = '';
    private bool $started = false;

    public function begin(int $status, string $contentType, array $extraHeaders = []): void
    {
        if ($this->started) {
            return;
        }
        $this->started = true;
        $this->status = $status;
        $this->headers = [
            'Content-Type' => $contentType,
            'Cache-Control' => 'no-store, no-cache, must-revalidate',
            'Pragma' => 'no-cache',
            'X-Content-Type-Options' => 'nosniff',
        ];
        foreach ($extraHeaders as $name => $value) {
            $this->headers[$name] = $value;
        }
    }

    public function write(string $data): void
    {
        $this->body .= $data;
    }

    public function copyFrom($stream): int
    {
        $data = @stream_get_contents($stream);
        if ($data === false) {
            return -1;
        }
        $this->body .= $data;
        return strlen($data);
    }

    public function isStarted(): bool
    {
        return $this->started;
    }
}

final class Http
{
    public const JSON = 'application/json; charset=utf-8';
    public const NDJSON = 'application/x-ndjson; charset=utf-8';
    public const HTML = 'text/html; charset=utf-8';

    /** @param array<string,string> $extraHeaders */
    public static function error(ResponseWriter $writer, int $status, string $code, array $extraHeaders = []): void
    {
        if ($status === 503) {
            $extraHeaders['Retry-After'] = '1';
        }
        $writer->begin($status, self::JSON, $extraHeaders);
        $writer->write('{"type":"error","code":"' . $code . '"}');
    }

    /** Media type comparison, parameters are tolerated but a charset must be utf-8. */
    public static function isMediaType(?string $header, string $expected): bool
    {
        if ($header === null) {
            return false;
        }
        $parts = explode(';', $header);
        $media = strtolower(trim($parts[0]));
        if ($media !== $expected) {
            return false;
        }
        for ($i = 1; $i < count($parts); $i++) {
            $parameter = trim($parts[$i]);
            if (stripos($parameter, 'charset=') === 0) {
                $charset = strtolower(trim(substr($parameter, 8), '"'));
                if ($charset !== 'utf-8' && $charset !== 'utf8') {
                    return false;
                }
            }
        }
        return true;
    }
}

// ---------------------------------------------------------------------------
// 13. Request abstraction
// ---------------------------------------------------------------------------

interface RequestInput
{
    /** @return resource|null */
    public function stream();
}

final class PhpInput implements RequestInput
{
    public function stream()
    {
        $handle = @fopen('php://input', 'rb');
        return $handle === false ? null : $handle;
    }
}

final class StringInput implements RequestInput
{
    public function __construct(private readonly string $body)
    {
    }

    public function stream()
    {
        $handle = fopen('php://temp', 'w+b');
        fwrite($handle, $this->body);
        rewind($handle);
        return $handle;
    }
}

final class Request
{
    public function __construct(
        public readonly string $method,
        public readonly ?string $action,
        public readonly ?string $contentType,
        public readonly ?int $contentLength,
        public readonly RequestInput $input,
    ) {
    }

    // @wp-skip-begin The WordPress plugin builds the request from sanitized superglobals itself.
    public static function fromGlobals(): self
    {
        $method = strtoupper((string) ($_SERVER['REQUEST_METHOD'] ?? 'GET'));
        $action = $_GET['action'] ?? null;
        $length = $_SERVER['CONTENT_LENGTH'] ?? null;
        return new self(
            $method,
            // A non string action, for example action[]=x, can never match a known action.
            is_string($action) ? $action : ($action === null ? null : 'invalid'),
            isset($_SERVER['CONTENT_TYPE']) && is_string($_SERVER['CONTENT_TYPE']) ? $_SERVER['CONTENT_TYPE'] : null,
            is_string($length) && ctype_digit($length) ? (int) $length : null,
            new PhpInput(),
        );
    }
    // @wp-skip-end

    /** Reads at most $max bytes, returns null when the body is larger. */
    public function readBody(int $max): ?string
    {
        if ($this->contentLength !== null && $this->contentLength > $max) {
            return null;
        }
        $stream = $this->input->stream();
        if ($stream === null) {
            return '';
        }
        $data = @stream_get_contents($stream, $max + 1);
        @fclose($stream);
        if ($data === false) {
            return '';
        }
        return strlen($data) > $max ? null : $data;
    }
}

// phpcs:enable WordPress.WP.AlternativeFunctions

// ---------------------------------------------------------------------------
// 14. Handlers
// ---------------------------------------------------------------------------

// @wp-skip-begin The WordPress plugin renders its own status page.
final class StatusPageHandler
{
    /** @return array<string|int, string> request headers as received by PHP */
    public static function receivedHeaders(): array
    {
        if (function_exists('getallheaders') && is_array($headers = getallheaders())) {
            return $headers;
        }
        $headers = [];
        foreach ($_SERVER as $name => $value) {
            if (is_string($name) && str_starts_with($name, 'HTTP_') && is_string($value)) {
                $headers[str_replace(' ', '-', ucwords(strtolower(strtr(substr($name, 5), '_', ' '))))] = $value;
            }
        }
        return $headers;
    }

    /** @param array<string|int, string> $headers shown in the Show HTTP Headers dialog */
    public function handle(ResponseWriter $writer, bool $insecure, array $headers): void
    {
        $html = <<<'STATUS_HTML'
<!doctype html><html lang=en><meta name=viewport content="width=device-width"><title>VPN server is running</title><style>*{box-sizing:border-box}body{margin:0;padding:2rem 1rem;min-height:100vh;display:flex;flex-direction:column;align-items:center;background:#eee;font:16px Arial,sans-serif}main{width:920px;max-width:100%;padding:1.5rem;background:#fff;border-radius:1rem;box-shadow:0 .4rem 1.5rem #0003}h1,h2{margin-top:0}aside{padding:1rem;background:#fee;color:#900;border:1px solid #e00;border-radius:.6rem}button{padding:.6rem;border:0;border-radius:.5rem;background:#06c;color:#fff;cursor:pointer}aside button{background:#e00;color:#fff}hr{border:0;border-top:1px solid #ddd;margin:1.5rem 0}.setup{display:grid;grid-template-columns:minmax(0,1fr) 240px;gap:1.5rem;align-items:start}.setup img{width:100%;max-width:240px;height:auto;border:1px solid #bbb}.setup p:first-child{margin-top:0}.setup li{margin:.75rem 0}#u{display:block;margin:.5rem 0;padding:.75rem;background:#eee;border-radius:.4rem;font-family:monospace;overflow-wrap:anywhere}@media(max-width:640px){.setup{grid-template-columns:1fr}}.downloads{display:flex;flex-wrap:wrap;gap:.75rem}.downloads a{padding:.6rem 1rem;border-radius:.5rem;background:#06c;color:#fff;text-decoration:none}#r:not([hidden]){display:flex;gap:.5rem}#k{min-width:0;flex:1;font-family:monospace}dialog{width:900px;max-width:calc(100% - 2rem);border:0;border-radius:1rem;padding:1.5rem}dialog::backdrop{background:#0006}#x,#z{float:right;padding:0 .5rem;background:none;color:#000;font-size:1.5rem}#t{width:100%;height:14rem;font-family:monospace;white-space:pre}#b,#a{text-align:right}#f{color:#777;font-size:.9rem}#f a{color:inherit}</style><main><h1>VPN server is running</h1><p>Virtual private network connections to this server (PHPVPN) are established using the Packet Handoff Protocol. The client application can be configured by following the instructions below.</p><aside><b>Warning:</b> The default value of SERVER_SECRET is still being used. The SERVER_SECRET variable in this script must be set to a secure value immediately. A secure value can be created using the Generate key button below.<p>Secure hexadecimal key: <button id=g>Generate key</button></p><div id=r hidden><input id=k aria-label="Generated key" readonly><button id=c>Copy key</button></div></aside><hr><section class=setup><div><p><strong>Step 1 - Download and install the client app</strong></p><p>To connect to this VPN server, the client application for your operating system must first be downloaded and installed.</p><div class=downloads><a href="__CLIENT_WINDOWS_URL__">Windows</a><a href="__CLIENT_MACOS_URL__">macOS</a><a href="__CLIENT_LINUX_URL__">Linux</a></div><p>After installation, the PHPVPN client app should be launched. On Windows, it can be opened from the system tray or located by searching for phpvpn.exe in the Start menu. A similar procedure can be followed on macOS and Linux.</p></div><img src="data:image/png;base64,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" alt="Windows system tray menu with the Configuration Wizard and Edit Config options highlighted" width=240 height=180></section><hr><section><p><strong>Step 2A - Configure the client app</strong></p><p>If a configuration file has been provided by your administrator, its contents can be pasted into the configuration form accessed through <b>Edit Config</b>.</p><p><strong>Step 2B - Use the Configuration Wizard</strong></p><p>If no configuration file has been provided, the <b>Configuration Wizard</b> can be opened from the client menu. The following steps should then be completed:</p><ul><li>The VPN server URL should be set to the address of this page:<code id=u></code></li><li>The connection to the server can be checked by clicking the &quot;Verify &amp; continue&quot; button in the Configuration Wizard. If the connection fails, the HTTP request headers can be viewed using <button id=s>Show HTTP Headers</button> and then copied into the Configuration Wizard.</li><li>The correct Server Secret should be entered, or the default used if applicable.</li><li>The correct IP Address and the corresponding Access Token should be entered, or the defaults used if applicable.</li><li>The default Communication Contracts should be generated.</li><li>The Server Secret and, where required, the IP Address and Access Token should be provided by your administrator.</li></ul></section><noscript>JavaScript must be enabled so that a key can be generated and copied and HTTP headers can be viewed.</noscript></main><p id=f>Developed by <a href=https://www.pilandrome.com>www.pilandrome.com</a> (MIT License)</p><dialog id=d><form method=dialog><button id=x aria-label=Close>&times;</button><h2>HTTP headers</h2><p>The HTTP request headers received by this script may be incomplete. For exact values, your browser&apos;s developer tools can be opened with F12, and the request headers can be copied from the Network tab.</p><textarea id=t aria-label="HTTP headers" readonly></textarea><p id=b><button id=y type=button>Copy</button> <button>OK</button></p></form></dialog><dialog id=j aria-labelledby=v><form method=dialog><button id=z aria-label=Close>&times;</button><h2 id=v>SERVER_SECRET copied successfully</h2><p>The key generated by JavaScript in the web browser has been copied to the clipboard. You should now replace the value of SERVER_SECRET in the server script with this key to improve the security of the VPN server.</p><p id=a><button>OK</button></p></form></dialog><script>document.getElementById('u').textContent=location.href;C=async e=>{e.select();try{if(navigator.clipboard){await navigator.clipboard.writeText(e.value);return true}}catch{}try{e.select();return document.execCommand('copy')}catch{return false}};if(document.getElementById('g')){g.onclick=()=>{k.value=[...crypto.getRandomValues(new Uint8Array(32))].map(x=>x.toString(16).padStart(2,0)).join('');r.hidden=false};c.onclick=async()=>{if(await C(k))j.showModal()}}s.onclick=()=>{d.showModal();t.select()};y.onclick=()=>C(t)</script>
STATUS_HTML;
        $html = str_replace(
            ['__CLIENT_WINDOWS_URL__', '__CLIENT_MACOS_URL__', '__CLIENT_LINUX_URL__'],
            array_map(static fn(string $url): string => htmlspecialchars($url, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8'),
                [CLIENT_WINDOWS_DOWNLOAD_URL, CLIENT_MACOS_DOWNLOAD_URL, CLIENT_LINUX_DOWNLOAD_URL]),
            $html
        );
        if (!$insecure) {
            $html = preg_replace('/<aside>.*?<\/aside>/s', '', $html);
        }
        $lines = [];
        foreach ($headers as $name => $value) {
            // The client owns these names and its HTTP headers dialog rejects them.
            if (!preg_match('/^(host|connection|content-(type|length)|transfer-encoding)$/i', (string) $name)) {
                $lines[] = $name . ': ' . $value;
            }
        }
        $text = htmlspecialchars(implode("\n", $lines), ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
        $html = str_replace('readonly></textarea>', 'readonly>' . $text . '</textarea>', $html);
        $writer->begin(200, Http::HTML, ['Content-Length' => (string) strlen($html)]);
        $writer->write($html);
    }
}
// @wp-skip-end

final class HealthHandler
{
    public function __construct(private readonly Config $config, private readonly Clock $clock)
    {
    }

    public function handle(Request $request, ResponseWriter $writer): void
    {
        if (!Http::isMediaType($request->contentType, 'application/json')) {
            Http::error($writer, 415, 'unsupported_media_type');
            return;
        }
        $body = $request->readBody(MAX_AUTH_RECORD_BYTES);
        if ($body === null) {
            Http::error($writer, 413, 'payload_too_large');
            return;
        }
        $fields = JsonWire::parseFlatStringObject($body, ['type', 'requestId']);
        if ($fields === null || $fields['type'] !== 'auth-none' || !Validators::isUuidV4($fields['requestId'])) {
            Http::error($writer, 400, 'bad_request');
            return;
        }

        $requestId = $fields['requestId'];
        $timestamp = Iso8601::formatMs($this->clock->nowMs());
        $iv = random_bytes(12);
        $ivBase64 = Base64Url::encode($iv);
        $gcm = new AesGcm(KeyDerivation::healthKey($this->config->serverSecret, $requestId));
        $tag = $gcm->mac($iv, Authenticator::healthAad($requestId, $timestamp, $ivBase64));

        $writer->begin(200, Http::JSON);
        $writer->write((string) json_encode([
            'serverVersion' => SERVER_VERSION,
            'status' => 'running',
            'description' => 'OK',
            'timestamp' => $timestamp,
            'requestId' => $requestId,
            'iv' => $ivBase64,
            'tag' => Base64Url::encode($tag),
        ], JSON_UNESCAPED_SLASHES));
    }
}

final class InfoHandler
{
    public function __construct(
        private readonly Config $config,
        private readonly Clock $clock,
        private readonly Storage $storage,
    ) {
    }

    public function handle(Request $request, ResponseWriter $writer): void
    {
        if (!Http::isMediaType($request->contentType, 'application/json')) {
            Http::error($writer, 415, 'unsupported_media_type');
            return;
        }
        if (MAX_INFO_REQUEST_BODY_BYTES > MAX_REQUEST_BODY_BYTES || MAX_INFO_REQUEST_BODY_BYTES < MAX_AUTH_RECORD_BYTES) {
            Http::error($writer, 503, 'server_misconfigured');
            return;
        }
        $body = $request->readBody(MAX_INFO_REQUEST_BODY_BYTES);
        if ($body === null) {
            Http::error($writer, 413, 'payload_too_large');
            return;
        }
        $object = JsonWire::parseInfoObject($body);
        $extended = $object !== null && (property_exists($object, 'maskedCommunicationContracts') || property_exists($object, 'includeMaskedCommunicationContracts'));
        if (!$extended && strlen($body) > MAX_AUTH_RECORD_BYTES) {
            Http::error($writer, 413, 'payload_too_large');
            return;
        }
        $fields = $extended ? get_object_vars($object) : JsonWire::parseFlatStringObject($body, ['type', 'timestamp', 'requestId', 'tag']);
        $required = $extended ? ['type', 'src', 'timestamp', 'requestId', 'tag'] : ['type', 'timestamp', 'requestId', 'tag'];
        $valid = $fields !== null;
        foreach ($required as $key) $valid = $valid && isset($fields[$key]) && is_string($fields[$key]);
        if (!$valid || array_diff(array_keys($fields), array_merge($required, ['maskedCommunicationContracts', 'includeMaskedCommunicationContracts'])) !== [] ||
            $fields['type'] !== ($extended ? 'auth-full' : 'auth-semi') || !Validators::isUuidV4($fields['requestId'])) {
            Http::error($writer, 400, 'bad_request');
            return;
        }
        $extensions = [];
        if ($extended) {
            if (!Validators::isVpnAddress($fields['src'])) {
                Http::error($writer, 401, 'unauthorized');
                return;
            }
            if (array_key_exists('maskedCommunicationContracts', $fields)) {
                $contracts = JsonWire::maskedContracts($fields['maskedCommunicationContracts'], $fields['src']);
                if (is_int($contracts)) {
                    Http::error($writer, $contracts, $contracts === 413 ? 'payload_too_large' : 'bad_request');
                    return;
                }
                $extensions['maskedCommunicationContracts'] = $contracts;
            }
            if (array_key_exists('includeMaskedCommunicationContracts', $fields)) {
                if (!is_bool($fields['includeMaskedCommunicationContracts'])) {
                    Http::error($writer, 400, 'bad_request');
                    return;
                }
                $extensions['includeMaskedCommunicationContracts'] = $fields['includeMaskedCommunicationContracts'];
            }
        }
        $timestampMs = Iso8601::parseMs($fields['timestamp']);
        if ($timestampMs === null || !Base64Url::isCanonicalOfByteLength($fields['tag'], 32)) {
            Http::error($writer, 400, 'bad_request');
            return;
        }
        if (!Authenticator::withinSkew($timestampMs, $this->clock->nowMs())) {
            Http::error($writer, 401, 'unauthorized');
            return;
        }

        $known = true;
        if ($extended) {
            $accessKey = $this->config->accessKey($fields['src']);
            $known = $accessKey !== null;
            $key = KeyDerivation::fullAuthKey($this->config->serverSecret, $accessKey ?? KeyDerivation::DUMMY_ACCESS_KEY, $fields['src']);
            $canonical = Authenticator::infoFullCanonical($fields['src'], $fields['timestamp'], $fields['requestId'],
                hash('sha256', (string) json_encode($extensions, JSON_UNESCAPED_SLASHES)));
        } else {
            $key = KeyDerivation::semiKey($this->config->serverSecret);
            $canonical = Authenticator::semiCanonical($fields['timestamp'], $fields['requestId']);
        }
        $expected = hash_hmac('sha256', $canonical, $key, true);
        $provided = Base64Url::decode($fields['tag']);
        $valid = $provided !== null && hash_equals($expected, $provided);
        if (!$known || !$valid) {
            Http::error($writer, 401, 'unauthorized');
            return;
        }

        if ($extended) {
            if (array_key_exists('maskedCommunicationContracts', $extensions)) {
                if (!$this->storage->publishMaskedContracts($fields['src'], $extensions['maskedCommunicationContracts'], $this->clock->nowMs())) {
                    Http::error($writer, 503, 'queue_unavailable');
                    return;
                }
            } else {
                $this->storage->touchLastSeenBestEffort($fields['src'], $this->clock->nowMs());
            }
        }
        $plaintext = $this->buildPlaintext(($extensions['includeMaskedCommunicationContracts'] ?? false) ? $fields['src'] : null);
        if ($plaintext === null) {
            Http::error($writer, 503, 'server_misconfigured');
            return;
        }

        $requestId = $fields['requestId'];
        $salt = random_bytes(32);
        $iv = random_bytes(12);
        $saltBase64 = Base64Url::encode($salt);
        $ivBase64 = Base64Url::encode($iv);
        $gcm = new AesGcm(KeyDerivation::infoKey($this->config->serverSecret, $salt, $requestId));
        [$ciphertext, $tag] = $gcm->seal($iv, $plaintext, Authenticator::infoAad($requestId, $saltBase64, $ivBase64));

        $writer->begin(200, Http::JSON);
        $writer->write((string) json_encode([
            'type' => 'info',
            'requestId' => $requestId,
            'salt' => $saltBase64,
            'iv' => $ivBase64,
            'ciphertext' => Base64Url::encode($ciphertext),
            'tag' => Base64Url::encode($tag),
        ], JSON_UNESCAPED_SLASHES));
    }

    private function buildPlaintext(?string $requester = null): ?string
    {
        $clients = [];
        foreach ($this->config->clientAddresses() as $address) {
            $snapshot = $requester === null ? null : $this->storage->readClientInfoSnapshot($address);
            $lastSeen = $snapshot === null ? $this->storage->readLastSeenMs($address) : $snapshot['lastSeen'];
            if ($lastSeen === null) {
                continue;
            }
            $client = ['address' => $address, 'lastSeen' => Iso8601::formatMs($lastSeen)];
            if ($requester !== null) {
                $client['maskedCommunicationContracts'] = [];
                if ($address !== $requester) foreach ($snapshot['contracts'] as $contract) {
                    if ($contract['peer'] === $requester) $client['maskedCommunicationContracts'][] = $contract;
                }
            }
            $clients[] = $client;
        }
        $limits = [
            'maxDataRecordsPerRequest' => MAX_DATA_RECORDS_PER_REQUEST,
            'maxRequestBodyBytes' => MAX_REQUEST_BODY_BYTES,
            'maxQueueBytesPerClient' => MAX_QUEUE_BYTES_PER_CLIENT,
            'queueTtlSeconds' => QUEUE_TTL_SECONDS,
            'minDataPollIntervalMs' => MIN_DATA_POLL_INTERVAL_MS,
            'maxDataPollIntervalMs' => MAX_DATA_POLL_INTERVAL_MS,
        ];

        while (true) {
            $plaintext = json_encode([
                'serverVersion' => SERVER_VERSION,
                'generatedAt' => Iso8601::formatMs($this->clock->nowMs()),
                'clients' => $clients,
                'limits' => $limits,
            ], JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT);
            if ($plaintext === false) {
                return null;
            }
            if (strlen($plaintext) <= MAX_INFO_PLAINTEXT_BYTES) {
                return $plaintext;
            }
            if ($clients === []) {
                return null;
            }
            array_pop($clients);
        }
    }
}

final class DataHandler
{
    public function __construct(
        private readonly Config $config,
        private readonly Clock $clock,
        private readonly Storage $storage,
    ) {
    }

    public function handle(Request $request, ResponseWriter $writer): void
    {
        if (!Http::isMediaType($request->contentType, 'application/x-ndjson')) {
            Http::error($writer, 415, 'unsupported_media_type');
            return;
        }
        $body = $request->readBody(MAX_REQUEST_BODY_BYTES);
        if ($body === null) {
            Http::error($writer, 413, 'payload_too_large');
            return;
        }

        $lf = strpos($body, "\n");
        if ($lf === false) {
            if (strlen($body) + 1 > MAX_AUTH_RECORD_BYTES) {
                Http::error($writer, 413, 'payload_too_large');
                return;
            }
            Http::error($writer, 400, 'bad_request');
            return;
        }
        if ($lf + 1 > MAX_AUTH_RECORD_BYTES) {
            Http::error($writer, 413, 'payload_too_large');
            return;
        }
        $authLine = substr($body, 0, $lf);
        $rest = substr($body, $lf + 1);

        $auth = JsonWire::parseFlatStringObject($authLine, ['type', 'src', 'timestamp', 'requestId', 'tag']);
        if ($auth === null || $auth['type'] !== 'auth-full' || !Validators::isUuidV4($auth['requestId'])) {
            Http::error($writer, 400, 'bad_request');
            return;
        }
        if (!Validators::isVpnAddress($auth['src'])) {
            Http::error($writer, 400, 'bad_request');
            return;
        }
        $timestampMs = Iso8601::parseMs($auth['timestamp']);
        if ($timestampMs === null || !Base64Url::isCanonicalOfByteLength($auth['tag'], 32)) {
            Http::error($writer, 400, 'bad_request');
            return;
        }
        if (!Authenticator::withinSkew($timestampMs, $this->clock->nowMs())) {
            Http::error($writer, 401, 'unauthorized');
            return;
        }

        $src = $auth['src'];
        // An unknown client runs the identical derivation with a fixed dummy key, chapter 9.2.
        $accessKey = $this->config->accessKey($src) ?? KeyDerivation::DUMMY_ACCESS_KEY;
        $known = $this->config->accessKey($src) !== null;
        $fullAuthKey = KeyDerivation::fullAuthKey($this->config->serverSecret, $accessKey, $src);
        $canonical = Authenticator::fullCanonical(
            $src,
            $auth['timestamp'],
            $auth['requestId'],
            hash('sha256', $rest)
        );
        $expected = hash_hmac('sha256', $canonical, $fullAuthKey, true);
        $provided = Base64Url::decode($auth['tag']);
        $valid = $provided !== null && hash_equals($expected, $provided);
        if (!$known || !$valid) {
            Http::error($writer, 401, 'unauthorized');
            return;
        }

        $records = $this->parseRecords($rest, $src, $status);
        if ($records === null) {
            Http::error($writer, $status, match ($status) {
                413 => 'payload_too_large',
                401 => 'unauthorized',
                default => 'bad_request',
            });
            return;
        }

        if (!$this->storage->isUsable()) {
            Http::error($writer, 503, 'queue_unavailable');
            return;
        }

        $nowMs = $this->clock->nowMs();
        try {
            [$accepted, $deferred, $acceptedBytes] = $this->publish($records, $src, $nowMs);
        } catch (QueueBusy) {
            Http::error($writer, 503, 'queue_busy');
            return;
        }

        // Best effort statistics, never able to change the protocol result, chapter 10.6.
        $this->storage->touchLastSeenBestEffort($src, $nowMs);

        $claimed = $this->storage->drainAll($src, $nowMs);

        $recordCount = 0;
        foreach ($claimed as $batch) {
            $recordCount += count($batch->pairs);
        }

        $resultLine = (string) json_encode([
            'type' => 'data-result',
            'requestId' => $auth['requestId'],
            'acceptedMessageIds' => array_values($accepted),
            'deferredMessageIds' => array_values($deferred),
            'acceptedRecords' => count($accepted),
            'acceptedBytes' => $acceptedBytes,
        ], JSON_UNESCAPED_SLASHES);

        $mac = $this->responseMac($fullAuthKey, $auth['requestId'], $recordCount, $claimed, $resultLine);
        $macLine = (string) json_encode([
            'type' => 'data-mac',
            'requestId' => $auth['requestId'],
            'tag' => Base64Url::encode($mac),
        ], JSON_UNESCAPED_SLASHES);

        $writer->begin(200, Http::NDJSON);
        foreach ($claimed as $batch) {
            if (!$this->emitBatch($writer, $batch)) {
                // The response is already partially written, ending it without data-mac
                // makes the client discard everything, which is the intended failure mode.
                return;
            }
        }
        $writer->write($resultLine . "\n");
        $writer->write($macLine . "\n");

        $this->afterResponse($nowMs);
    }

    /**
     * @return DataRecord[]|null
     */
    private function parseRecords(string $rest, string $src, ?int &$status): ?array
    {
        $status = 400;
        if ($rest === '') {
            return [];
        }
        if (substr($rest, -1) !== "\n") {
            return null;
        }
        // The split is bounded, otherwise a body of line feeds would allocate one entry per byte.
        $lines = explode("\n", substr($rest, 0, -1), MAX_DATA_RECORDS_PER_REQUEST + 1);
        if (count($lines) > MAX_DATA_RECORDS_PER_REQUEST) {
            $status = 413;
            return null;
        }

        $records = [];
        $seen = [];
        foreach ($lines as $line) {
            if ($line === '' || str_contains($line, "\r")) {
                return null;
            }
            $record = JsonWire::parseDataRecord($line, $src);
            if (is_int($record)) {
                $status = $record;
                return null;
            }
            if (isset($seen[$record->messageId])) {
                return null;
            }
            $seen[$record->messageId] = true;
            $records[] = $record;
        }
        return $records;
    }

    /**
     * @param DataRecord[] $records
     * @return array{0:string[],1:string[],2:int}
     */
    private function publish(array $records, string $src, int $nowMs): array
    {
        if ($records === []) {
            return [[], [], 0];
        }
        $groups = [];
        foreach ($records as $record) {
            $groups[$record->dst][] = $record;
        }
        // Deterministic processing order, chapter 10.2.
        uksort($groups, static fn(string $a, string $b): int
            => Validators::addressSortKey($a) <=> Validators::addressSortKey($b));

        $acceptedIds = [];
        $deferredIds = [];
        $acceptedBytes = 0;
        $expiresAtMs = $nowMs + QUEUE_TTL_SECONDS * 1000;

        foreach ($groups as $dst => $groupRecords) {
            $lines = [];
            $pairs = [];
            $bytes = 0;
            foreach ($groupRecords as $record) {
                $lines[] = $record->line;
                $pairs[] = [$record->messageId, $record->gcmTag];
                $bytes += strlen($record->line) + 1;
            }
            $published = $this->storage->enqueue((string) $dst, $src, $lines, $pairs, $expiresAtMs);
            foreach ($groupRecords as $record) {
                if ($published) {
                    $acceptedIds[$record->messageId] = $record->messageId;
                } else {
                    $deferredIds[$record->messageId] = $record->messageId;
                }
            }
            if ($published) {
                $acceptedBytes += $bytes;
            }
        }

        // Preserve the request order in both result arrays.
        $accepted = [];
        $deferred = [];
        foreach ($records as $record) {
            if (isset($acceptedIds[$record->messageId])) {
                $accepted[] = $record->messageId;
            } else {
                $deferred[] = $record->messageId;
            }
        }
        return [$accepted, $deferred, $acceptedBytes];
    }

    /** @param ClaimedBatch[] $claimed */
    private function responseMac(
        string $fullAuthKey,
        string $requestId,
        int $recordCount,
        array $claimed,
        string $resultLine
    ): string {
        $context = hash_init('sha256', HASH_HMAC, $fullAuthKey);
        hash_update($context, "PHPVPN-DATA-RESPONSE\ndata\n" . $requestId . "\n" . $recordCount . "\n");
        foreach ($claimed as $batch) {
            foreach ($batch->pairs as $pair) {
                hash_update($context, $pair[0] . "\n" . $pair[1] . "\n");
            }
        }
        hash_update($context, hash('sha256', $resultLine));
        return hash_final($context, true);
    }

    private function emitBatch(ResponseWriter $writer, ClaimedBatch $batch): bool
    {
        // Streams a claimed queue batch from the storage root, see section 3.
        // phpcs:disable WordPress.WP.AlternativeFunctions
        $handle = @fopen($batch->path, 'rb');
        if ($handle === false) {
            return false;
        }
        if (@fseek($handle, $batch->offset) !== 0) {
            @fclose($handle);
            return false;
        }
        $copied = $writer->copyFrom($handle);
        @fclose($handle);
        // phpcs:enable WordPress.WP.AlternativeFunctions
        Fs::unlink($batch->path);
        // A short copy means a truncated or damaged batch file, the response must not be completed.
        return $copied === $batch->bytes;
    }

    private function afterResponse(int $nowMs): void
    {
        if (function_exists('fastcgi_finish_request')) {
            @fastcgi_finish_request();
        }
        $this->storage->sweep($nowMs, SWEEP_MAX_MILLISECONDS);
    }
}

// ---------------------------------------------------------------------------
// 15. Router and application
// ---------------------------------------------------------------------------

final class Application
{
    // @wp-skip-begin Standalone entry point, the WordPress plugin calls handleRoot and handleAction.
    public function handle(Request $request, ResponseWriter $writer): void
    {
        if ($request->action === null || $request->action === '') {
            $statusPage = STATUS_PAGE_ENABLED
                ? static fn() => (new StatusPageHandler())->handle(
                    $writer,
                    self::isSecretInsecure(SERVER_SECRET),
                    StatusPageHandler::receivedHeaders()
                )
                : null;
            self::handleRoot($request, $writer, $statusPage);
            return;
        }
        $this->handleAction($request, $writer, static fn(): Config => Config::fromConstants());
    }
    // @wp-skip-end

    /** The root URL without an action, $statusPage null means a disabled status page. */
    public static function handleRoot(Request $request, ResponseWriter $writer, ?callable $statusPage): void
    {
        if ($request->method === 'GET' || $request->method === 'HEAD') {
            if ($statusPage === null) {
                Http::error($writer, 404, 'not_found');
                return;
            }
            $statusPage();
            return;
        }
        Http::error($writer, 405, 'method_not_allowed', ['Allow' => 'GET, HEAD']);
    }

    /** @param callable(): Config $configFactory may throw ConfigException */
    public function handleAction(Request $request, ResponseWriter $writer, callable $configFactory): void
    {
        $action = (string) $request->action;
        if ($action !== 'health' && $action !== 'info' && $action !== 'data') {
            Http::error($writer, 404, 'not_found');
            return;
        }
        if ($request->method !== 'POST') {
            Http::error($writer, 405, 'method_not_allowed', ['Allow' => 'POST']);
            return;
        }

        try {
            $config = $configFactory();
        } catch (ConfigException) {
            Http::error($writer, 503, 'server_misconfigured');
            return;
        }

        $this->dispatch($config, $request, $writer, $action);
    }

    public function dispatch(
        Config $config,
        Request $request,
        ResponseWriter $writer,
        string $action,
        ?Clock $clock = null,
        ?Storage $storage = null
    ): void {
        $clock ??= new Clock();
        $storage ??= new DiskStorage($config->bufferDirectory);

        if ($action === 'health') {
            if (!AesGcm::selfTest()) {
                Http::error($writer, 500, 'internal_error');
                return;
            }
            (new HealthHandler($config, $clock))->handle($request, $writer);
            return;
        }
        if ($action === 'info') {
            if (!AesGcm::selfTest()) {
                Http::error($writer, 500, 'internal_error');
                return;
            }
            (new InfoHandler($config, $clock, $storage))->handle($request, $writer);
            return;
        }
        (new DataHandler($config, $clock, $storage))->handle($request, $writer);
    }

    /** The status page must never depend on full configuration validation, chapter 5.1. */
    public static function isSecretInsecure(string $configured): bool
    {
        if ($configured === '') {
            return true;
        }
        return $configured === 'CHANGE_THIS_SERVER_SECRET' || $configured === str_repeat("\x00", strlen($configured))
            || $configured === str_repeat('0', strlen($configured));
    }

    // @wp-skip-begin
    public static function runFromGlobals(): void
    {
        Runtime::init();
        $request = Request::fromGlobals();
        $writer = new HttpResponseWriter($request->method === 'HEAD');
        Runtime::bindWriter($writer);
        (new Application())->handle($request, $writer);
    }
    // @wp-skip-end
}

// @wp-skip-begin
if (!defined('PHPVPN_NO_AUTORUN')) {
    Application::runFromGlobals();
}
// @wp-skip-end

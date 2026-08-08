<?php

/**
 * phpvpn.php - single file relay for an end to end encrypted IPv4 VPN.
 *
 * The server never decrypts a packet. It authorises the caller, validates the
 * envelope and moves opaque payloads between per client on disk queues.
 *
 * Requirements: PHP 8.2+ and one writable local directory. No database, no
 * Composer, no cron, no shell and no optional PHP extension. Only the always
 * enabled core extensions hash, json, pcre, random, date, SPL and standard are
 * used.
 *
 * The transport is deliberately ignored. HTTP and HTTPS, a direct connection
 * and a connection through a proxy all behave identically: the scheme, the
 * port, the peer address and every X-Forwarded header are never read, never
 * logged and never influence authentication, replay protection, the queues,
 * the response or the HTTP status. Plain HTTP is a knowingly supported mode.
 * It provides no confidentiality, no server authentication and no protection
 * of the response on the wire. Where those properties are needed they are
 * supplied by infrastructure in front of this file, never by this file.
 *
 * Endpoints:
 *   GET  /phpvpn.php                 operational HTML page
 *   *    /phpvpn.php?action=health   version, status and authoritative UTC time
 *   POST /phpvpn.php?action=sync     one short bidirectional batch exchange
 *   POST /phpvpn.php?action=selftest environment preflight, disabled by default
 *
 * Deliberate deviations from design2.md, all documented in PROTOCOL.md:
 *   1. A destination queue that cannot accept a batch does not fail the whole
 *      request. Stored packets are reported in acceptedPacketUuids, the rest in
 *      deferredPacketUuids. 429 or 503 is returned only when no destination
 *      could be served at all.
 *   2. A claimed batch is renamed to "<batchId>.sending" without a random
 *      suffix, which turns ack processing into a single unlink.
 *   3. Batches are stored already wrapped as "delivery" lines and the batch id
 *      carries the payload size, so the read path is a pure byte copy and needs
 *      no stat call while budgeting a response.
 *
 * @see design2.md  architecture
 * @see problems.md hosting constraints that shaped the defaults
 * @see PROTOCOL.md wire format for client implementers
 */

declare(strict_types=1);

// =============================================================================
// Identity - not a setting, an administrator never changes this.
// =============================================================================

/** Server version, reported as the first key of every health response. */
const PHPVPN_VERSION = '1.0.0';

// =============================================================================
// CONFIGURATION - this is the only part an administrator edits.
// =============================================================================

/** Random secret, at least 32 characters. Without it sync is refused. */
const SERVER_SECRET = '';

/**
 * Root of the on disk queue. Strongly prefer a path OUTSIDE the document root,
 * for example '/home/account/private/phpvpn-queue'. An empty value falls back
 * to a "phpvpn" directory next to this file, which then relies on a working
 * webserver deny rule. Use forward slashes on every platform.
 * RECOMMENDATION: USE RAMDISK IF POSSIBLE
 */
const QUEUE_ROOT = '';

/** Hard ceiling for a whole request body. Must stay well below post_max_size. */
const MAX_REQUEST_BYTES = 1048576;

/** Hard ceiling for a single response. */
const MAX_RESPONSE_BYTES = 1048576;

/** Longest acceptable single NDJSON line. 128 KiB fits a 65535 byte IP packet. */
const MAX_PACKET_LINE_BYTES = 131072;

/** Protocol ceiling on data lines per request. Exceeding it is an error. */
const MAX_REQUEST_PACKETS = 2000;

/** Operational ceiling on bytes actually queued in one cycle. */
const MAX_ACCEPTED_REQUEST_BYTES = 786432;

/** Operational ceiling on packets actually queued in one cycle. */
const MAX_ACCEPTED_REQUEST_PACKETS = 1000;

/** Maximum number of pending batches (.data plus .sending) in one queue. */
const MAX_BUFFERS = 128;

/** An unclaimed batch older than this is dropped. */
const QUEUE_TTL_SECONDS = 60;

/** Replay protection. false knowingly allows replay inside the time window. */
const PERSIST_NONCES = false;

/** VPN IP of a client => its 64 lowercase hex access token. */
const ACCESS_TOKENS = [
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
    '10.99.99.15' => '0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f'
];

// =============================================================================
// ADVANCED TUNING - change only after load testing the target hosting.
// =============================================================================

/**
 * Allowed client clock skew in seconds. A higher value better tolerates
 * inaccurate clocks, but extends the validity of a signed request.
 */
const CLOCK_SKEW_SECONDS        = 60;

/**
 * Maximum length of the initial authentication line in bytes. This limits
 * memory usage and rejects an excessively long line as a malformed request.
 */
const MAX_AUTH_LINE_BYTES       = 4096;

/**
 * Maximum time to wait for a queue or nonce lock in milliseconds. A higher
 * value reduces rejections under contention, but increases response time.
 */
const LOCK_TIMEOUT_MS           = 200;

/**
 * Maximum sleep between lock attempts in microseconds. A higher value saves
 * CPU under contention, but delays acquisition of a released lock.
 */
const LOCK_RETRY_MAX_SLEEP_US   = 8000;

/**
 * Maximum interval between cleanups of a specific queue in seconds. A shorter
 * interval removes stale files sooner at the cost of more frequent I/O.
 */
const GC_INTERVAL_SECONDS       = 5;

/**
 * Age in seconds after which a .sending file is returned for delivery. Too low
 * a value risks duplicates, while a higher value slows recovery.
 */
const SENDING_RECLAIM_SECONDS   = 30;

/**
 * Age in seconds after which cleanup discards an incomplete .writing file.
 * A higher value retains orphaned files and their disk usage for longer.
 */
const WRITING_STALE_SECONDS     = 60;

/**
 * Retention time for an undeletable .error file in seconds. A longer period
 * aids diagnostics, but consumes more disk space and inodes.
 */
const ERROR_FILE_TTL_SECONDS    = 3600;

/**
 * Maximum number of acknowledgments (acks) in one request. A higher limit
 * allows faster batch cleanup, but increases processing work and the request
 * length.
 */
const MAX_ACKS_PER_REQUEST      = 256;

/**
 * Approximate total byte limit across all queues. This protects the disk;
 * once the limit is reached, new batches are deferred.
 */
const MAX_TOTAL_QUEUE_BYTES     = 134217728;

/**
 * Approximate total file limit across all queues. This protects inodes and
 * defers new batches once the limit is reached.
 */
const MAX_TOTAL_QUEUE_FILES     = 4096;

/**
 * Base validity period of the global statistics in seconds. A staler estimate
 * reduces quota accuracy; after six times this period it no longer blocks
 * traffic.
 */
const GLOBAL_STAT_TTL_SECONDS   = 10;

/**
 * Minimum interval between global sweeps in seconds. A shorter interval speeds
 * cleanup and quota updates at the cost of increased I/O.
 */
const GLOBAL_SWEEP_INTERVAL_SEC = 60;

/**
 * Maximum number of queue directories processed by one global sweep. A higher
 * value completes a full pass sooner, but lengthens the request.
 */
const GLOBAL_SWEEP_MAX_DIRS     = 8;

/**
 * Duration of one nonce time bucket in seconds. This determines the granularity
 * of replay protection and the frequency of nonce files.
 */
const NONCE_BUCKET_SECONDS      = 60;

/**
 * Number of nonce buckets checked, including the current one. A higher value
 * extends the protection window, but increases reads and retention time.
 */
const NONCE_WINDOW_BUCKETS      = 3;

/**
 * Maximum number of nonces for one client in one bucket. A higher limit handles
 * more requests, but increases the file size and lookup cost.
 */
const MAX_NONCES_PER_BUCKET     = 4096;

/**
 * Forces a published batch to be synced to disk. true improves resilience
 * to failures at the cost of latency; false only flushes the data.
 */
const FSYNC_ON_PUBLISH          = false;

/**
 * Controls the Content-Length header. true calculates the response length in
 * advance; false lets the web server handle the transfer without it.
 */
const SEND_CONTENT_LENGTH       = true;

/**
 * Target processing time in milliseconds used to recommend the next batch.
 * This is not a timeout; exceeding it only reduces the recommended size.
 */
const REQUEST_BUDGET_MS         = 250;

/**
 * Logging level. Valid values: 'off' logs nothing, 'error' logs only errors,
 * and 'info' also logs operational events, which increases I/O.
 */
const LOG_LEVEL                 = 'error';

/**
 * Number of days to retain daily logs. A higher value preserves a longer
 * history, but consumes more disk space.
 */
const LOG_RETENTION_DAYS        = 3;

/**
 * Exposes the signed diagnostic selftest endpoint. true enables it; false
 * returns 404 and reduces the exposed surface in production.
 */
const SELFTEST_ENABLED          = false;

// =============================================================================
// Errors
// =============================================================================

/**
 * A controlled failure that maps to an HTTP status and a generic error code.
 * The code is the only thing ever sent to the client, so it must not leak
 * anything about configuration, other clients or queue contents.
 */
final class HttpError extends RuntimeException
{
    public function __construct(
        public readonly int $status,
        public readonly string $errorCode
    ) {
        parent::__construct($errorCode);
    }
}

// =============================================================================
// Request context
// =============================================================================

/** Per request state that would otherwise be passed through every layer. */
final class Ctx
{
    public static string $rid = '';
    public static int $startedAt = 0;
    public static int $now = 0;
    public static int $lockWaitNs = 0;
    public static ?string $writingPath = null;
    public static string $client = '';
    public static string $action = '';

    private static string $pool = '';
    private static int $poolOffset = 0;

    public static function init(string $action): void
    {
        self::$startedAt = hrtime(true);
        self::$now = time();
        self::$action = $action;
        self::$rid = self::hex(8);
    }

    /** Elapsed milliseconds since the request started. */
    public static function elapsedMs(): float
    {
        return (hrtime(true) - self::$startedAt) / 1e6;
    }

    /**
     * Random lowercase hex drawn from a single pooled random_bytes call, which
     * keeps the number of CSPRNG syscalls per request at one in practice.
     */
    public static function hex(int $bytes): string
    {
        if (self::$poolOffset + $bytes > strlen(self::$pool)) {
            self::$pool = random_bytes(max(64, $bytes));
            self::$poolOffset = 0;
        }
        $raw = substr(self::$pool, self::$poolOffset, $bytes);
        self::$poolOffset += $bytes;

        return bin2hex($raw);
    }
}

// =============================================================================
// Configuration validation
// =============================================================================

final class Cfg
{
    public const MIN_SECRET_LENGTH = 32;

    /**
     * O(1) checks executed on every request. Only constant expressions are
     * inspected, so this stays free of I/O and of the token table scan.
     */
    public static function assertRuntime(): void
    {
        if (strlen(SERVER_SECRET) < self::MIN_SECRET_LENGTH) {
            throw new HttpError(503, 'server_misconfigured');
        }
        if (MAX_REQUEST_BYTES < 1024
            || MAX_RESPONSE_BYTES < 1024
            || MAX_PACKET_LINE_BYTES < 256
            || MAX_PACKET_LINE_BYTES > MAX_REQUEST_BYTES
            || MAX_ACCEPTED_REQUEST_BYTES > MAX_REQUEST_BYTES
            || MAX_ACCEPTED_REQUEST_PACKETS > MAX_REQUEST_PACKETS
            || MAX_ACCEPTED_REQUEST_PACKETS < 1
            || MAX_BUFFERS < 2
            || QUEUE_TTL_SECONDS < 5
            || CLOCK_SKEW_SECONDS < 5
            || LOCK_TIMEOUT_MS < 10
            || SENDING_RECLAIM_SECONDS < 5
        ) {
            throw new HttpError(503, 'server_misconfigured');
        }
    }

    /** True when the running configuration can serve sync at all. */
    public static function runtimeOk(): bool
    {
        try {
            self::assertRuntime();

            return true;
        } catch (HttpError) {
            return false;
        }
    }

    /**
     * Full diagnostic validation. Deliberately kept off the hot path: sync only
     * needs the single token it actually uses, which Auth validates in O(1).
     *
     * @return list<string> human readable problems, empty when the config is fine
     */
    public static function diagnose(): array
    {
        $problems = [];

        if (strlen(SERVER_SECRET) < self::MIN_SECRET_LENGTH) {
            $problems[] = 'SERVER_SECRET must be at least ' . self::MIN_SECRET_LENGTH . ' characters.';
        }
        if (!self::runtimeOk()) {
            $problems[] = 'One or more size limits are out of range or inconsistent.';
        }
        if (ACCESS_TOKENS === []) {
            $problems[] = 'ACCESS_TOKENS is empty, no client can authenticate.';
        }

        $seen = [];
        foreach (ACCESS_TOKENS as $ip => $token) {
            if (!is_string($ip) || Wire::octetOf((string) $ip) === 0) {
                $problems[] = 'ACCESS_TOKENS contains an address outside 10.99.99.2 - 10.99.99.254.';
                continue;
            }
            if (!is_string($token) || !Wire::isAccessToken($token)) {
                $problems[] = 'The access token for ' . $ip . ' is not 64 lowercase hex characters.';
                continue;
            }
            if (isset($seen[$token])) {
                $problems[] = 'The access token for ' . $ip . ' is reused by another client.';
            }
            $seen[$token] = true;
        }

        $postMax = self::iniBytes('post_max_size');
        if ($postMax > 0 && MAX_REQUEST_BYTES >= $postMax) {
            $problems[] = 'MAX_REQUEST_BYTES is not below post_max_size (' . ini_get('post_max_size') . ').';
        }
        $memory = self::iniBytes('memory_limit');
        if ($memory > 0 && MAX_RESPONSE_BYTES * 4 > $memory) {
            $problems[] = 'MAX_RESPONSE_BYTES leaves too little headroom under memory_limit.';
        }

        return $problems;
    }

    /** Resolves a PHP ini shorthand size to bytes. Returns 0 when unlimited. */
    public static function iniBytes(string $key): int
    {
        $raw = trim((string) ini_get($key));
        if ($raw === '' || $raw === '-1') {
            return 0;
        }
        $unit = strtolower($raw[strlen($raw) - 1]);
        $value = (int) $raw;

        return match ($unit) {
            'g' => $value * 1073741824,
            'm' => $value * 1048576,
            'k' => $value * 1024,
            default => $value,
        };
    }
}

// =============================================================================
// HTTP helpers
// =============================================================================

final class Http
{
    /** Headers shared by every response, including error responses. */
    public static function baseHeaders(): void
    {
        header('Cache-Control: no-store, no-cache, must-revalidate');
        header('Pragma: no-cache');
        header('X-Content-Type-Options: nosniff');
        header('Referrer-Policy: no-referrer');
    }

    public static function method(): string
    {
        return strtoupper((string) ($_SERVER['REQUEST_METHOD'] ?? 'GET'));
    }

    public static function action(): string
    {
        $action = $_GET['action'] ?? '';

        return is_string($action) ? $action : '';
    }

    public static function contentType(): string
    {
        return strtolower(trim((string) ($_SERVER['CONTENT_TYPE'] ?? '')));
    }

    public static function contentLength(): int
    {
        $raw = $_SERVER['CONTENT_LENGTH'] ?? '';

        return is_string($raw) && $raw !== '' && ctype_digit($raw) ? (int) $raw : -1;
    }

    /** Sends a small generic error document. Never reveals internal detail. */
    public static function sendError(int $status, string $code): void
    {
        if (headers_sent()) {
            return;
        }
        http_response_code($status);
        self::baseHeaders();
        header('Content-Type: application/json; charset=utf-8');
        if ($status === 429 || $status === 503) {
            header('Retry-After: 1');
        }
        echo '{"status":"error","error":"', $code, '"}';
    }

    public static function sendMethodNotAllowed(string $allow): void
    {
        if (headers_sent()) {
            return;
        }
        header('Allow: ' . $allow);
        self::sendError(405, 'method_not_allowed');
    }
}

// =============================================================================
// Logging
// =============================================================================

final class Log
{
    private const LEVELS = ['off' => 0, 'error' => 1, 'info' => 2];

    public static function error(string $category, array $fields = []): void
    {
        self::write(1, $category, $fields);
    }

    public static function info(string $category, array $fields = []): void
    {
        self::write(2, $category, $fields);
    }

    private static function write(int $level, string $category, array $fields): void
    {
        if ((self::LEVELS[LOG_LEVEL] ?? 1) < $level) {
            return;
        }
        $dir = QueuePath::logDir();
        if ($dir === null) {
            return;
        }

        $record = [
            'ts' => gmdate('Y-m-d\TH:i:s\Z', Ctx::$now),
            'rid' => Ctx::$rid,
            'action' => Ctx::$action,
            'client' => Ctx::$client,
            'category' => $category,
            'elapsedMs' => round(Ctx::elapsedMs(), 2),
            'lockWaitMs' => round(Ctx::$lockWaitNs / 1e6, 2),
        ] + $fields;

        $line = json_encode($record, JSON_UNESCAPED_SLASHES);
        if ($line === false) {
            return;
        }
        // A short append under an exclusive lock is atomic on the supported
        // single node filesystems, so no separate lock file is needed here.
        @file_put_contents(
            $dir . '/' . gmdate('Y-m-d', Ctx::$now) . '.log',
            $line . "\n",
            FILE_APPEND | LOCK_EX
        );
    }
}

// =============================================================================
// Wire format
// =============================================================================

/**
 * Validation and serialisation of the NDJSON protocol.
 *
 * Every field that ends up inside a generated JSON string is validated against
 * a strict character class first. That invariant is what makes the escape free
 * concatenation in buildPacketFragment() provably safe, so it must never be
 * relaxed without also switching that method back to json_encode().
 */
final class Wire
{
    public const RE_TIMESTAMP = '/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/';
    public const RE_NONCE     = '/^[A-Za-z0-9_-]{16,64}$/';
    public const RE_SIGNATURE = '/^[0-9a-f]{64}$/';
    public const RE_UUID      = '/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/';
    public const RE_TOKEN     = '/^[0-9a-f]{64}$/';
    public const RE_BASE64    = '/^[A-Za-z0-9+\/]+={0,2}$/';

    private const DELIVERY_PREFIX = '{"type":"delivery","batchId":"';
    private const DELIVERY_MIDDLE = '","packet":';
    private const DELIVERY_SUFFIX = "}\n";

    /**
     * Last octet of a VPN address, or 0 when the address is not a valid client
     * address. Rejects leading zeros and any non numeric tail.
     */
    public static function octetOf(string $ip): int
    {
        if (strncmp($ip, '10.99.99.', 9) !== 0) {
            return 0;
        }
        $tail = substr($ip, 9);
        if ($tail === '' || strlen($tail) > 3 || $tail !== (string) (int) $tail) {
            return 0;
        }
        $octet = (int) $tail;

        return ($octet >= 2 && $octet <= 254) ? $octet : 0;
    }

    public static function isAccessToken(string $token): bool
    {
        return preg_match(self::RE_TOKEN, $token) === 1;
    }

    public static function isBase64(string $value): bool
    {
        $length = strlen($value);

        return $length > 0
            && $length % 4 === 0
            && preg_match(self::RE_BASE64, $value) === 1;
    }

    /** Parses an ISO 8601 UTC timestamp. Returns null when malformed. */
    public static function parseTimestamp(string $value): ?int
    {
        if (preg_match(self::RE_TIMESTAMP, $value) !== 1) {
            return null;
        }

        return gmmktime(
            (int) substr($value, 11, 2),
            (int) substr($value, 14, 2),
            (int) substr($value, 17, 2),
            (int) substr($value, 5, 2),
            (int) substr($value, 8, 2),
            (int) substr($value, 0, 4)
        );
    }

    public static function isoTime(int $epoch): string
    {
        return gmdate('Y-m-d\TH:i:s\Z', $epoch);
    }

    /** Decodes one NDJSON line into an object, or throws a 400. */
    public static function decodeLine(string $line): array
    {
        $value = json_decode($line, true, 4);
        if (!is_array($value) || $value === [] || array_is_list($value)) {
            throw new HttpError(400, 'bad_request');
        }

        return $value;
    }

    /**
     * Serialised "packet" member of a delivery line.
     *
     * Safety: uuid, src, dst and data have already been validated against
     * character classes that exclude every byte JSON would need to escape.
     */
    public static function buildPacketFragment(string $uuid, string $src, string $dst, string $data): string
    {
        return '{"type":"data","uuid":"' . $uuid
            . '","src":"' . $src
            . '","dst":"' . $dst
            . '","data":"' . $data . '"}';
    }

    /** Constant byte overhead a fragment gains once wrapped as a delivery line. */
    public static function deliveryOverhead(): int
    {
        static $overhead = 0;
        if ($overhead === 0) {
            $overhead = strlen(self::DELIVERY_PREFIX)
                + Batch::ID_LENGTH
                + strlen(self::DELIVERY_MIDDLE)
                + strlen(self::DELIVERY_SUFFIX);
        }

        return $overhead;
    }

    /**
     * Joins fragments into the exact bytes stored on disk. Because batches are
     * persisted in their final delivery form, serving them later is a plain
     * byte copy with no JSON work at all.
     *
     * @param list<string> $fragments
     */
    public static function buildBatchPayload(string $batchId, array $fragments): string
    {
        $prefix = self::DELIVERY_PREFIX . $batchId . self::DELIVERY_MIDDLE;
        $payload = '';
        foreach ($fragments as $fragment) {
            $payload .= $prefix . $fragment . self::DELIVERY_SUFFIX;
        }

        return $payload;
    }

    /**
     * Final control line of a successful sync response.
     *
     * @param list<string> $accepted
     * @param list<string> $deferred
     */
    public static function buildSyncResult(
        array $accepted,
        array $deferred,
        int $acceptedPackets,
        int $acceptedBytes,
        bool $truncated,
        int $recommendedBytes,
        int $recommendedPackets
    ): string {
        return '{"type":"sync-result","acceptedPacketUuids":[' . self::uuidList($accepted)
            . '],"deferredPacketUuids":[' . self::uuidList($deferred)
            . '],"acceptedPackets":' . $acceptedPackets
            . ',"acceptedBytes":' . $acceptedBytes
            . ',"responseTruncated":' . ($truncated ? 'true' : 'false')
            . ',"recommendedRequestBytes":' . $recommendedBytes
            . ',"recommendedRequestPackets":' . $recommendedPackets
            . "}\n";
    }

    /** @param list<string> $uuids all previously validated against RE_UUID */
    private static function uuidList(array $uuids): string
    {
        return $uuids === [] ? '' : '"' . implode('","', $uuids) . '"';
    }
}

// =============================================================================
// Batch identifiers
// =============================================================================

/**
 * A batch id is "<UTC timestamp>_<random>.<payload size>" and has a fixed
 * length, which lets the payload size be known before the id is generated.
 * Example: 20260808T142000.123456Z_9f2c1ab7d4e60518.000083214
 *
 * The leading timestamp makes plain lexicographic ordering equal to FIFO order,
 * and the trailing size lets a reader budget a response without any stat call.
 */
final class Batch
{
    public const ID_LENGTH = 50;
    private const RE_ID = '/^\d{8}T\d{6}\.\d{6}Z_[0-9a-f]{16}\.\d{9}$/';
    private const SIZE_OFFSET = 41;

    public static function newId(int $payloadBytes): string
    {
        $time = gettimeofday();

        return gmdate('Ymd\THis', $time['sec'])
            . '.' . sprintf('%06d', $time['usec'])
            . 'Z_' . Ctx::hex(8)
            . '.' . sprintf('%09d', $payloadBytes);
    }

    public static function isValidId(string $id): bool
    {
        return strlen($id) === self::ID_LENGTH && preg_match(self::RE_ID, $id) === 1;
    }

    public static function bytesOf(string $id): int
    {
        return (int) substr($id, self::SIZE_OFFSET);
    }

    /**
     * Creation time embedded in the id. Used only as an absolute age backstop,
     * because a reclaimed .sending file has a refreshed filemtime and could
     * otherwise be recycled forever. Age driven maintenance still uses
     * filemtime, as design2.md section 10 requires.
     */
    public static function epochOf(string $id): ?int
    {
        if (!self::isValidId($id)) {
            return null;
        }

        return gmmktime(
            (int) substr($id, 9, 2),
            (int) substr($id, 11, 2),
            (int) substr($id, 13, 2),
            (int) substr($id, 4, 2),
            (int) substr($id, 6, 2),
            (int) substr($id, 0, 4)
        );
    }
}

// =============================================================================
// Streaming NDJSON body reader
// =============================================================================

/**
 * Reads the request body once, in chunks, while hashing everything after the
 * auth line. Nothing is spooled to disk and no more than one line plus the
 * accepted payload is ever held in memory.
 */
final class BodyReader
{
    private const CHUNK = 65536;
    private const COMPACT_AT = 32768;

    /** @var resource */
    private $stream;
    private string $buffer = '';
    private int $offset = 0;
    private int $consumed = 0;
    private bool $eof = false;
    private bool $discard = false;
    private ?HashContext $hash = null;
    private int $hashFrom = PHP_INT_MAX;

    public function __construct()
    {
        $stream = fopen('php://input', 'rb');
        if ($stream === false) {
            throw new HttpError(400, 'bad_request');
        }
        $this->stream = $stream;
    }

    public function __destruct()
    {
        if (is_resource($this->stream)) {
            fclose($this->stream);
        }
    }

    /** Starts hashing at the current read position, retroactively if buffered. */
    public function beginHash(): void
    {
        $this->hash = hash_init('sha256');
        $this->hashFrom = $this->consumed - strlen($this->buffer) + $this->offset;
        if ($this->offset < strlen($this->buffer)) {
            hash_update($this->hash, substr($this->buffer, $this->offset));
        }
    }

    public function finishHash(): string
    {
        if ($this->hash === null) {
            return hash('sha256', '');
        }

        return hash_final($this->hash);
    }

    /**
     * Next line without its terminator, or null at end of body.
     *
     * A trailing fragment without a newline is a protocol error, because the
     * format guarantees every record is newline terminated.
     */
    public function readLine(int $maxLength): ?string
    {
        for (;;) {
            $newline = strpos($this->buffer, "\n", $this->offset);
            if ($newline !== false) {
                $length = $newline - $this->offset;
                if ($length > $maxLength) {
                    throw new HttpError(413, 'payload_too_large');
                }
                $line = substr($this->buffer, $this->offset, $length);
                $this->offset = $newline + 1;
                $this->compact();

                return $line;
            }
            if (strlen($this->buffer) - $this->offset > $maxLength) {
                throw new HttpError(413, 'payload_too_large');
            }
            if (!$this->fill()) {
                if ($this->offset < strlen($this->buffer)) {
                    throw new HttpError(400, 'bad_request');
                }

                return null;
            }
        }
    }

    /**
     * Consumes and hashes the remainder without parsing or buffering it.
     *
     * This keeps the signature verifiable even after a parse error, so an
     * unauthenticated caller always sees 401 rather than a parser oracle.
     */
    public function drain(): void
    {
        $this->discard = true;
        $this->buffer = '';
        $this->offset = 0;
        while ($this->fill()) {
            // fill() discards as it goes.
        }
    }

    private function compact(): void
    {
        if ($this->offset >= self::COMPACT_AT) {
            $this->buffer = substr($this->buffer, $this->offset);
            $this->offset = 0;
        }
    }

    private function fill(): bool
    {
        if ($this->eof) {
            return false;
        }
        $room = MAX_REQUEST_BYTES - $this->consumed;
        if ($room <= 0) {
            $probe = fread($this->stream, 1);
            if (is_string($probe) && $probe !== '') {
                throw new HttpError(413, 'payload_too_large');
            }
            $this->eof = true;

            return false;
        }

        $chunk = fread($this->stream, min(self::CHUNK, $room));
        if ($chunk === false || $chunk === '') {
            $this->eof = true;

            return false;
        }

        $chunkStart = $this->consumed;
        $this->consumed += strlen($chunk);

        if ($this->hash !== null) {
            if ($chunkStart >= $this->hashFrom) {
                hash_update($this->hash, $chunk);
            } elseif ($this->consumed > $this->hashFrom) {
                hash_update($this->hash, substr($chunk, $this->hashFrom - $chunkStart));
            }
        }

        if ($this->discard) {
            $this->buffer = '';
            $this->offset = 0;
        } else {
            $this->buffer .= $chunk;
        }

        return true;
    }
}

// =============================================================================
// Authentication
// =============================================================================

final class Auth
{
    /** Used for unknown clients so that response timing does not leak the table. */
    private const DUMMY_TOKEN = '0000000000000000000000000000000000000000000000000000000000000000';

    /**
     * Validates the auth record. Structural problems are a 400, anything that
     * depends on who the caller claims to be is a 401.
     *
     * @return array{client:string,octet:int,timestamp:string,nonce:string,signature:string}
     */
    public static function parseLine(string $line): array
    {
        $record = Wire::decodeLine($line);
        if (count($record) !== 5
            || ($record['type'] ?? null) !== 'auth'
            || !isset($record['client'], $record['timestamp'], $record['nonce'], $record['signature'])
        ) {
            throw new HttpError(400, 'bad_request');
        }

        $client = $record['client'];
        $timestamp = $record['timestamp'];
        $nonce = $record['nonce'];
        $signature = $record['signature'];
        if (!is_string($client) || !is_string($timestamp) || !is_string($nonce) || !is_string($signature)) {
            throw new HttpError(400, 'bad_request');
        }

        $octet = Wire::octetOf($client);
        if ($octet === 0
            || preg_match(Wire::RE_NONCE, $nonce) !== 1
            || preg_match(Wire::RE_SIGNATURE, $signature) !== 1
        ) {
            throw new HttpError(401, 'unauthorized');
        }

        $epoch = Wire::parseTimestamp($timestamp);
        if ($epoch === null || abs(Ctx::$now - $epoch) > CLOCK_SKEW_SECONDS) {
            throw new HttpError(401, 'unauthorized');
        }

        return [
            'client' => $client,
            'octet' => $octet,
            'timestamp' => $timestamp,
            'nonce' => $nonce,
            'signature' => $signature,
        ];
    }

    /**
     * Verifies the HMAC over the canonical request. The work is identical for
     * known and unknown clients, so timing does not disclose configured IPs.
     *
     * @param array{client:string,timestamp:string,nonce:string,signature:string} $auth
     */
    public static function verify(array $auth, string $action, string $bodyHashHex): void
    {
        $token = ACCESS_TOKENS[$auth['client']] ?? null;
        $known = is_string($token) && Wire::isAccessToken($token);
        if (!$known) {
            $token = self::DUMMY_TOKEN;
        }

        $derivedKey = hash_hmac('sha256', $auth['client'] . "\n" . $token, SERVER_SECRET, true);
        $canonical = "POST\n"
            . $action . "\n"
            . $auth['client'] . "\n"
            . $auth['timestamp'] . "\n"
            . $auth['nonce'] . "\n"
            . $bodyHashHex;
        $expected = hash_hmac('sha256', $canonical, $derivedKey);

        if (!hash_equals($expected, $auth['signature']) || !$known) {
            throw new HttpError(401, 'unauthorized');
        }
    }
}

// =============================================================================
// Replay protection
// =============================================================================

/**
 * Nonces live in per client, per minute buckets of fixed width records instead
 * of one file per nonce. At four requests per second a client would otherwise
 * need roughly 480 inodes, which shared hosting inode quotas do not tolerate.
 */
final class NonceStore
{
    private const RECORD_BYTES = 8;

    /** True when the nonce is fresh and was recorded, false when replayed. */
    public static function claim(int $octet, string $nonce, int $now): bool
    {
        $dir = QueuePath::nonceDir($octet);
        if ($dir === null) {
            throw new HttpError(503, 'queue_unavailable');
        }

        $key = substr(hash('sha256', $nonce, true), 0, self::RECORD_BYTES);
        $bucket = intdiv($now, NONCE_BUCKET_SECONDS);
        $path = $dir . '/' . $bucket . '.bin';

        $handle = @fopen($path, 'c+b');
        if ($handle === false) {
            throw new HttpError(503, 'queue_unavailable');
        }

        try {
            if (!self::lock($handle)) {
                throw new HttpError(503, 'queue_busy');
            }
            for ($i = 1; $i < NONCE_WINDOW_BUCKETS; $i++) {
                $previous = @file_get_contents($dir . '/' . ($bucket - $i) . '.bin');
                if (is_string($previous) && self::contains($previous, $key)) {
                    return false;
                }
            }

            $current = stream_get_contents($handle);
            if (!is_string($current)) {
                throw new HttpError(503, 'queue_unavailable');
            }
            if (self::contains($current, $key)) {
                return false;
            }
            if (strlen($current) >= MAX_NONCES_PER_BUCKET * self::RECORD_BYTES) {
                // Fail closed: dropping the check would silently allow replay.
                throw new HttpError(429, 'queue_busy');
            }
            if (fwrite($handle, $key) !== self::RECORD_BYTES) {
                throw new HttpError(503, 'queue_unavailable');
            }
            fflush($handle);

            return true;
        } finally {
            @flock($handle, LOCK_UN);
            fclose($handle);
        }
    }

    /** Fixed width records mean a match must start on a record boundary. */
    private static function contains(string $blob, string $key): bool
    {
        $offset = 0;
        while (($found = strpos($blob, $key, $offset)) !== false) {
            if ($found % self::RECORD_BYTES === 0) {
                return true;
            }
            $offset = $found + 1;
        }

        return false;
    }

    /** @param resource $handle */
    private static function lock($handle): bool
    {
        $deadline = hrtime(true) + LOCK_TIMEOUT_MS * 1000000;
        $sleep = 250;
        while (hrtime(true) < $deadline) {
            if (@flock($handle, LOCK_EX | LOCK_NB)) {
                return true;
            }
            usleep($sleep + random_int(0, $sleep));
            $sleep = min($sleep * 2, LOCK_RETRY_MAX_SLEEP_US);
        }

        return false;
    }
}

// =============================================================================
// Queue paths
// =============================================================================

final class QueuePath
{
    private const SUBNET_DIR = '10_99_99_0';

    private static ?string $root = null;
    private static ?bool $rootReady = null;

    public static function root(): string
    {
        if (self::$root === null) {
            $configured = trim(QUEUE_ROOT);
            self::$root = $configured !== ''
                ? rtrim(str_replace('\\', '/', $configured), '/')
                : rtrim(str_replace('\\', '/', __DIR__), '/') . '/phpvpn';
        }

        return self::$root;
    }

    /** Creates the root plus its defence in depth guards exactly once. */
    public static function ensureRoot(): bool
    {
        if (self::$rootReady !== null) {
            return self::$rootReady;
        }
        $root = self::root();
        if (!self::mkdirp($root)) {
            return self::$rootReady = false;
        }
        if (!is_file($root . '/.htaccess')) {
            @file_put_contents(
                $root . '/.htaccess',
                "Require all denied\n<IfModule !mod_authz_core.c>\nDeny from all\n</IfModule>\n"
            );
        }
        if (!is_file($root . '/index.html')) {
            @file_put_contents($root . '/index.html', '');
        }

        return self::$rootReady = true;
    }

    /** The only place a queue path is ever built, and only from a valid octet. */
    public static function queueDir(int $octet): string
    {
        return self::root() . '/' . self::SUBNET_DIR . '/' . sprintf('%03d', $octet);
    }

    public static function nonceDir(int $octet): ?string
    {
        $dir = self::root() . '/_nonce/' . sprintf('%03d', $octet);

        return self::mkdirp($dir) ? $dir : null;
    }

    public static function stateDir(): ?string
    {
        $dir = self::root() . '/_state';

        return self::mkdirp($dir) ? $dir : null;
    }

    public static function logDir(): ?string
    {
        $dir = self::root() . '/_logs';

        return self::mkdirp($dir) ? $dir : null;
    }

    public static function subnetDir(): string
    {
        return self::root() . '/' . self::SUBNET_DIR;
    }

    /** Tolerates a concurrent creation of the same directory. */
    public static function mkdirp(string $dir): bool
    {
        if (is_dir($dir)) {
            return true;
        }
        if (@mkdir($dir, 0700, true)) {
            return true;
        }
        clearstatcache(true, $dir);

        return is_dir($dir);
    }
}

// =============================================================================
// Queue locking
// =============================================================================

/**
 * Per queue mutual exclusion.
 *
 * Always non blocking with a fixed time budget, because a blocking flock can
 * hold a worker until max_execution_time and turn contention into an outage.
 * Only one lock is ever held at a time, which rules out deadlock by design.
 */
final class QueueLock
{
    /** @return resource|null null means the budget was exhausted */
    public static function acquire(string $dir)
    {
        $path = $dir . '/queue.lock';
        $handle = @fopen($path, 'c+b');
        if ($handle === false) {
            if (!QueuePath::mkdirp($dir)) {
                return null;
            }
            $handle = @fopen($path, 'c+b');
            if ($handle === false) {
                return null;
            }
        }

        $started = hrtime(true);
        $deadline = $started + LOCK_TIMEOUT_MS * 1000000;
        $sleep = 250;
        for (;;) {
            if (@flock($handle, LOCK_EX | LOCK_NB)) {
                Ctx::$lockWaitNs += hrtime(true) - $started;

                return $handle;
            }
            if (hrtime(true) >= $deadline) {
                Ctx::$lockWaitNs += hrtime(true) - $started;
                fclose($handle);

                return null;
            }
            usleep($sleep + random_int(0, $sleep));
            $sleep = min($sleep * 2, LOCK_RETRY_MAX_SLEEP_US);
        }
    }

    /** @param resource $handle */
    public static function release($handle): void
    {
        @flock($handle, LOCK_UN);
        fclose($handle);
    }
}

// =============================================================================
// Per queue accounting
// =============================================================================

/**
 * A tiny sidecar file holding the queue size, byte total and last GC time.
 *
 * It removes the directory listing from the write path, which is the hottest
 * filesystem operation when many senders target the same recipient, and it
 * doubles as the shard used to compute the global quota without a global lock.
 */
final class QueueStat
{
    /** @return array{files:int,bytes:int,gc:int} */
    public static function load(string $dir): ?array
    {
        $raw = @file_get_contents($dir . '/queue.stat');
        if (!is_string($raw)) {
            return null;
        }
        $parts = explode("\t", trim($raw));
        if (count($parts) !== 4 || $parts[0] !== '1') {
            return null;
        }

        return [
            'files' => (int) $parts[1],
            'bytes' => (int) $parts[2],
            'gc' => (int) $parts[3],
        ];
    }

    /** Callers must already hold the queue lock. */
    public static function save(string $dir, int $files, int $bytes, int $gcAt): void
    {
        @file_put_contents(
            $dir . '/queue.stat',
            "1\t" . $files . "\t" . $bytes . "\t" . $gcAt . "\n"
        );
    }
}

// =============================================================================
// Queue maintenance
// =============================================================================

final class Gc
{
    /**
     * Single pass over one queue directory. Optionally applies maintenance.
     *
     * Age is always taken from filemtime, never from the file name, so a batch
     * that survived a crash is judged by what the filesystem knows about it.
     *
     * @return array{files:int,bytes:int,data:array<string,int>}
     */
    public static function scan(string $dir, int $now, bool $maintain): array
    {
        $handle = @opendir($dir);
        if ($handle === false) {
            return ['files' => 0, 'bytes' => 0, 'data' => []];
        }

        $files = 0;
        $bytes = 0;
        $data = [];

        while (($entry = readdir($handle)) !== false) {
            if ($entry === '.' || $entry === '..' || $entry === 'queue.lock' || $entry === 'queue.stat') {
                continue;
            }
            $path = $dir . '/' . $entry;

            if (str_ends_with($entry, '.data')) {
                $id = substr($entry, 0, -5);
                if (!Batch::isValidId($id)) {
                    self::discard($path);
                    continue;
                }
                if ($maintain && self::expired($path, $id, $now)) {
                    self::discard($path);
                    continue;
                }
                $size = Batch::bytesOf($id);
                $data[$id] = $size;
                $files++;
                $bytes += $size;
                continue;
            }

            if (str_ends_with($entry, '.sending')) {
                $id = substr($entry, 0, -8);
                if (!Batch::isValidId($id)) {
                    self::discard($path);
                    continue;
                }
                $size = Batch::bytesOf($id);
                if ($maintain && self::reclaim($dir, $id, $path, $now)) {
                    $data[$id] = $size;
                }
                $files++;
                $bytes += $size;
                continue;
            }

            if (str_ends_with($entry, '.writing')) {
                if ($maintain && self::ageOf($path, $now) > WRITING_STALE_SECONDS) {
                    self::discard($path);
                }
                continue;
            }

            if ($maintain && str_ends_with($entry, '.error')
                && self::ageOf($path, $now) > ERROR_FILE_TTL_SECONDS
            ) {
                @unlink($path);
            }
        }
        closedir($handle);

        return ['files' => $files, 'bytes' => $bytes, 'data' => $data];
    }

    /**
     * Returns a claimed batch to the queue, or drops it once it is hopeless.
     *
     * Reclaiming refreshes filemtime, so the absolute age from the batch id is
     * the backstop that prevents a batch from being recycled forever.
     */
    private static function reclaim(string $dir, string $id, string $path, int $now): bool
    {
        $createdAt = Batch::epochOf($id);
        if ($createdAt !== null && $now - $createdAt > QUEUE_TTL_SECONDS) {
            self::discard($path);

            return false;
        }
        if (self::ageOf($path, $now) <= SENDING_RECLAIM_SECONDS) {
            return false;
        }

        // On Windows a rename fails while another worker still streams the
        // file. That is expected, not fatal: the next sweep tries again.
        return @rename($path, $dir . '/' . $id . '.data');
    }

    /**
     * True once a published batch has outlived the TTL.
     *
     * filemtime is authoritative, exactly as design2.md section 10 requires.
     * The time embedded in the id is consulted only as an additional upper
     * bound, so a reclaimed batch cannot keep extending its own life. The name
     * can therefore shorten a lifetime but never lengthen one.
     */
    private static function expired(string $path, string $id, int $now): bool
    {
        if (self::ageOf($path, $now) > QUEUE_TTL_SECONDS) {
            return true;
        }
        $createdAt = Batch::epochOf($id);

        return $createdAt !== null && $now - $createdAt > QUEUE_TTL_SECONDS;
    }

    private static function ageOf(string $path, int $now): int
    {
        clearstatcache(true, $path);
        $modified = @filemtime($path);

        return $modified === false ? 0 : $now - $modified;
    }

    /** Removes a file, or parks it as .error when removal is impossible. */
    private static function discard(string $path): void
    {
        if (@unlink($path)) {
            return;
        }
        clearstatcache(true, $path);
        if (is_file($path)) {
            @rename($path, $path . '.error');
        }
    }
}

// =============================================================================
// Global quota
// =============================================================================

/**
 * Shared byte and inode ceiling for the whole installation.
 *
 * Shared hosting bills inodes as well as bytes, and MAX_BUFFERS alone only
 * bounds a single queue. The aggregate is refreshed by the sweeper, so reading
 * it costs one small file read and never contends on a global lock.
 */
final class Quota
{
    /** @return array{files:int,bytes:int,at:int} */
    public static function snapshot(): array
    {
        static $cached = null;
        if ($cached !== null) {
            return $cached;
        }
        $dir = QueuePath::stateDir();
        $raw = $dir === null ? false : @file_get_contents($dir . '/global.stat');
        if (is_string($raw)) {
            $parts = explode("\t", trim($raw));
            if (count($parts) === 4 && $parts[0] === '1') {
                return $cached = [
                    'files' => (int) $parts[1],
                    'bytes' => (int) $parts[2],
                    'at' => (int) $parts[3],
                ];
            }
        }

        return $cached = ['files' => 0, 'bytes' => 0, 'at' => 0];
    }

    public static function allows(int $additionalBytes): bool
    {
        $snapshot = self::snapshot();
        if ($snapshot['at'] === 0 || Ctx::$now - $snapshot['at'] > GLOBAL_STAT_TTL_SECONDS * 6) {
            return true; // Stale or missing aggregate must not block traffic.
        }

        return $snapshot['files'] + 1 <= MAX_TOTAL_QUEUE_FILES
            && $snapshot['bytes'] + $additionalBytes <= MAX_TOTAL_QUEUE_BYTES;
    }

    /** Fraction of the tighter of the two global ceilings, clamped to [0, 1]. */
    public static function fill(): float
    {
        $snapshot = self::snapshot();
        if ($snapshot['at'] === 0) {
            return 0.0;
        }
        $byFiles = $snapshot['files'] / MAX_TOTAL_QUEUE_FILES;
        $byBytes = $snapshot['bytes'] / MAX_TOTAL_QUEUE_BYTES;

        return min(1.0, max(0.0, $byFiles, $byBytes));
    }

    public static function store(int $files, int $bytes): void
    {
        $dir = QueuePath::stateDir();
        if ($dir === null) {
            return;
        }
        @file_put_contents(
            $dir . '/global.stat',
            "1\t" . $files . "\t" . $bytes . "\t" . Ctx::$now . "\n"
        );
    }
}

// =============================================================================
// Queue operations
// =============================================================================

final class Queue
{
    public const OK = 0;
    public const FULL = 1;
    public const UNAVAILABLE = 2;

    /**
     * Publishes one batch into a destination queue.
     *
     * The write is a create, a full write and an atomic rename inside the same
     * directory, so a reader never observes a partial batch.
     *
     * @param list<string> $fragments
     */
    public static function enqueue(int $octet, array $fragments, int $expectedBytes, float &$fill): int
    {
        $dir = QueuePath::queueDir($octet);
        $lock = QueueLock::acquire($dir);
        if ($lock === null) {
            return self::UNAVAILABLE;
        }

        try {
            $stat = QueueStat::load($dir);
            $due = $stat === null
                || $stat['files'] >= MAX_BUFFERS
                || Ctx::$now - $stat['gc'] >= GC_INTERVAL_SECONDS;
            if ($due) {
                $scan = Gc::scan($dir, Ctx::$now, true);
                $stat = ['files' => $scan['files'], 'bytes' => $scan['bytes'], 'gc' => Ctx::$now];
                QueueStat::save($dir, $stat['files'], $stat['bytes'], $stat['gc']);
            }

            $fill = max($fill, $stat['files'] / MAX_BUFFERS);
            if ($stat['files'] >= MAX_BUFFERS || !Quota::allows($expectedBytes)) {
                return self::FULL;
            }

            $id = Batch::newId($expectedBytes);
            $payload = Wire::buildBatchPayload($id, $fragments);
            if (strlen($payload) !== $expectedBytes) {
                // The size is part of the file name, so a mismatch would break
                // response budgeting. Refuse rather than publish a wrong size.
                Log::error('batch_size_mismatch', ['expected' => $expectedBytes, 'actual' => strlen($payload)]);

                return self::UNAVAILABLE;
            }

            if (!self::writeFile($dir . '/' . $id . '.writing', $payload)) {
                return self::UNAVAILABLE;
            }
            if (!@rename($dir . '/' . $id . '.writing', $dir . '/' . $id . '.data')) {
                @unlink($dir . '/' . $id . '.writing');
                Ctx::$writingPath = null;

                return self::UNAVAILABLE;
            }
            Ctx::$writingPath = null;

            QueueStat::save($dir, $stat['files'] + 1, $stat['bytes'] + $expectedBytes, $stat['gc']);

            return self::OK;
        } finally {
            QueueLock::release($lock);
        }
    }

    /**
     * Acknowledges delivered batches, then claims the oldest pending ones.
     *
     * Claiming only renames. The payload is streamed after the lock is gone, so
     * the lock is never held while the response is being written out.
     *
     * @param list<string> $ackIds
     * @return array{claimed:list<array{id:string,path:string,bytes:int}>,truncated:bool,fill:float,busy:bool}
     */
    public static function collect(int $octet, array $ackIds, int $maxBytes): array
    {
        $result = ['claimed' => [], 'truncated' => false, 'fill' => 0.0, 'busy' => false];

        $dir = QueuePath::queueDir($octet);
        $lock = QueueLock::acquire($dir);
        if ($lock === null) {
            $result['busy'] = true;
            $result['fill'] = 1.0;

            return $result;
        }

        try {
            foreach ($ackIds as $id) {
                // A missing file is the normal at-least-once outcome, not an error.
                @unlink($dir . '/' . $id . '.sending');
            }

            $stat = QueueStat::load($dir);
            $maintain = $stat === null || Ctx::$now - $stat['gc'] >= GC_INTERVAL_SECONDS;
            $scan = Gc::scan($dir, Ctx::$now, $maintain);

            $ids = array_keys($scan['data']);
            sort($ids, SORT_STRING); // The id prefix is a timestamp, so this is FIFO.

            $claimed = [];
            $total = 0;
            foreach ($ids as $id) {
                $size = $scan['data'][$id];
                if ($claimed !== [] && $total + $size > $maxBytes) {
                    break;
                }
                $claimedPath = $dir . '/' . $id . '.sending';
                if (!@rename($dir . '/' . $id . '.data', $claimedPath)) {
                    continue; // Another worker won the race or the file is busy.
                }
                // rename keeps the original mtime, so stamp the claim instead.
                // SENDING_RECLAIM_SECONDS must count from hand over, not from
                // creation, or a batch claimed late would be reclaimed at once.
                @touch($claimedPath);
                $claimed[] = ['id' => $id, 'path' => $claimedPath, 'bytes' => $size];
                $total += $size;
                if ($total >= $maxBytes) {
                    break;
                }
            }

            QueueStat::save(
                $dir,
                $scan['files'],
                $scan['bytes'],
                $maintain ? Ctx::$now : ($stat['gc'] ?? Ctx::$now)
            );

            $result['claimed'] = $claimed;
            $result['truncated'] = count($claimed) < count($ids);
            $result['fill'] = $scan['files'] / MAX_BUFFERS;

            return $result;
        } finally {
            QueueLock::release($lock);
        }
    }

    /** Writes the whole payload, tolerating short writes. */
    private static function writeFile(string $path, string $payload): bool
    {
        $handle = @fopen($path, 'x+b');
        if ($handle === false) {
            return false;
        }
        Ctx::$writingPath = $path;

        $total = strlen($payload);
        $written = 0;
        while ($written < $total) {
            $count = @fwrite($handle, $written === 0 ? $payload : substr($payload, $written));
            if ($count === false || $count === 0) {
                fclose($handle);
                @unlink($path);
                Ctx::$writingPath = null;

                return false;
            }
            $written += $count;
        }

        $flushed = @fflush($handle);
        if ($flushed && FSYNC_ON_PUBLISH && function_exists('fsync')) {
            @fsync($handle);
        }
        fclose($handle);

        if (!$flushed) {
            @unlink($path);
            Ctx::$writingPath = null;

            return false;
        }

        return true;
    }
}

// =============================================================================
// Opportunistic global maintenance
// =============================================================================

/**
 * Without cron, maintenance only happens while requests arrive. A queue whose
 * client never returns would otherwise keep its artefacts forever, so a small,
 * throttled and strictly bounded slice of global work runs after each response.
 */
final class Sweeper
{
    public static function maybeRun(): void
    {
        $stateDir = QueuePath::stateDir();
        if ($stateDir === null) {
            return;
        }

        $lockPath = $stateDir . '/sweep.lock';
        clearstatcache(true, $lockPath);
        $last = @filemtime($lockPath);
        if ($last !== false && Ctx::$now - $last < GLOBAL_SWEEP_INTERVAL_SEC) {
            return;
        }

        $lock = @fopen($lockPath, 'c+b');
        if ($lock === false) {
            return;
        }
        if (!@flock($lock, LOCK_EX | LOCK_NB)) {
            fclose($lock); // Another worker is already sweeping.

            return;
        }

        try {
            @touch($lockPath);
            self::sweepQueues($stateDir);
            self::sweepNonces();
            self::rotateLogs();
        } catch (Throwable $error) {
            Log::error('sweep_failed', ['type' => get_class($error)]);
        } finally {
            @flock($lock, LOCK_UN);
            fclose($lock);
        }
    }

    /** Visits a bounded slice of queues per sweep, round robin. */
    private static function sweepQueues(string $stateDir): void
    {
        $subnet = QueuePath::subnetDir();
        $handle = @opendir($subnet);
        if ($handle === false) {
            return;
        }
        $names = [];
        while (($entry = readdir($handle)) !== false) {
            if (strlen($entry) === 3 && ctype_digit($entry)) {
                $names[] = $entry;
            }
        }
        closedir($handle);
        sort($names, SORT_STRING);
        if ($names === []) {
            Quota::store(0, 0);

            return;
        }

        $cursorPath = $stateDir . '/sweep.cursor';
        $cursor = (int) @file_get_contents($cursorPath);
        $count = count($names);
        $slice = min(GLOBAL_SWEEP_MAX_DIRS, $count);

        $totalFiles = 0;
        $totalBytes = 0;
        for ($i = 0; $i < $count; $i++) {
            $name = $names[$i];
            $dir = $subnet . '/' . $name;
            $maintain = $i >= $cursor && $i < $cursor + $slice;

            if ($maintain) {
                $lock = QueueLock::acquire($dir);
                if ($lock === null) {
                    $maintain = false;
                } else {
                    try {
                        $scan = Gc::scan($dir, Ctx::$now, true);
                        QueueStat::save($dir, $scan['files'], $scan['bytes'], Ctx::$now);
                        $totalFiles += $scan['files'];
                        $totalBytes += $scan['bytes'];
                    } finally {
                        QueueLock::release($lock);
                    }
                }
            }
            if (!$maintain) {
                $stat = QueueStat::load($dir);
                $totalFiles += $stat['files'] ?? 0;
                $totalBytes += $stat['bytes'] ?? 0;
            }
        }

        @file_put_contents($cursorPath, (string) (($cursor + $slice) % $count));
        Quota::store($totalFiles, $totalBytes);
    }

    private static function sweepNonces(): void
    {
        if (!PERSIST_NONCES) {
            return;
        }
        $root = QueuePath::root() . '/_nonce';
        $handle = @opendir($root);
        if ($handle === false) {
            return;
        }
        $oldest = intdiv(Ctx::$now, NONCE_BUCKET_SECONDS) - NONCE_WINDOW_BUCKETS - 1;
        while (($entry = readdir($handle)) !== false) {
            if ($entry === '.' || $entry === '..') {
                continue;
            }
            $inner = @opendir($root . '/' . $entry);
            if ($inner === false) {
                continue;
            }
            while (($bucket = readdir($inner)) !== false) {
                if (!str_ends_with($bucket, '.bin')) {
                    continue;
                }
                if ((int) substr($bucket, 0, -4) < $oldest) {
                    @unlink($root . '/' . $entry . '/' . $bucket);
                }
            }
            closedir($inner);
        }
        closedir($handle);
    }

    private static function rotateLogs(): void
    {
        $dir = QueuePath::logDir();
        if ($dir === null) {
            return;
        }
        $cutoff = gmdate('Y-m-d', Ctx::$now - LOG_RETENTION_DAYS * 86400);
        $handle = @opendir($dir);
        if ($handle === false) {
            return;
        }
        while (($entry = readdir($handle)) !== false) {
            if (str_ends_with($entry, '.log') && substr($entry, 0, -4) < $cutoff) {
                @unlink($dir . '/' . $entry);
            }
        }
        closedir($handle);
    }
}

// =============================================================================
// Sync
// =============================================================================

final class SyncHandler
{
    public static function handle(): void
    {
        self::assertPreconditions();
        Cfg::assertRuntime();
        if (!QueuePath::ensureRoot()) {
            throw new HttpError(503, 'queue_unavailable');
        }

        $reader = new BodyReader();
        $authLine = $reader->readLine(MAX_AUTH_LINE_BYTES);
        if ($authLine === null) {
            throw new HttpError(400, 'bad_request');
        }
        $reader->beginHash();

        $auth = Auth::parseLine($authLine);
        Ctx::$client = $auth['client'];

        $body = self::readBody($reader, $auth['client']);
        Auth::verify($auth, 'sync', $reader->finishHash());

        // Only now, with the caller proven, may a parse error be reported and
        // may anything at all be written to disk.
        if ($body['error'] instanceof HttpError) {
            throw $body['error'];
        }
        if (PERSIST_NONCES && !NonceStore::claim($auth['octet'], $auth['nonce'], Ctx::$now)) {
            throw new HttpError(401, 'unauthorized');
        }

        $outcome = self::publish($body);
        $own = Queue::collect($auth['octet'], $body['acks'], MAX_RESPONSE_BYTES);

        self::respond($body, $outcome, $own);
    }

    private static function assertPreconditions(): void
    {
        $type = Http::contentType();
        if (!str_starts_with($type, 'application/x-ndjson') && !str_starts_with($type, 'application/jsonl')) {
            throw new HttpError(415, 'unsupported_media_type');
        }
        $length = Http::contentLength();
        if ($length > MAX_REQUEST_BYTES) {
            throw new HttpError(413, 'payload_too_large');
        }
    }

    /**
     * Single pass over the remaining lines.
     *
     * A parse error stops parsing but not reading: the rest is still consumed
     * and hashed so the signature can be checked first.
     *
     * @return array{
     *   error:?HttpError, acks:list<string>, dest:array<int,array{fragments:list<string>,uuids:list<string>,bytes:int}>,
     *   accepted:list<string>, deferred:list<string>, acceptedPackets:int, acceptedBytes:int
     * }
     */
    private static function readBody(BodyReader $reader, string $client): array
    {
        $error = null;
        $acks = [];
        $dest = [];
        $accepted = [];
        $deferred = [];
        $packets = 0;
        $acceptedPackets = 0;
        $acceptedBytes = 0;
        $overhead = Wire::deliveryOverhead();

        for (;;) {
            try {
                $line = $reader->readLine(MAX_PACKET_LINE_BYTES);
                if ($line === null) {
                    break;
                }

                $record = Wire::decodeLine($line);
                $type = $record['type'] ?? null;

                if ($type === 'data') {
                    if (++$packets > MAX_REQUEST_PACKETS) {
                        throw new HttpError(413, 'payload_too_large');
                    }
                    $packet = self::parseDataRecord($record, $client);

                    if ($acceptedPackets >= MAX_ACCEPTED_REQUEST_PACKETS
                        || $acceptedBytes + strlen($line) > MAX_ACCEPTED_REQUEST_BYTES
                    ) {
                        $deferred[] = $packet['uuid'];
                        continue;
                    }

                    $octet = $packet['octet'];
                    if (!isset($dest[$octet])) {
                        $dest[$octet] = ['fragments' => [], 'uuids' => [], 'bytes' => 0, 'rawBytes' => 0];
                    }
                    $fragment = Wire::buildPacketFragment(
                        $packet['uuid'],
                        $client,
                        $packet['dst'],
                        $packet['data']
                    );
                    $dest[$octet]['fragments'][] = $fragment;
                    $dest[$octet]['uuids'][] = $packet['uuid'];
                    $dest[$octet]['bytes'] += strlen($fragment) + $overhead;
                    // Request bytes, not stored bytes. The client sizes its
                    // next batch from recommendedRequestBytes, so acceptedBytes
                    // has to be measured against the same yardstick.
                    $dest[$octet]['rawBytes'] += strlen($line);
                    $acceptedPackets++;
                    $acceptedBytes += strlen($line);
                    continue;
                }

                if ($type === 'ack') {
                    if (count($acks) >= MAX_ACKS_PER_REQUEST) {
                        throw new HttpError(400, 'bad_request');
                    }
                    $batchId = $record['batchId'] ?? null;
                    if (count($record) !== 2 || !is_string($batchId) || !Batch::isValidId($batchId)) {
                        throw new HttpError(400, 'bad_request');
                    }
                    $acks[] = $batchId;
                    continue;
                }

                // A second auth record, or anything unknown, is a protocol error.
                throw new HttpError(400, 'bad_request');
            } catch (HttpError $failure) {
                $error = $failure;
                break;
            }
        }

        if ($error !== null) {
            $reader->drain();
        }

        foreach ($dest as $entry) {
            foreach ($entry['uuids'] as $uuid) {
                $accepted[] = $uuid;
            }
        }

        return [
            'error' => $error,
            'acks' => $acks,
            'dest' => $dest,
            'accepted' => $accepted,
            'deferred' => $deferred,
            'acceptedPackets' => $acceptedPackets,
            'acceptedBytes' => $acceptedBytes,
        ];
    }

    /** @return array{uuid:string,dst:string,data:string,octet:int} */
    private static function parseDataRecord(array $record, string $client): array
    {
        if (count($record) !== 5
            || !isset($record['uuid'], $record['src'], $record['dst'], $record['data'])
        ) {
            throw new HttpError(400, 'bad_request');
        }

        $uuid = $record['uuid'];
        $src = $record['src'];
        $dst = $record['dst'];
        $data = $record['data'];
        if (!is_string($uuid) || !is_string($src) || !is_string($dst) || !is_string($data)) {
            throw new HttpError(400, 'bad_request');
        }
        if ($src !== $client) {
            throw new HttpError(401, 'unauthorized');
        }
        if (preg_match(Wire::RE_UUID, $uuid) !== 1
            || strlen($data) > MAX_PACKET_LINE_BYTES - 256
            || !Wire::isBase64($data)
        ) {
            throw new HttpError(400, 'bad_request');
        }
        $octet = Wire::octetOf($dst);
        if ($octet === 0) {
            throw new HttpError(400, 'bad_request');
        }

        return ['uuid' => $uuid, 'dst' => $dst, 'data' => $data, 'octet' => $octet];
    }

    /**
     * Writes every destination batch, one queue lock at a time.
     *
     * @return array{accepted:list<string>,deferred:list<string>,packets:int,bytes:int,fill:float,worst:int,served:bool}
     */
    private static function publish(array $body): array
    {
        $dest = $body['dest'];
        ksort($dest, SORT_NUMERIC); // Deterministic order, one lock at a time.

        $accepted = [];
        $deferred = $body['deferred'];
        $packets = 0;
        $bytes = 0;
        $fill = 0.0;
        $worst = Queue::OK;
        $served = $dest === [];

        foreach ($dest as $octet => $entry) {
            $status = Queue::enqueue($octet, $entry['fragments'], $entry['bytes'], $fill);
            if ($status === Queue::OK) {
                $served = true;
                foreach ($entry['uuids'] as $uuid) {
                    $accepted[] = $uuid;
                }
                $packets += count($entry['fragments']);
                $bytes += $entry['rawBytes'];
                continue;
            }
            $worst = max($worst, $status);
            foreach ($entry['uuids'] as $uuid) {
                $deferred[] = $uuid;
            }
            Log::error('enqueue_failed', ['dst' => $octet, 'status' => $status]);
        }

        if (!$served) {
            throw new HttpError(
                $worst === Queue::FULL ? 429 : 503,
                $worst === Queue::FULL ? 'queue_full' : 'queue_unavailable'
            );
        }

        return [
            'accepted' => $accepted,
            'deferred' => $deferred,
            'packets' => $packets,
            'bytes' => $bytes,
            'fill' => $fill,
            'worst' => $worst,
            'served' => $served,
        ];
    }

    private static function respond(array $body, array $outcome, array $own): void
    {
        $pressure = max($outcome['fill'], $own['fill'], Quota::fill());
        [$recommendedBytes, $recommendedPackets] = self::recommend($pressure, $own['busy']);

        $result = Wire::buildSyncResult(
            $outcome['accepted'],
            $outcome['deferred'],
            $outcome['packets'],
            $outcome['bytes'],
            $own['truncated'],
            $recommendedBytes,
            $recommendedPackets
        );

        http_response_code(200);
        Http::baseHeaders();
        header('Content-Type: application/x-ndjson; charset=utf-8');

        if (SEND_CONTENT_LENGTH) {
            $total = strlen($result);
            foreach ($own['claimed'] as $batch) {
                clearstatcache(true, $batch['path']);
                $size = @filesize($batch['path']);
                $total += $size === false ? $batch['bytes'] : $size;
            }
            header('Content-Length: ' . $total);
        }

        foreach ($own['claimed'] as $batch) {
            if (connection_aborted() !== 0) {
                break;
            }
            // A claimed batch is stored in its final delivery form, so serving
            // it is a byte copy with no parsing and no JSON encoding.
            if (@readfile($batch['path']) === false) {
                Log::error('delivery_read_failed', []);
                break;
            }
        }
        echo $result;

        Log::info('sync', [
            'inPackets' => $outcome['packets'],
            'inBytes' => $outcome['bytes'],
            'deferred' => count($outcome['deferred']),
            'outBatches' => count($own['claimed']),
            'truncated' => $own['truncated'],
        ]);
    }

    /**
     * Upper bounds advertised to the client for its next request.
     *
     * Quantised to powers of two so a client heuristic sees a stable signal
     * rather than a value that jitters with every cycle.
     *
     * @return array{0:int,1:int}
     */
    private static function recommend(float $pressure, bool $busy): array
    {
        $factor = 1.0;
        if ($pressure >= 0.875) {
            $factor = 0.125;
        } elseif ($pressure >= 0.75) {
            $factor = 0.25;
        } elseif ($pressure >= 0.5) {
            $factor = 0.5;
        }

        $lockWaitMs = Ctx::$lockWaitNs / 1e6;
        if ($busy || $lockWaitMs >= LOCK_TIMEOUT_MS * 0.5) {
            $factor = min($factor, 0.25);
        } elseif ($lockWaitMs >= LOCK_TIMEOUT_MS * 0.25) {
            $factor = min($factor, 0.5);
        }

        $elapsed = Ctx::elapsedMs();
        if ($elapsed >= REQUEST_BUDGET_MS) {
            $factor = min($factor, 0.25);
        } elseif ($elapsed >= REQUEST_BUDGET_MS * 0.5) {
            $factor = min($factor, 0.5);
        }

        return [
            max(16384, (int) (MAX_ACCEPTED_REQUEST_BYTES * $factor)),
            max(8, (int) (MAX_ACCEPTED_REQUEST_PACKETS * $factor)),
        ];
    }
}

// =============================================================================
// Preflight
// =============================================================================

/**
 * Environment checks that answer the questions in problems.md before a hosting
 * is trusted with production traffic. Disabled by default and always signed.
 */
final class SelfTest
{
    public static function handle(): void
    {
        if (!SELFTEST_ENABLED) {
            throw new HttpError(404, 'not_found');
        }

        $reader = new BodyReader();
        $authLine = $reader->readLine(MAX_AUTH_LINE_BYTES);
        if ($authLine === null) {
            throw new HttpError(400, 'bad_request');
        }
        $reader->beginHash();
        $auth = Auth::parseLine($authLine);
        Ctx::$client = $auth['client'];
        $reader->drain();
        Auth::verify($auth, 'selftest', $reader->finishHash());

        $checks = array_merge(
            self::checkRuntime(),
            self::checkFunctions(),
            self::checkStorage(),
            self::checkLimits(),
            self::checkExposure()
        );

        $failed = 0;
        $warned = 0;
        foreach ($checks as $check) {
            if ($check['result'] === 'fail') {
                $failed++;
            } elseif ($check['result'] === 'warn') {
                $warned++;
            }
        }

        http_response_code(200);
        Http::baseHeaders();
        header('Content-Type: application/json; charset=utf-8');
        echo json_encode([
            'status' => $failed === 0 ? 'OK' : 'FAILED',
            'failed' => $failed,
            'warnings' => $warned,
            'serverTime' => Wire::isoTime(Ctx::$now),
            'checks' => $checks,
        ], JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT);
    }

    private static function check(string $name, string $result, string $detail): array
    {
        return ['name' => $name, 'result' => $result, 'detail' => $detail];
    }

    private static function checkRuntime(): array
    {
        $checks = [];
        $checks[] = self::check(
            'php_version',
            version_compare(PHP_VERSION, '8.2.0', '>=') ? 'pass' : 'fail',
            PHP_VERSION
        );
        $checks[] = self::check(
            'sha256',
            in_array('sha256', hash_algos(), true) ? 'pass' : 'fail',
            'hash_algos'
        );
        try {
            random_bytes(16);
            $checks[] = self::check('csprng', 'pass', 'random_bytes works');
        } catch (Throwable) {
            $checks[] = self::check('csprng', 'fail', 'random_bytes is unavailable');
        }

        $problems = Cfg::diagnose();
        $checks[] = self::check(
            'configuration',
            $problems === [] ? 'pass' : 'fail',
            $problems === [] ? 'valid' : implode(' ', $problems)
        );

        return $checks;
    }

    private static function checkFunctions(): array
    {
        $required = [
            'hash_hmac', 'hash_equals', 'hash_init', 'random_bytes', 'flock', 'fopen',
            'fwrite', 'fflush', 'rename', 'unlink', 'filemtime', 'filesize', 'mkdir',
            'opendir', 'readdir', 'readfile', 'clearstatcache', 'json_decode',
        ];
        $missing = [];
        foreach ($required as $function) {
            if (!function_exists($function)) {
                $missing[] = $function;
            }
        }
        $checks = [self::check(
            'required_functions',
            $missing === [] ? 'pass' : 'fail',
            $missing === [] ? 'all present' : 'missing: ' . implode(', ', $missing)
        )];

        $optional = ['fsync', 'fastcgi_finish_request'];
        $absent = [];
        foreach ($optional as $function) {
            if (!function_exists($function)) {
                $absent[] = $function;
            }
        }
        $checks[] = self::check(
            'optional_functions',
            'pass',
            $absent === [] ? 'all present' : 'absent, degraded gracefully: ' . implode(', ', $absent)
        );

        return $checks;
    }

    private static function checkStorage(): array
    {
        $checks = [];
        if (!QueuePath::ensureRoot()) {
            return [self::check('queue_root', 'fail', 'QUEUE_ROOT is not writable')];
        }
        $checks[] = self::check('queue_root', 'pass', QueuePath::root());

        $dir = QueuePath::stateDir();
        if ($dir === null) {
            return array_merge($checks, [self::check('atomic_publish', 'fail', 'state directory is not writable')]);
        }

        $probe = $dir . '/selftest-' . Ctx::hex(8);
        $handle = @fopen($probe . '.writing', 'x+b');
        $ok = $handle !== false
            && @fwrite($handle, str_repeat('x', 4096)) === 4096
            && @fflush($handle);
        if ($handle !== false) {
            fclose($handle);
        }
        $ok = $ok && @rename($probe . '.writing', $probe . '.data');
        $ok = $ok && @unlink($probe . '.data');
        @unlink($probe . '.writing');
        $checks[] = self::check(
            'atomic_publish',
            $ok ? 'pass' : 'fail',
            'exclusive create, full write, fflush, rename, unlink'
        );

        $lockFile = @fopen($dir . '/selftest.lock', 'c+b');
        $locked = $lockFile !== false && @flock($lockFile, LOCK_EX | LOCK_NB);
        if ($lockFile !== false) {
            if ($locked) {
                @flock($lockFile, LOCK_UN);
            }
            fclose($lockFile);
        }
        $checks[] = self::check(
            'flock',
            $locked ? 'pass' : 'fail',
            'in process only, a concurrent two worker test is still required'
        );

        $free = @disk_free_space(QueuePath::root());
        $checks[] = self::check(
            'disk_space',
            is_float($free) && $free > MAX_TOTAL_QUEUE_BYTES ? 'pass' : 'warn',
            is_float($free) ? (string) (int) $free . ' bytes free' : 'unknown'
        );

        return $checks;
    }

    private static function checkLimits(): array
    {
        $checks = [];
        $postMax = Cfg::iniBytes('post_max_size');
        $checks[] = self::check(
            'post_max_size',
            $postMax === 0 || MAX_REQUEST_BYTES < $postMax ? 'pass' : 'fail',
            (string) ini_get('post_max_size')
        );
        $memory = Cfg::iniBytes('memory_limit');
        $checks[] = self::check(
            'memory_limit',
            $memory === 0 || $memory >= MAX_RESPONSE_BYTES * 4 ? 'pass' : 'warn',
            (string) ini_get('memory_limit')
        );
        $execution = (int) ini_get('max_execution_time');
        $checks[] = self::check(
            'max_execution_time',
            $execution === 0 || $execution >= 30 ? 'pass' : 'warn',
            (string) $execution
        );
        $disabled = trim((string) ini_get('disable_functions'));
        $checks[] = self::check(
            'disable_functions',
            $disabled === '' ? 'pass' : 'warn',
            $disabled === '' ? 'empty' : $disabled
        );

        return $checks;
    }

    private static function checkExposure(): array
    {
        $checks = [];
        $root = QueuePath::root();
        $documentRoot = str_replace('\\', '/', rtrim((string) ($_SERVER['DOCUMENT_ROOT'] ?? ''), '/\\'));
        $inside = $documentRoot !== '' && str_starts_with($root . '/', $documentRoot . '/');
        $checks[] = self::check(
            'queue_outside_document_root',
            $inside ? (is_file($root . '/.htaccess') ? 'warn' : 'fail') : 'pass',
            $inside
                ? 'QUEUE_ROOT is inside the document root, verify externally that it returns 403 or 404'
                : 'QUEUE_ROOT is outside the document root'
        );

        return $checks;
    }
}

// =============================================================================
// Router
// =============================================================================

final class Router
{
    public static function main(): void
    {
        $action = Http::action();
        Ctx::init($action);

        header_remove('X-Powered-By');
        while (ob_get_level() > 0) {
            @ob_end_clean();
        }
        @ini_set('zlib.output_compression', '0');
        ignore_user_abort(true);
        @set_time_limit(30);

        register_shutdown_function([self::class, 'cleanup']);
        set_exception_handler([self::class, 'onUncaught']);

        try {
            self::dispatch($action);
        } catch (HttpError $error) {
            Http::sendError($error->status, $error->errorCode);
            if ($error->status >= 500 || $error->status === 429) {
                Log::error($error->errorCode, ['status' => $error->status]);
            }
        } catch (Throwable $error) {
            Log::error('unhandled', ['type' => get_class($error)]);
            Http::sendError(500, 'internal_error');
        }

        self::finish();
    }

    private static function dispatch(string $action): void
    {
        $method = Http::method();

        if ($action === '') {
            if ($method !== 'GET' && $method !== 'HEAD') {
                Http::sendMethodNotAllowed('GET, HEAD');

                return;
            }
            self::sendStatusPage();

            return;
        }

        if ($action === 'health') {
            if ($method !== 'GET' && $method !== 'POST' && $method !== 'HEAD') {
                Http::sendMethodNotAllowed('GET, POST, HEAD');

                return;
            }
            http_response_code(200);
            Http::baseHeaders();
            header('Content-Type: application/json; charset=utf-8');
            echo '{"version":"', PHPVPN_VERSION,
                '","status":"OK","serverTime":"', Wire::isoTime(Ctx::$now), '"}';

            return;
        }

        if ($action === 'sync') {
            if ($method !== 'POST') {
                Http::sendMethodNotAllowed('POST');

                return;
            }
            SyncHandler::handle();

            return;
        }

        if ($action === 'selftest') {
            if ($method !== 'POST') {
                Http::sendMethodNotAllowed('POST');

                return;
            }
            SelfTest::handle();

            return;
        }

        Http::sendError(404, 'not_found');
    }

    private static function sendStatusPage(): void
    {
        http_response_code(200);
        Http::baseHeaders();
        header('Content-Type: text/html; charset=utf-8');
        header("Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'");

        if (Http::method() === 'HEAD') {
            return;
        }

        $warning = '';
        if (strlen(SERVER_SECRET) < Cfg::MIN_SECRET_LENGTH) {
            $warning = '<p style="color:#c00;font-weight:bold">SERVER_SECRET is not configured correctly.'
                . ' This is a security risk. The secret must be at least ' . Cfg::MIN_SECRET_LENGTH
                . ' characters long and generated randomly using alphanumeric characters.'
                . ' Please open phpvpn.php in a text editor and update SERVER_SECRET.</p>';
        }

        echo '<!doctype html><html lang="en"><head><meta charset="utf-8">',
            '<meta name="robots" content="noindex,nofollow">',
            '<meta name="viewport" content="width=device-width,initial-scale=1">',
            '<title>phpvpn</title></head>',
            '<body style="font-family:system-ui,sans-serif;margin:3rem auto;max-width:40rem">',
            '<h1>Server is running</h1>', $warning, '</body></html>';
    }

    /** Flushes the response, then spends a bounded slice on global maintenance. */
    private static function finish(): void
    {
        if (function_exists('fastcgi_finish_request')) {
            fastcgi_finish_request();
        } else {
            flush();
        }
        if (QueuePath::ensureRoot()) {
            Sweeper::maybeRun();
        }
    }

    public static function onUncaught(Throwable $error): void
    {
        Log::error('unhandled', ['type' => get_class($error)]);
        Http::sendError(500, 'internal_error');
    }

    /** Removes a half written batch left behind by a fatal error or a timeout. */
    public static function cleanup(): void
    {
        if (Ctx::$writingPath !== null) {
            @unlink(Ctx::$writingPath);
            Ctx::$writingPath = null;
        }
    }
}

// PHPVPN_TEST_MODE lets the test suite require this file and exercise the
// classes directly without dispatching a request.
if (!defined('PHPVPN_TEST_MODE')) {
    Router::main();
}

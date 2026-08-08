<?php

declare(strict_types=1);

/**
 * Builds isolated phpvpn instances for the test suite.
 *
 * Servers are started with -n so that no local php.ini can influence a run.
 * The project targets a typical hosting, so the tests must see PHP defaults
 * such as post_max_size = 8M rather than whatever this machine happens to use.
 *
 * Several independent built-in servers can share one queue root, which is how
 * genuine multi process concurrency is obtained on Windows, where both
 * PHP_CLI_SERVER_WORKERS and PHP_FCGI_CHILDREN are unavailable.
 */
final class Harness
{
    public const SECRET = 'k7Qv2ZpL9wXn4RtY8mB3sD6fH1jA5cE0uI',
        CLIENT_A = '10.99.99.2',
        CLIENT_B = '10.99.99.3',
        CLIENT_C = '10.99.99.4';

    public const TOKENS = [
        self::CLIENT_A => '0202020202020202020202020202020202020202020202020202020202020202',
        self::CLIENT_B => '0303030303030303030303030303030303030303030303030303030303030303',
        self::CLIENT_C => '0404040404040404040404040404040404040404040404040404040404040404',
    ];

    /** Deterministic token for any VPN address, so tests never store a table. */
    public static function tokenFor(string $ip): string
    {
        $octet = (int) substr($ip, (int) strrpos($ip, '.') + 1);

        return str_repeat(sprintf('%02x', $octet), 32);
    }

    /** @return array<string,string> a client table covering 10.99.99.2 upwards */
    public static function clients(int $count): array
    {
        $clients = [];
        for ($octet = 2; $octet < 2 + $count; $octet++) {
            $ip = '10.99.99.' . $octet;
            $clients[$ip] = self::tokenFor($ip);
        }

        return $clients;
    }

    /** @var list<Instance> */
    private static array $live = [];
    private static ?string $tempRoot = null;

    public static function projectRoot(): string
    {
        return str_replace('\\', '/', dirname(__DIR__, 2));
    }

    public static function php(): string
    {
        return PHP_BINARY;
    }

    public static function tempRoot(): string
    {
        if (self::$tempRoot === null) {
            $root = str_replace('\\', '/', sys_get_temp_dir()) . '/phpvpn-tests-' . getmypid();
            if (!is_dir($root) && !mkdir($root, 0700, true) && !is_dir($root)) {
                throw new RuntimeException('Cannot create ' . $root);
            }
            self::$tempRoot = $root;
        }

        return self::$tempRoot;
    }

    /**
     * Renders phpvpn.php with the configuration block rewritten.
     *
     * Overriding through source substitution keeps the shipped file free of any
     * test only hook and proves the configuration block stays easy to edit.
     *
     * @param array<string,string> $overrides constant name => PHP literal
     */
    public static function render(array $overrides): string
    {
        $source = file_get_contents(self::projectRoot() . '/phpvpn.php');
        if ($source === false) {
            throw new RuntimeException('Cannot read phpvpn.php');
        }

        foreach ($overrides as $name => $literal) {
            $pattern = $name === 'ACCESS_TOKENS'
                ? '/^const ACCESS_TOKENS\s*=\s*\[.*?\];$/ms'
                : '/^const ' . preg_quote($name, '/') . '\s*=\s*[^;]*;$/m';
            $replaced = preg_replace($pattern, 'const ' . $name . ' = ' . $literal . ';', $source, 1, $count);
            if ($replaced === null || $count !== 1) {
                throw new RuntimeException('Cannot override constant ' . $name);
            }
            $source = $replaced;
        }

        return $source;
    }

    /** @param array<string,string> $overrides */
    public static function instance(array $overrides = [], int $servers = 1): Instance
    {
        $name = 'i' . count(self::$live) . '-' . bin2hex(random_bytes(4));
        $dir = self::tempRoot() . '/' . $name;
        $docroot = $dir . '/www';
        $queue = $dir . '/queue';
        foreach ([$dir, $docroot, $queue] as $path) {
            if (!mkdir($path, 0700, true) && !is_dir($path)) {
                throw new RuntimeException('Cannot create ' . $path);
            }
        }

        // {{DOCROOT}} lets a test place the queue inside the web root on
        // purpose, which is the misconfiguration the preflight must catch.
        foreach ($overrides as $name => $literal) {
            $overrides[$name] = str_replace('{{DOCROOT}}', $docroot, $literal);
        }

        $config = array_merge([
            'SERVER_SECRET' => var_export(self::SECRET, true),
            'QUEUE_ROOT' => var_export($queue, true),
            'REQUIRE_HTTPS' => 'false',
            'ACCESS_TOKENS' => self::tokenLiteral(self::TOKENS),
        ], $overrides);

        file_put_contents($docroot . '/phpvpn.php', self::render($config));

        $instance = new Instance($dir, $docroot, $queue);
        for ($i = 0; $i < $servers; $i++) {
            $instance->startServer();
        }
        self::$live[] = $instance;

        return $instance;
    }

    /** @param array<string,string> $tokens */
    public static function tokenLiteral(array $tokens): string
    {
        $parts = [];
        foreach ($tokens as $ip => $token) {
            $parts[] = "\n    " . var_export($ip, true) . ' => ' . var_export($token, true) . ',';
        }

        return '[' . implode('', $parts) . "\n]";
    }

    public static function shutdown(): void
    {
        foreach (self::$live as $instance) {
            $instance->stop();
        }
        self::$live = [];
    }

    public static function removeTree(string $path): void
    {
        if (!is_dir($path)) {
            @unlink($path);

            return;
        }
        $entries = @scandir($path);
        if ($entries !== false) {
            foreach ($entries as $entry) {
                if ($entry !== '.' && $entry !== '..') {
                    self::removeTree($path . '/' . $entry);
                }
            }
        }
        @rmdir($path);
    }
}

/** One rendered phpvpn deployment plus the servers exposing it. */
final class Instance
{
    /** @var list<array{process:resource,port:int}> */
    private array $servers = [];

    public function __construct(
        public readonly string $dir,
        public readonly string $docroot,
        public readonly string $queueRoot
    ) {
    }

    public function startServer(): string
    {
        $port = self::freePort();
        $command = [
            Harness::php(),
            '-n',
            '-d', 'error_reporting=E_ALL',
            '-d', 'display_errors=0',
            '-d', 'log_errors=1',
            '-d', 'error_log=' . $this->dir . '/php-error.log',
            '-S', '127.0.0.1:' . $port,
            '-t', $this->docroot,
        ];
        $descriptors = [
            0 => ['pipe', 'r'],
            1 => ['file', $this->dir . '/server-out.log', 'a'],
            2 => ['file', $this->dir . '/server-err.log', 'a'],
        ];
        $process = proc_open($command, $descriptors, $pipes, $this->docroot);
        if (!is_resource($process)) {
            throw new RuntimeException('Cannot start the built-in server');
        }
        fclose($pipes[0]);

        $deadline = microtime(true) + 10.0;
        for (;;) {
            $probe = @fsockopen('127.0.0.1', $port, $code, $message, 0.25);
            if (is_resource($probe)) {
                fclose($probe);
                break;
            }
            if (microtime(true) > $deadline) {
                throw new RuntimeException('The built-in server did not come up on port ' . $port);
            }
            usleep(50000);
        }

        $this->servers[] = ['process' => $process, 'port' => $port];

        return $this->url(count($this->servers) - 1);
    }

    public function url(int $server = 0): string
    {
        return 'http://127.0.0.1:' . $this->servers[$server]['port'] . '/phpvpn.php';
    }

    public function serverCount(): int
    {
        return count($this->servers);
    }

    public function queueDir(string $clientIp): string
    {
        $octet = (int) substr($clientIp, strrpos($clientIp, '.') + 1);

        return $this->queueRoot . '/10_99_99_0/' . sprintf('%03d', $octet);
    }

    /** @return list<string> file names in one client queue, sorted */
    public function queueFiles(string $clientIp, string $suffix = ''): array
    {
        $dir = $this->queueDir($clientIp);
        if (!is_dir($dir)) {
            return [];
        }
        $found = [];
        foreach (scandir($dir) ?: [] as $entry) {
            if ($entry === '.' || $entry === '..' || $entry === 'queue.lock' || $entry === 'queue.stat') {
                continue;
            }
            if ($suffix === '' || str_ends_with($entry, $suffix)) {
                $found[] = $entry;
            }
        }
        sort($found);

        return $found;
    }

    /**
     * Ages every claimed batch of one client and forces the next sync to run
     * maintenance, which simulates a client that never sent its ack.
     */
    public function expireClaims(string $clientIp, int $secondsAgo): int
    {
        $dir = $this->queueDir($clientIp);
        $aged = 0;
        foreach ($this->queueFiles($clientIp, '.sending') as $entry) {
            touch($dir . '/' . $entry, time() - $secondsAgo);
            $aged++;
        }
        @unlink($dir . '/queue.stat'); // A missing stat always triggers a sweep.

        return $aged;
    }

    public function phpErrorLog(): string
    {
        $path = $this->dir . '/php-error.log';

        return is_file($path) ? (string) file_get_contents($path) : '';
    }

    public function stop(): void
    {
        foreach ($this->servers as $server) {
            if (is_resource($server['process'])) {
                proc_terminate($server['process']);
                proc_close($server['process']);
            }
        }
        $this->servers = [];
    }

    private static function freePort(): int
    {
        $socket = @stream_socket_server('tcp://127.0.0.1:0', $code, $message);
        if ($socket === false) {
            throw new RuntimeException('Cannot reserve a port: ' . $message);
        }
        $name = stream_socket_get_name($socket, false);
        fclose($socket);

        return (int) substr((string) $name, strrpos((string) $name, ':') + 1);
    }
}

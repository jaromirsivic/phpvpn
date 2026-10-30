<?php
/** Isolated constant-boundary tests. Run: php server/php/tests/tests-info-pretty-boundaries.php */
declare(strict_types=1);

if (PHP_SAPI !== 'cli') { http_response_code(404); exit; }

function check(bool $condition, string $message): void {
    if (!$condition) throw new RuntimeException($message);
}

if (($argv[1] ?? '') === '--child') {
    // Evaluate the current production implementation in a fresh process. Only
    // constants change, never the builder, its size loop or the crypto path.
    $limit = (int) $argv[2];
    $version = match ($argv[3]) { 'large' => str_repeat('x', 1500), 'invalid' => "\xff", default => '1.0.0' };
    $source = file_get_contents(dirname(__DIR__) . '/phpvpn.php');
    $source = preg_replace('/const MAX_INFO_PLAINTEXT_BYTES = 262144;/', 'const MAX_INFO_PLAINTEXT_BYTES = ' . $limit . ';', $source, 1, $count);
    check($count === 1, 'production limit declaration changed');
    $source = preg_replace('/const SERVER_VERSION = [^;]+;/', 'const SERVER_VERSION = ' . var_export($version, true) . ';', $source, 1, $count);
    check($count === 1, 'production version declaration changed');
    define('PHPVPN_NO_AUTORUN', true);
    eval(substr($source, 5));
    $directory = sys_get_temp_dir() . '/phpvpn-pretty-' . bin2hex(random_bytes(8));
    $config = \PhpVpn\Config::fromConstants()->withBufferDirectory($directory);
    $storage = new \PhpVpn\DiskStorage($directory);
    $now = 1789300800000;
    try {
        foreach ($config->clientAddresses() as $address) {
            $contracts = $address === '10.99.99.2' ? [] : [['peer' => '10.99.99.2', 'hash' => str_repeat('a',64)]];
            check($storage->publishMaskedContracts($address, $contracts, $now), 'fixture publication');
        }
        $handler = new \PhpVpn\InfoHandler($config, new \PhpVpn\Clock($now), $storage);
        $method = new ReflectionMethod($handler, 'buildPlaintext');
        $plain = $method->invoke($handler, $argv[4] === 'extended' ? '10.99.99.2' : null);
        echo json_encode($plain === null ? null : base64_encode($plain));
    } finally {
        if (is_dir($directory)) {
            $files = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($directory, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
            foreach ($files as $file) { $file->isDir() ? rmdir($file->getPathname()) : unlink($file->getPathname()); }
            rmdir($directory);
        }
    }
    exit;
}

function runChild(int $limit, string $version, string $mode): ?string {
    $process = proc_open([PHP_BINARY, __FILE__, '--child', (string)$limit, $version, $mode],
        [1 => ['pipe','w'], 2 => ['pipe','w']], $pipes);
    check(is_resource($process), 'start isolated PHP');
    $output = stream_get_contents($pipes[1]); $error = stream_get_contents($pipes[2]);
    fclose($pipes[1]); fclose($pipes[2]);
    check(proc_close($process) === 0, 'child failed: ' . $error);
    $value = json_decode($output, true, 512, JSON_THROW_ON_ERROR);
    return $value === null ? null : base64_decode($value, true);
}

foreach (['standard','extended'] as $mode) {
    $full = runChild(262144, 'normal', $mode);
    check($full !== null && str_starts_with($full, "{\n    \"serverVersion\":"), 'pretty plaintext');
    $model = json_decode($full, true, 512, JSON_THROW_ON_ERROR);
    check(array_keys($model) === ['serverVersion','generatedAt','clients','limits'], 'member order');
    check(count($model['clients']) === 14, 'complete unbounded fixture');
    check(json_decode(json_encode($model, JSON_UNESCAPED_SLASHES), true) === $model, 'compact semantics and order');
    check($full === json_encode($model, JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT), 'standard PHP whitespace');
    foreach ([strlen($full), strlen($full)-1, 1024] as $limit) {
        $bounded = runChild($limit, 'normal', $mode);
        check($bounded !== null && strlen($bounded) <= $limit, 'exact serialized byte bound');
        $kept = json_decode($bounded, true, 512, JSON_THROW_ON_ERROR);
        $expected = $model;
        while (strlen(json_encode($expected, JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT)) > $limit) array_pop($expected['clients']);
        check($kept === $expected, 'whole deterministic last-client trimming');
        check($bounded === runChild($limit, 'normal', $mode), 'frozen-clock determinism');
        if ($limit === strlen($full)) check($bounded === $full, 'exact limit accepted');
        if ($limit === strlen($full)-1) check(count($kept['clients']) === 13, 'one-byte excess drops one whole last client');
        echo "PASS $mode limit=$limit bytes=" . strlen($bounded) . ' clients=' . count($kept['clients']) . "\n";
    }
    check(runChild(1024, 'large', $mode) === null, 'oversized base fails closed');
    check(runChild(262144, 'invalid', $mode) === null, 'serialization failure fails closed');
}
echo "ALL PRETTY BOUNDARY TESTS PASSED\n";

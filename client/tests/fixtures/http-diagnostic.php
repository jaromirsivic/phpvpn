<?php
declare(strict_types=1);

// Opt-in loopback fixture. A temporary endpoint defines the two absolute paths.
// It uses the real health signer, an isolated public test secret, and no storage.
if (($_SERVER['REMOTE_ADDR'] ?? '') !== '127.0.0.1' ||
    !defined('PHPVPN_DIAGNOSTIC_LIBRARY') || !defined('PHPVPN_DIAGNOSTIC_CONTROL')) {
    http_response_code(404);
    exit;
}
define('PHPVPN_NO_AUTORUN', true);
require PHPVPN_DIAGNOSTIC_LIBRARY;
$mode = trim((string) file_get_contents(PHPVPN_DIAGNOSTIC_CONTROL));
$request = \PhpVpn\Request::fromGlobals();
$writer = new \PhpVpn\BufferedResponseWriter();
if ($request->action === 'health') {
    $config = \PhpVpn\Config::fromConstants()->withSecret('runtime-fixture-secret');
    (new \PhpVpn\HealthHandler($config, new \PhpVpn\Clock()))->handle($request, $writer);
}
$body = $writer->body;
if ($mode === 'health' || ($mode === 'info' && $request->action === 'info')) {
    $body = '<script>fixture</script>head' . $body . '<script>fixture</script>tail';
} elseif ($mode === 'single') {
    $body[0] = '!';
} elseif (str_starts_with($mode, 'body')) {
    $body = str_repeat('x', (int) substr($mode, 4));
} elseif ($mode === 'empty') {
    $body = '';
}
header('Content-Type: application/json');
header('Cache-Control: no-store');
header('Content-Length: ' . strlen($body));
header('X-Diagnostic-Duplicate: first', false);
header('X-Diagnostic-Duplicate: second', false);
echo $body;

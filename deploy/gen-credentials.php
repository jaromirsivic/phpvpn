<?php

/**
 * Generates a ready to paste credentials block for phpvpn.php.
 *
 * Usage:
 *   php deploy/gen-credentials.php            one client, 10.99.99.2
 *   php deploy/gen-credentials.php 12         twelve clients, 10.99.99.2 upwards
 *   php deploy/gen-credentials.php 12 --json  the same, as JSON for a provisioning script
 *
 * Every value comes from random_bytes. Never reuse a token between clients and
 * never commit the generated block to a public repository.
 */

declare(strict_types=1);

if (PHP_SAPI !== 'cli') {
    http_response_code(404);
    exit;
}

$count = 1;
$asJson = false;
foreach (array_slice($argv, 1) as $argument) {
    if ($argument === '--json') {
        $asJson = true;
        continue;
    }
    if (ctype_digit($argument) && (int) $argument >= 1 && (int) $argument <= 253) {
        $count = (int) $argument;
        continue;
    }
    exit("Unknown argument: {$argument}\nUsage: php deploy/gen-credentials.php [1-253] [--json]\n");
}

$alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
$secret = '';
for ($i = 0; $i < 48; $i++) {
    $secret .= $alphabet[random_int(0, strlen($alphabet) - 1)];
}

$tokens = [];
for ($octet = 2; $octet < 2 + $count; $octet++) {
    $tokens['10.99.99.' . $octet] = bin2hex(random_bytes(32));
}

if ($asJson) {
    echo json_encode(
        ['serverSecret' => $secret, 'accessTokens' => $tokens],
        JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES
    ), "\n";
    exit(0);
}

echo "Paste this into the configuration block of phpvpn.php:\n\n";
echo "const SERVER_SECRET = '", $secret, "';\n\n";
echo "const ACCESS_TOKENS = [\n";
foreach ($tokens as $ip => $token) {
    printf("    '%s' => '%s',\n", $ip, $token);
}
echo "];\n\n";
echo "Give each client only its own address and token. The token itself is never\n";
echo "sent over the network, it is only used to derive the request signature.\n";

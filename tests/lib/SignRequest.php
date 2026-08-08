<?php

declare(strict_types=1);

/**
 * Reference implementation of the phpvpn request signature.
 *
 * This is intentionally written from PROTOCOL.md rather than by reusing server
 * code, so a test failure means the two really disagree.
 */
final class SignRequest
{
    /**
     * Builds a complete NDJSON body with a valid auth line in front.
     *
     * @param list<string> $lines compact JSON records without their terminator
     * @param array{timestamp?:string,nonce?:string,client?:string,bodyOverride?:string} $options
     */
    public static function body(
        string $client,
        string $token,
        string $secret,
        string $action,
        array $lines = [],
        array $options = []
    ): string {
        $rest = $lines === [] ? '' : implode("\n", $lines) . "\n";
        if (isset($options['bodyOverride'])) {
            $rest = $options['bodyOverride'];
        }

        $timestamp = $options['timestamp'] ?? gmdate('Y-m-d\TH:i:s\Z');
        $nonce = $options['nonce'] ?? rtrim(strtr(base64_encode(random_bytes(18)), '+/', '-_'), '=');
        $signedClient = $options['client'] ?? $client;

        $signature = self::signature($signedClient, $token, $secret, $action, $timestamp, $nonce, $rest);

        $auth = json_encode([
            'type' => 'auth',
            'client' => $signedClient,
            'timestamp' => $timestamp,
            'nonce' => $nonce,
            'signature' => $signature,
        ], JSON_UNESCAPED_SLASHES);

        return $auth . "\n" . $rest;
    }

    public static function signature(
        string $client,
        string $token,
        string $secret,
        string $action,
        string $timestamp,
        string $nonce,
        string $bodyAfterAuth
    ): string {
        $derivedKey = hash_hmac('sha256', $client . "\n" . $token, $secret, true);
        $canonical = "POST\n"
            . $action . "\n"
            . $client . "\n"
            . $timestamp . "\n"
            . $nonce . "\n"
            . hash('sha256', $bodyAfterAuth);

        return hash_hmac('sha256', $canonical, $derivedKey);
    }

    /** One compact data record. */
    public static function packet(string $src, string $dst, string $payload, ?string $uuid = null): string
    {
        return json_encode([
            'type' => 'data',
            'uuid' => $uuid ?? self::uuid(),
            'src' => $src,
            'dst' => $dst,
            'data' => base64_encode($payload),
        ], JSON_UNESCAPED_SLASHES);
    }

    public static function ack(string $batchId): string
    {
        return '{"type":"ack","batchId":"' . $batchId . '"}';
    }

    public static function uuid(): string
    {
        $bytes = random_bytes(16);
        $bytes[6] = chr((ord($bytes[6]) & 0x0f) | 0x40);
        $bytes[8] = chr((ord($bytes[8]) & 0x3f) | 0x80);
        $hex = bin2hex($bytes);

        return substr($hex, 0, 8) . '-' . substr($hex, 8, 4) . '-' . substr($hex, 12, 4)
            . '-' . substr($hex, 16, 4) . '-' . substr($hex, 20, 12);
    }

    /** Splits an NDJSON response into decoded records. */
    public static function parseNdjson(string $body): array
    {
        $records = [];
        foreach (explode("\n", trim($body)) as $line) {
            if ($line === '') {
                continue;
            }
            $decoded = json_decode($line, true);
            if (is_array($decoded)) {
                $records[] = $decoded;
            }
        }

        return $records;
    }

    /** The trailing sync-result record, or null when the response has none. */
    public static function syncResult(string $body): ?array
    {
        $records = self::parseNdjson($body);
        $last = end($records);

        return is_array($last) && ($last['type'] ?? '') === 'sync-result' ? $last : null;
    }

    /** @return list<array> only the delivery records */
    public static function deliveries(string $body): array
    {
        $deliveries = [];
        foreach (self::parseNdjson($body) as $record) {
            if (($record['type'] ?? '') === 'delivery') {
                $deliveries[] = $record;
            }
        }

        return $deliveries;
    }
}

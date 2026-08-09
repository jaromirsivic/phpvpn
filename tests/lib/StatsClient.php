<?php

declare(strict_types=1);

/**
 * Reference client for the stats action.
 *
 * Written from PROTOCOL.md and deliberately implemented the other way round
 * than the server: the key comes from the built in hash_hkdf(), AES runs on a
 * textbook byte oriented state with an explicit MixColumns and GHASH multiplies
 * bit by bit. The local PHP has no OpenSSL, so an agreement between these two
 * independently written implementations is the only evidence available that
 * either of them is right.
 *
 * Speed is irrelevant here, clarity is not.
 */
final class StatsClient
{
    public const INFO = 'phpvpn/stats-response/v1';

    private const SBOX_HEX =
        '637c777bf26b6fc53001672bfed7ab76'
        . 'ca82c97dfa5947f0add4a2af9ca472c0'
        . 'b7fd9326363ff7cc34a5e5f171d83115'
        . '04c723c31896059a071280e2eb27b275'
        . '09832c1a1b6e5aa0523bd6b329e32f84'
        . '53d100ed20fcb15b6acbbe394a4c58cf'
        . 'd0efaafb434d338545f9027f503c9fa8'
        . '51a3408f929d38f5bcb6da2110fff3d2'
        . 'cd0c13ec5f974417c4a77e3d645d1973'
        . '60814fdc222a908846eeb814de5e0bdb'
        . 'e0323a0a4906245cc2d3ac629195e479'
        . 'e7c8376d8dd54ea96c56f4ea657aae08'
        . 'ba78252e1ca6b4c6e8dd741f4bbd8b8a'
        . '703eb5664803f60e613557b986c11d9e'
        . 'e1f8981169d98e949b1e87e9ce5528df'
        . '8ca1890dbfe6426841992d0fb054bb16';

    /**
     * Complete signed request body, one NDJSON line.
     *
     * @param array{timestamp?:string,nonce?:string,signature?:string,action?:string,extra?:string} $options
     */
    public static function body(string $secret, array $options = []): string
    {
        $timestamp = $options['timestamp'] ?? gmdate('Y-m-d\TH:i:s\Z');
        $nonce = $options['nonce'] ?? rtrim(strtr(base64_encode(random_bytes(18)), '+/', '-_'), '=');
        $signature = $options['signature'] ?? hash_hmac(
            'sha256',
            self::canonical($timestamp, $nonce, $options['action'] ?? 'stats'),
            $secret
        );

        $line = json_encode([
            'type' => 'stats-auth',
            'timestamp' => $timestamp,
            'nonce' => $nonce,
            'signature' => $signature,
        ], JSON_UNESCAPED_SLASHES);

        return $line . "\n" . ($options['extra'] ?? '');
    }

    public static function canonical(string $timestamp, string $nonce, string $action = 'stats'): string
    {
        return "POST\n" . $action . "\n" . $timestamp . "\n" . $nonce;
    }

    /** The canonical request of a body produced by body(). */
    public static function canonicalOf(string $body): string
    {
        $first = strstr($body, "\n", true);
        $record = (array) json_decode($first === false ? $body : $first, true);

        return self::canonical((string) $record['timestamp'], (string) $record['nonce']);
    }

    /** HKDF-SHA-256 through the always available core implementation. */
    public static function deriveKey(string $secret, string $canonical): string
    {
        return hash_hkdf('sha256', $secret, 32, self::INFO, hash('sha256', $canonical, true));
    }

    /**
     * Decodes and decrypts a stats response.
     *
     * @return ?string null whenever the response is malformed or the tag fails
     */
    public static function open(string $responseBody, string $secret, string $canonical): ?string
    {
        $decoded = json_decode($responseBody, true);
        if (!is_array($decoded) || ($decoded['alg'] ?? '') !== 'AES-256-GCM') {
            return null;
        }
        foreach (['iv', 'ciphertext', 'tag'] as $field) {
            if (!isset($decoded[$field]) || !is_string($decoded[$field])) {
                return null;
            }
        }
        $iv = base64_decode($decoded['iv'], true);
        $ciphertext = base64_decode($decoded['ciphertext'], true);
        $tag = base64_decode($decoded['tag'], true);
        if ($iv === false || $ciphertext === false || $tag === false) {
            return null;
        }

        return self::decrypt(
            $ciphertext,
            self::deriveKey($secret, $canonical),
            $iv,
            $tag,
            $canonical
        );
    }

    /** @return ?string null when the authentication tag does not match */
    public static function decrypt(
        string $ciphertext,
        string $key,
        string $iv,
        string $tag,
        string $aad
    ): ?string {
        if (strlen($key) !== 32 || strlen($iv) !== 12 || strlen($tag) !== 16) {
            return null;
        }
        $schedule = self::expandKey($key);
        $subkey = self::encryptBlock(str_repeat("\0", 16), $schedule);
        $preCounter = $iv . "\0\0\0\1";

        $checksum = self::ghash(
            $subkey,
            self::pad($aad) . self::pad($ciphertext)
            . self::lengthBlock(strlen($aad), strlen($ciphertext))
        );
        if (!hash_equals($checksum ^ self::encryptBlock($preCounter, $schedule), $tag)) {
            // Nothing decrypted is returned when the tag is wrong.
            return null;
        }

        $counter = $preCounter;
        $plaintext = '';
        for ($offset = 0; $offset < strlen($ciphertext); $offset += 16) {
            $counter = self::increment($counter);
            $keystream = self::encryptBlock($counter, $schedule);
            $chunk = substr($ciphertext, $offset, 16);
            $plaintext .= $chunk ^ substr($keystream, 0, strlen($chunk));
        }

        return $plaintext;
    }

    /** @return list<string> fifteen round keys of sixteen bytes each */
    private static function expandKey(string $key): array
    {
        $sbox = (string) hex2bin(self::SBOX_HEX);
        $words = [];
        for ($i = 0; $i < 8; $i++) {
            $words[$i] = substr($key, $i * 4, 4);
        }

        $rcon = 1;
        for ($i = 8; $i < 60; $i++) {
            $word = $words[$i - 1];
            if ($i % 8 === 0) {
                $word = self::substitute($sbox, $word[1] . $word[2] . $word[3] . $word[0]);
                $word[0] = chr(ord($word[0]) ^ $rcon);
                $rcon = self::xtime($rcon);
            } elseif ($i % 8 === 4) {
                $word = self::substitute($sbox, $word);
            }
            $words[$i] = $words[$i - 8] ^ $word;
        }

        $schedule = [];
        for ($round = 0; $round <= 14; $round++) {
            $schedule[] = $words[$round * 4] . $words[$round * 4 + 1]
                . $words[$round * 4 + 2] . $words[$round * 4 + 3];
        }

        return $schedule;
    }

    private static function substitute(string $sbox, string $word): string
    {
        return $sbox[ord($word[0])] . $sbox[ord($word[1])]
            . $sbox[ord($word[2])] . $sbox[ord($word[3])];
    }

    /** @param list<string> $schedule */
    private static function encryptBlock(string $block, array $schedule): string
    {
        $sbox = (string) hex2bin(self::SBOX_HEX);
        // The state is column major: index = column * 4 + row.
        $state = array_values((array) unpack('C16', $block ^ $schedule[0]));

        for ($round = 1; $round <= 14; $round++) {
            foreach ($state as $index => $byte) {
                $state[$index] = ord($sbox[$byte]);
            }

            $shifted = [];
            for ($column = 0; $column < 4; $column++) {
                for ($row = 0; $row < 4; $row++) {
                    $shifted[$column * 4 + $row] = $state[((($column + $row) % 4) * 4) + $row];
                }
            }
            $state = $shifted;

            if ($round < 14) {
                for ($column = 0; $column < 4; $column++) {
                    $a = array_slice($state, $column * 4, 4);
                    $state[$column * 4] = self::xtime($a[0]) ^ self::xtime($a[1]) ^ $a[1] ^ $a[2] ^ $a[3];
                    $state[$column * 4 + 1] = $a[0] ^ self::xtime($a[1]) ^ self::xtime($a[2]) ^ $a[2] ^ $a[3];
                    $state[$column * 4 + 2] = $a[0] ^ $a[1] ^ self::xtime($a[2]) ^ self::xtime($a[3]) ^ $a[3];
                    $state[$column * 4 + 3] = self::xtime($a[0]) ^ $a[0] ^ $a[1] ^ $a[2] ^ self::xtime($a[3]);
                }
            }

            $key = array_values((array) unpack('C16', $schedule[$round]));
            foreach ($key as $index => $byte) {
                $state[$index] ^= $byte;
            }
        }

        $out = '';
        foreach ($state as $byte) {
            $out .= chr($byte & 0xff);
        }

        return $out;
    }

    private static function xtime(int $byte): int
    {
        return (($byte << 1) & 0xff) ^ (($byte & 0x80) !== 0 ? 0x1b : 0);
    }

    private static function ghash(string $subkey, string $data): string
    {
        $h = array_values((array) unpack('N4', $subkey));
        $y = [0, 0, 0, 0];
        for ($offset = 0; $offset < strlen($data); $offset += 16) {
            $block = array_values((array) unpack('N4', substr($data, $offset, 16)));
            $y = self::multiply([
                $y[0] ^ $block[0],
                $y[1] ^ $block[1],
                $y[2] ^ $block[2],
                $y[3] ^ $block[3],
            ], $h);
        }

        return pack('N4', $y[0], $y[1], $y[2], $y[3]);
    }

    /**
     * Multiplication in GF(2^128), one bit per iteration.
     *
     * @param array<int,int> $x
     * @param array<int,int> $y
     * @return array<int,int>
     */
    private static function multiply(array $x, array $y): array
    {
        $z = [0, 0, 0, 0];
        $v = $y;
        for ($bit = 0; $bit < 128; $bit++) {
            if ((($x[$bit >> 5] >> (31 - ($bit & 31))) & 1) === 1) {
                $z = [$z[0] ^ $v[0], $z[1] ^ $v[1], $z[2] ^ $v[2], $z[3] ^ $v[3]];
            }
            $carry = $v[3] & 1;
            $v[3] = (($v[3] >> 1) | (($v[2] & 1) << 31)) & 0xFFFFFFFF;
            $v[2] = (($v[2] >> 1) | (($v[1] & 1) << 31)) & 0xFFFFFFFF;
            $v[1] = (($v[1] >> 1) | (($v[0] & 1) << 31)) & 0xFFFFFFFF;
            $v[0] = ($v[0] >> 1) ^ ($carry === 1 ? 0xe1000000 : 0);
        }

        return $z;
    }

    private static function lengthBlock(int $aadBytes, int $textBytes): string
    {
        return pack('N4', 0, $aadBytes * 8, 0, $textBytes * 8);
    }

    private static function pad(string $value): string
    {
        $remainder = strlen($value) % 16;

        return $remainder === 0 ? $value : $value . str_repeat("\0", 16 - $remainder);
    }

    private static function increment(string $block): string
    {
        $counter = (int) ((array) unpack('N', substr($block, 12, 4)))[1];

        return substr($block, 0, 12) . pack('N', ($counter + 1) & 0xFFFFFFFF);
    }
}

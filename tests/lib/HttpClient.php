<?php

declare(strict_types=1);

/**
 * Tiny HTTP/1.1 client built on stream_socket_client.
 *
 * The project must run without cURL, so the test suite deliberately does not
 * use it either. Requests can be issued in parallel, which is what makes the
 * concurrency tests meaningful.
 */
final class HttpClient
{
    /**
     * @param array<string,string> $headers
     * @return array{status:int,headers:array<string,string>,body:string}
     */
    public static function request(
        string $url,
        string $method = 'GET',
        array $headers = [],
        string $body = '',
        float $timeout = 15.0
    ): array {
        $responses = self::parallel([
            ['url' => $url, 'method' => $method, 'headers' => $headers, 'body' => $body],
        ], $timeout);

        return $responses[0];
    }

    /**
     * Sends the request head in one write and then the body in tiny TCP writes.
     *
     * phpvpn parses the body as a stream, so record boundaries must be
     * irrelevant to how the network happens to split those bytes. The head is
     * deliberately not fragmented: parsing it is the webserver's job, and the
     * built-in development server rejects a head split into small reads with
     * "Malformed HTTP request". Apache, nginx and PHP-FPM do not.
     *
     * @param array<string,string> $headers
     * @return array{status:int,headers:array<string,string>,body:string}
     */
    public static function slowRequest(
        string $url,
        string $method,
        array $headers,
        string $body,
        int $chunkSize = 7,
        float $timeout = 20.0
    ): array {
        $connection = self::open(['url' => $url, 'method' => $method, 'headers' => $headers, 'body' => $body]);
        $socket = $connection['socket'];
        $out = $connection['out'];
        $total = strlen($out);
        $headEnd = strpos($out, "\r\n\r\n");
        $headEnd = $headEnd === false ? $total : $headEnd + 4;
        $sent = 0;
        $raw = '';
        $deadline = microtime(true) + $timeout;

        // Reading while still writing matters. If the server answers early, for
        // example with 413 before it touches the body, a client that only
        // writes gets its connection reset instead of the real response.
        while (microtime(true) < $deadline) {
            $read = [$socket];
            $write = $sent < $total ? [$socket] : [];
            $except = null;
            if (@stream_select($read, $write, $except, 0, 50000) === false) {
                break;
            }

            if ($write !== []) {
                $limit = $sent < $headEnd ? $headEnd - $sent : $chunkSize;
                $written = @fwrite($socket, substr($out, $sent, $limit));
                if ($written === false) {
                    break;
                }
                $sent += $written;
            }
            if ($read !== []) {
                $chunk = @fread($socket, 65536);
                if ($chunk === false) {
                    break;
                }
                $raw .= $chunk;
                if (feof($socket)) {
                    break;
                }
            }
        }
        fclose($socket);

        return self::parse($raw);
    }

    /**
     * @param list<array{url:string,method?:string,headers?:array<string,string>,body?:string}> $requests
     * @return list<array{status:int,headers:array<string,string>,body:string}>
     */
    public static function parallel(array $requests, float $timeout = 15.0): array
    {
        $connections = [];
        foreach ($requests as $index => $request) {
            $connections[$index] = self::open($request);
        }

        $deadline = microtime(true) + $timeout;
        while (self::pending($connections)) {
            if (microtime(true) > $deadline) {
                throw new RuntimeException('HTTP timeout after ' . $timeout . 's');
            }

            $read = [];
            $write = [];
            foreach ($connections as $index => $connection) {
                if ($connection['state'] === 'writing') {
                    $write[$index] = $connection['socket'];
                } elseif ($connection['state'] === 'reading') {
                    $read[$index] = $connection['socket'];
                }
            }
            $except = null;
            if ($read === [] && $write === []) {
                break;
            }
            $readCopy = $read;
            $writeCopy = $write;
            if (@stream_select($readCopy, $writeCopy, $except, 0, 200000) === false) {
                continue;
            }

            foreach ($writeCopy as $index => $socket) {
                self::pump($connections[$index]);
            }
            foreach ($readCopy as $index => $socket) {
                self::receive($connections[$index]);
            }
        }

        $responses = [];
        foreach ($connections as $index => $connection) {
            fclose($connection['socket']);
            $responses[$index] = self::parse($connection['raw']);
        }
        ksort($responses);

        return array_values($responses);
    }

    private static function open(array $request): array
    {
        $url = parse_url($request['url']);
        if (!is_array($url) || !isset($url['host'])) {
            throw new RuntimeException('Invalid URL: ' . $request['url']);
        }
        $host = $url['host'];
        $port = $url['port'] ?? 80;
        $path = ($url['path'] ?? '/') . (isset($url['query']) ? '?' . $url['query'] : '');

        $socket = @stream_socket_client(
            'tcp://' . $host . ':' . $port,
            $errorCode,
            $errorMessage,
            5.0,
            STREAM_CLIENT_CONNECT
        );
        if ($socket === false) {
            throw new RuntimeException('Cannot connect to ' . $host . ':' . $port . ' - ' . $errorMessage);
        }
        stream_set_blocking($socket, false);

        $body = $request['body'] ?? '';
        $headers = $request['headers'] ?? [];
        $headers['Host'] = $host . ':' . $port;
        $headers['Connection'] = 'close';
        $headers['Content-Length'] = (string) strlen($body);

        $raw = strtoupper($request['method'] ?? 'GET') . ' ' . $path . " HTTP/1.1\r\n";
        foreach ($headers as $name => $value) {
            $raw .= $name . ': ' . $value . "\r\n";
        }
        $raw .= "\r\n" . $body;

        return ['socket' => $socket, 'out' => $raw, 'sent' => 0, 'raw' => '', 'state' => 'writing'];
    }

    private static function pending(array $connections): bool
    {
        foreach ($connections as $connection) {
            if ($connection['state'] !== 'done') {
                return true;
            }
        }

        return false;
    }

    private static function pump(array &$connection): void
    {
        $chunk = substr($connection['out'], $connection['sent']);
        $written = @fwrite($connection['socket'], $chunk);
        if ($written === false) {
            $connection['state'] = 'done';

            return;
        }
        $connection['sent'] += $written;
        if ($connection['sent'] >= strlen($connection['out'])) {
            $connection['state'] = 'reading';
        }
    }

    private static function receive(array &$connection): void
    {
        $chunk = @fread($connection['socket'], 65536);
        if ($chunk === false || ($chunk === '' && feof($connection['socket']))) {
            $connection['state'] = 'done';

            return;
        }
        $connection['raw'] .= $chunk;
        if (feof($connection['socket'])) {
            $connection['state'] = 'done';
        }
    }

    /** @return array{status:int,headers:array<string,string>,body:string} */
    private static function parse(string $raw): array
    {
        $split = strpos($raw, "\r\n\r\n");
        if ($split === false) {
            return ['status' => 0, 'headers' => [], 'body' => $raw];
        }
        $head = substr($raw, 0, $split);
        $body = substr($raw, $split + 4);

        $lines = explode("\r\n", $head);
        $statusLine = array_shift($lines) ?? '';
        $status = 0;
        if (preg_match('#^HTTP/\d\.\d (\d{3})#', $statusLine, $match) === 1) {
            $status = (int) $match[1];
        }

        $headers = [];
        foreach ($lines as $line) {
            $colon = strpos($line, ':');
            if ($colon !== false) {
                $headers[strtolower(substr($line, 0, $colon))] = trim(substr($line, $colon + 1));
            }
        }

        if (strtolower($headers['transfer-encoding'] ?? '') === 'chunked') {
            $body = self::dechunk($body);
        }

        return ['status' => $status, 'headers' => $headers, 'body' => $body];
    }

    private static function dechunk(string $body): string
    {
        $decoded = '';
        $offset = 0;
        $length = strlen($body);
        while ($offset < $length) {
            $newline = strpos($body, "\r\n", $offset);
            if ($newline === false) {
                break;
            }
            $size = hexdec(trim(substr($body, $offset, $newline - $offset)));
            if ($size === 0) {
                break;
            }
            $decoded .= substr($body, $newline + 2, (int) $size);
            $offset = $newline + 2 + (int) $size + 2;
        }

        return $decoded;
    }
}

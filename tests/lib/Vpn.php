<?php

declare(strict_types=1);

/** Convenience wrappers that speak the phpvpn protocol over HTTP. */
final class Vpn
{
    public const NDJSON = 'application/x-ndjson';

    /**
     * @param list<string> $lines
     * @param array{timestamp?:string,nonce?:string,client?:string,bodyOverride?:string} $options
     * @return array{status:int,headers:array<string,string>,body:string}
     */
    public static function sync(
        Instance $instance,
        string $client,
        array $lines = [],
        array $options = [],
        int $server = 0,
        string $contentType = self::NDJSON
    ): array {
        $body = SignRequest::body(
            $client,
            Harness::tokenFor($client),
            Harness::SECRET,
            'sync',
            $lines,
            $options
        );

        return self::post($instance, $body, $server, $contentType);
    }

    /** @return array{status:int,headers:array<string,string>,body:string} */
    public static function post(
        Instance $instance,
        string $body,
        int $server = 0,
        string $contentType = self::NDJSON
    ): array {
        return HttpClient::request(
            $instance->url($server) . '?action=sync',
            'POST',
            ['Content-Type' => $contentType],
            $body
        );
    }

    /** @param list<string> $lines */
    public static function requestFor(
        Instance $instance,
        string $client,
        array $lines = [],
        int $server = 0
    ): array {
        return [
            'url' => $instance->url($server) . '?action=sync',
            'method' => 'POST',
            'headers' => ['Content-Type' => self::NDJSON],
            'body' => SignRequest::body($client, Harness::tokenFor($client), Harness::SECRET, 'sync', $lines),
        ];
    }

    /** Asserts a 200 sync response and returns its decoded sync-result. */
    public static function resultOf(array $response, string $context): array
    {
        Assert::same(200, $response['status'], $context . ': expected a successful sync');
        $result = SignRequest::syncResult($response['body']);
        Assert::true(is_array($result), $context . ': the response must end with sync-result');

        return (array) $result;
    }
}

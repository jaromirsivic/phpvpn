<?php

declare(strict_types=1);

final class AssertionFailed extends RuntimeException
{
}

/** Thrown by a test that cannot run meaningfully on the current platform. */
final class SkippedTest extends RuntimeException
{
}

function skipTest(string $reason): never
{
    throw new SkippedTest($reason);
}

/** Minimal assertion helpers so the suite needs neither Composer nor PHPUnit. */
final class Assert
{
    public static function fail(string $message): never
    {
        throw new AssertionFailed($message);
    }

    public static function true(bool $value, string $message): void
    {
        if (!$value) {
            self::fail($message);
        }
    }

    public static function false(bool $value, string $message): void
    {
        if ($value) {
            self::fail($message);
        }
    }

    public static function same(mixed $expected, mixed $actual, string $message): void
    {
        if ($expected !== $actual) {
            self::fail($message . ' (expected ' . self::render($expected) . ', got ' . self::render($actual) . ')');
        }
    }

    public static function notSame(mixed $unexpected, mixed $actual, string $message): void
    {
        if ($unexpected === $actual) {
            self::fail($message . ' (unexpectedly ' . self::render($actual) . ')');
        }
    }

    public static function contains(string $needle, string $haystack, string $message): void
    {
        if (!str_contains($haystack, $needle)) {
            self::fail($message . ' (missing ' . self::render($needle) . ' in ' . self::render($haystack) . ')');
        }
    }

    public static function notContains(string $needle, string $haystack, string $message): void
    {
        if (str_contains($haystack, $needle)) {
            self::fail($message . ' (unexpectedly found ' . self::render($needle) . ')');
        }
    }

    public static function count(int $expected, array $actual, string $message): void
    {
        if (count($actual) !== $expected) {
            self::fail($message . ' (expected ' . $expected . ' items, got ' . count($actual) . ')');
        }
    }

    public static function greaterOrEqual(int|float $minimum, int|float $actual, string $message): void
    {
        if ($actual < $minimum) {
            self::fail($message . ' (expected at least ' . $minimum . ', got ' . $actual . ')');
        }
    }

    public static function lessOrEqual(int|float $maximum, int|float $actual, string $message): void
    {
        if ($actual > $maximum) {
            self::fail($message . ' (expected at most ' . $maximum . ', got ' . $actual . ')');
        }
    }

    private static function render(mixed $value): string
    {
        if (is_string($value)) {
            return strlen($value) > 120
                ? '"' . substr($value, 0, 117) . '..."'
                : '"' . $value . '"';
        }
        if (is_bool($value)) {
            return $value ? 'true' : 'false';
        }
        if (is_array($value)) {
            return 'array(' . count($value) . ')';
        }

        return var_export($value, true);
    }
}

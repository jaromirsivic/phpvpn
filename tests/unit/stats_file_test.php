<?php

/**
 * The statistics file must be cheap to update, forgiving of damage and unable
 * to break the sync that produced it.
 */

declare(strict_types=1);

return function (Suite $suite): void {
    loadServerInProcess();

    $reset = static function (): void {
        Ctx::init('unit');
        @unlink(StatsFile::path());
        foreach (glob(StatsFile::path() . '.*') ?: [] as $leftover) {
            @unlink($leftover);
        }
    };

    $entries = static function (): array {
        return (array) json_decode(StatsFile::read(), true);
    };

    $suite->test('stats records the time of the first sync of a client', function () use ($reset, $entries) {
        $reset();
        StatsFile::record('10.99.99.2');

        Assert::true(is_file(StatsFile::path()), 'the file is created on demand');
        Assert::same(
            ['10.99.99.2' => Wire::isoTime(Ctx::$now)],
            $entries(),
            'the record maps the address to the current UTC time'
        );
        Assert::count(0, glob(StatsFile::path() . '.*.writing') ?: [], 'no temporary file survives');
    });

    $suite->test('stats skips a record that is younger than the interval', function () use ($reset) {
        $reset();
        StatsFile::record('10.99.99.2');
        $first = (string) file_get_contents(StatsFile::path());

        Ctx::$now += STATS_MIN_UPDATE_SECONDS;
        StatsFile::record('10.99.99.2');

        Assert::same($first, (string) file_get_contents(StatsFile::path()), 'nothing is rewritten inside the window');
    });

    $suite->test('stats refreshes a record once it is older than the interval', function () use ($reset, $entries) {
        $reset();
        StatsFile::record('10.99.99.2');
        $before = Wire::isoTime(Ctx::$now);

        Ctx::$now += STATS_MIN_UPDATE_SECONDS + 1;
        StatsFile::record('10.99.99.2');

        Assert::same(
            ['10.99.99.2' => Wire::isoTime(Ctx::$now)],
            $entries(),
            'the timestamp moves forward'
        );
        Assert::notSame($before, $entries()['10.99.99.2'], 'and really changed');
    });

    $suite->test('stats never drops the record of another client', function () use ($reset, $entries) {
        $reset();
        StatsFile::record('10.99.99.2');
        $recorded = Wire::isoTime(Ctx::$now);

        Ctx::$now += 3600;
        StatsFile::record('10.99.99.7');
        Ctx::$now += 3600;
        StatsFile::record('10.99.99.9');

        $found = $entries();
        Assert::count(3, $found, 'old records are kept forever');
        Assert::same($recorded, $found['10.99.99.2'] ?? '', 'an untouched record keeps its original time');
    });

    $suite->test('stats survives a damaged file without ever throwing', function () use ($reset, $entries) {
        $damaged = [
            'not json at all',
            '',
            '[]',
            '[1,2,3]',
            '{"10.99.99.2":42}',
            '{"nonsense":"2026-08-08T14:20:00Z"}',
            '{"10.99.99.2":"yesterday"}',
            '{"10.99.99.999":"2026-08-08T14:20:00Z"}',
            '{"10.99.99.2":{"nested":"2026-08-08T14:20:00Z"}}',
            str_repeat('x', 70000),
        ];

        foreach ($damaged as $content) {
            $reset();
            file_put_contents(StatsFile::path(), $content);
            StatsFile::record('10.99.99.3');

            $found = $entries();
            Assert::same(
                ['10.99.99.3' => Wire::isoTime(Ctx::$now)],
                $found,
                'a file of ' . strlen($content) . ' bytes is replaced by a valid one'
            );
        }
    });

    $suite->test('stats keeps a foreign but well formed record while adding its own', function () use ($reset, $entries) {
        $reset();
        file_put_contents(
            StatsFile::path(),
            '{"10.99.99.254":"2020-01-01T00:00:00Z","bogus":"x","10.99.99.5":"2026-08-08T14:20:00Z"}'
        );
        StatsFile::record('10.99.99.3');

        $found = $entries();
        Assert::count(3, $found, 'two valid records survive and one is added');
        Assert::same('2020-01-01T00:00:00Z', $found['10.99.99.254'] ?? '', 'an ancient record is not touched');
        Assert::false(isset($found['bogus']), 'an entry that is not a client address is dropped');
    });

    $suite->test('stats reports an empty object when there is no file', function () use ($reset) {
        $reset();

        Assert::same('{}', StatsFile::read(), 'a missing file reads as an empty object');
        Assert::false(is_file(StatsFile::path()), 'and reading must not create it');
    });

    $suite->test('an unwritable statistics path is logged, not raised', function () use ($reset) {
        $reset();
        // A directory where the file should be makes every write fail, which is
        // the closest portable stand in for a full disk or a denied path.
        mkdir(StatsFile::path(), 0700, true);

        try {
            StatsFile::record('10.99.99.2');
            Assert::same('{}', StatsFile::read(), 'nothing readable is produced');
        } finally {
            foreach (glob(StatsFile::path() . '/*') ?: [] as $leftover) {
                @unlink($leftover);
            }
            rmdir(StatsFile::path());
        }
    });
};

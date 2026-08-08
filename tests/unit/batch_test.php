<?php

declare(strict_types=1);

return function (Suite $suite): void {
    loadServerInProcess();

    $suite->test('batch ids have a fixed length and validate', function () {
        foreach ([0, 1, 4096, 999999999] as $size) {
            $id = Batch::newId($size);
            Assert::same(Batch::ID_LENGTH, strlen($id), 'id length must never vary for size ' . $size);
            Assert::true(Batch::isValidId($id), 'generated id must validate: ' . $id);
            Assert::same($size, Batch::bytesOf($id), 'the embedded size must round trip');
        }
    });

    $suite->test('batch ids sort chronologically', function () {
        $first = Batch::newId(100);
        usleep(2000);
        $second = Batch::newId(100);
        Assert::true($first < $second, 'lexicographic order must equal FIFO order');
    });

    $suite->test('batch ids are unique under rapid generation', function () {
        $seen = [];
        for ($i = 0; $i < 500; $i++) {
            $seen[Batch::newId(1)] = true;
        }
        Assert::count(500, $seen, 'no collisions within one request');
    });

    $suite->test('malformed batch ids are rejected', function () {
        foreach ([
            '',
            'not-a-batch-id',
            '20260808T142000.123456Z_9f2c1ab7d4e60518',
            '20260808T142000.123456Z_9f2c1ab7d4e60518.12345',
            '20260808T142000.123456Z_9f2c1ab7d4e6051g.000000001',
            '../../../etc/passwd.000000001',
            '20260808T142000.123456Z_9f2c1ab7d4e60518.000000001x',
        ] as $rejected) {
            Assert::false(Batch::isValidId($rejected), 'must reject ' . var_export($rejected, true));
        }
    });

    $suite->test('the embedded creation time is recoverable', function () {
        $before = time();
        $id = Batch::newId(7);
        $epoch = Batch::epochOf($id);
        Assert::true($epoch !== null, 'a valid id must expose its creation time');
        Assert::lessOrEqual(2, abs((int) $epoch - $before), 'the embedded time must match the clock');
        Assert::same(null, Batch::epochOf('nonsense'), 'an invalid id has no time');
    });
};

<?php

declare(strict_types=1);

return function (Suite $suite): void {
    loadServerInProcess();

    /** Publishes one batch of $count packets into the queue of $octet. */
    $put = static function (int $octet, int $count = 1, int $payloadBytes = 8): int {
        $fragments = [];
        $bytes = 0;
        for ($i = 0; $i < $count; $i++) {
            $fragment = Wire::buildPacketFragment(
                SignRequest::uuid(),
                '10.99.99.2',
                '10.99.99.' . $octet,
                base64_encode(str_repeat('p', $payloadBytes))
            );
            $fragments[] = $fragment;
            $bytes += strlen($fragment) + Wire::deliveryOverhead();
        }
        $fill = 0.0;

        return Queue::enqueue($octet, $fragments, $bytes, $fill);
    };

    $names = static function (int $octet, string $suffix): array {
        $dir = QueuePath::queueDir($octet);
        $found = [];
        foreach (scandir($dir) ?: [] as $entry) {
            if (str_ends_with($entry, $suffix)) {
                $found[] = $entry;
            }
        }
        sort($found);

        return $found;
    };

    $suite->test('a published batch becomes visible as .data only', function () use ($put, $names) {
        Ctx::init('unit');
        Assert::same(Queue::OK, $put(10), 'enqueue succeeds');
        Assert::count(1, $names(10, '.data'), 'exactly one published batch');
        Assert::count(0, $names(10, '.writing'), 'no partial write is left behind');
    });

    $suite->test('collect claims a batch and never serves it twice', function () use ($put, $names) {
        Ctx::init('unit');
        $put(11);
        $put(11);

        $first = Queue::collect(11, [], MAX_RESPONSE_BYTES);
        Assert::count(2, $first['claimed'], 'both batches are claimed');
        Assert::false($first['truncated'], 'nothing is left over');
        Assert::count(2, $names(11, '.sending'), 'claimed batches wait for an ack');
        Assert::count(0, $names(11, '.data'), 'nothing stays publishable');

        $second = Queue::collect(11, [], MAX_RESPONSE_BYTES);
        Assert::count(0, $second['claimed'], 'a claimed batch is never served twice');
    });

    $suite->test('an ack removes the claimed batch', function () use ($put, $names) {
        Ctx::init('unit');
        $put(12);
        $claimed = Queue::collect(12, [], MAX_RESPONSE_BYTES)['claimed'];
        Assert::count(1, $claimed, 'one batch claimed');

        Queue::collect(12, [$claimed[0]['id']], MAX_RESPONSE_BYTES);
        Assert::count(0, $names(12, '.sending'), 'the acknowledged batch is gone');

        // Acknowledging twice is the normal at-least-once outcome, not an error.
        Queue::collect(12, [$claimed[0]['id']], MAX_RESPONSE_BYTES);
        Assert::count(0, $names(12, '.sending'), 'a repeated ack stays harmless');
    });

    $suite->test('the response budget splits a queue across cycles', function () use ($put) {
        Ctx::init('unit');
        for ($i = 0; $i < 6; $i++) {
            $put(13, 1, 600);
        }

        $one = Batch::bytesOf(scandirData(QueuePath::queueDir(13))[0]);
        $budget = $one * 3 + 1;

        $first = Queue::collect(13, [], $budget);
        Assert::count(3, $first['claimed'], 'the budget limits the first cycle');
        Assert::true($first['truncated'], 'the client is told more is waiting');

        $ids = array_map(static fn (array $batch): string => $batch['id'], $first['claimed']);
        $second = Queue::collect(13, $ids, $budget);
        Assert::count(3, $second['claimed'], 'the rest arrives in the next cycle');
        Assert::false($second['truncated'], 'and then the queue is empty');
    });

    $suite->test('an oversized single batch is still delivered', function () use ($put) {
        Ctx::init('unit');
        $put(14, 1, 4096);
        $result = Queue::collect(14, [], 16);
        Assert::count(1, $result['claimed'], 'a batch larger than the budget must not starve');
    });

    $suite->test('a full queue is reported instead of overflowing', function () use ($put, $names) {
        Ctx::init('unit');
        for ($i = 0; $i < MAX_BUFFERS; $i++) {
            Assert::same(Queue::OK, $put(15), 'batch ' . $i . ' fits');
        }
        Assert::same(Queue::FULL, $put(15), 'the queue refuses to grow past MAX_BUFFERS');
        Assert::count(MAX_BUFFERS, $names(15, '.data'), 'older batches survive the refusal');
        Assert::count(0, $names(15, '.writing'), 'the refused batch leaves nothing behind');
    });

    $suite->test('garbage collection drops expired batches', function () use ($put, $names) {
        Ctx::init('unit');
        $put(16);
        $stale = QueuePath::queueDir(16) . '/' . $names(16, '.data')[0];
        touch($stale, time() - QUEUE_TTL_SECONDS - 30);

        Gc::scan(QueuePath::queueDir(16), time(), true);
        Assert::count(0, $names(16, '.data'), 'an expired batch is removed');
    });

    $suite->test('garbage collection reclaims an abandoned claim', function () use ($put, $names) {
        Ctx::init('unit');
        $put(17);
        Queue::collect(17, [], MAX_RESPONSE_BYTES);
        $claimed = QueuePath::queueDir(17) . '/' . $names(17, '.sending')[0];
        touch($claimed, time() - SENDING_RECLAIM_SECONDS - 5);

        Gc::scan(QueuePath::queueDir(17), time(), true);
        Assert::count(1, $names(17, '.data'), 'the batch is publishable again');
        Assert::count(0, $names(17, '.sending'), 'and no longer counted as in flight');
    });

    $suite->test('an abandoned claim is dropped once it is older than the TTL', function () use ($put, $names) {
        Ctx::init('unit');
        $put(18);
        Queue::collect(18, [], MAX_RESPONSE_BYTES);
        $claimed = QueuePath::queueDir(18) . '/' . $names(18, '.sending')[0];

        // A fresh filemtime with an old batch id is exactly the ping-pong case
        // the absolute age backstop exists to stop.
        touch($claimed, time());
        Gc::scan(QueuePath::queueDir(18), time() + QUEUE_TTL_SECONDS + 10, true);

        Assert::count(0, $names(18, '.sending'), 'the claim is dropped');
        Assert::count(0, $names(18, '.data'), 'and it is not resurrected');
    });

    $suite->test('a stale partial write is cleaned up', function () use ($names) {
        Ctx::init('unit');
        $dir = QueuePath::queueDir(19);
        QueuePath::mkdirp($dir);
        $orphan = $dir . '/' . Batch::newId(10) . '.writing';
        file_put_contents($orphan, 'half a batch');
        touch($orphan, time() - WRITING_STALE_SECONDS - 5);

        Gc::scan($dir, time(), true);
        Assert::count(0, $names(19, '.writing'), 'the orphan is removed');
    });

    $suite->test('a foreign file in a queue directory is discarded', function () use ($names) {
        Ctx::init('unit');
        $dir = QueuePath::queueDir(20);
        QueuePath::mkdirp($dir);
        file_put_contents($dir . '/not-a-batch-id.data', 'garbage');

        Gc::scan($dir, time(), true);
        Assert::count(0, $names(20, '.data'), 'only well formed batch names survive');
    });

    $suite->test('queue.stat survives corruption', function () use ($put) {
        Ctx::init('unit');
        $put(21);
        file_put_contents(QueuePath::queueDir(21) . '/queue.stat', "corrupted\x00");

        Assert::same(null, QueueStat::load(QueuePath::queueDir(21)), 'corruption is detected');
        Assert::same(Queue::OK, $put(21), 'and the queue rebuilds itself');

        $stat = QueueStat::load(QueuePath::queueDir(21));
        Assert::same(2, $stat['files'] ?? -1, 'the rebuilt count is correct');
    });
};

/** @return list<string> batch ids of the .data files in a directory, sorted */
function scandirData(string $dir): array
{
    $ids = [];
    foreach (scandir($dir) ?: [] as $entry) {
        if (str_ends_with($entry, '.data')) {
            $ids[] = substr($entry, 0, -5);
        }
    }
    sort($ids);

    return $ids;
}

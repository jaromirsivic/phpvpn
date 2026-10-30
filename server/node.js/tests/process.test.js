'use strict';
// Separate server processes: shared root, hard kill and restart. Real HTTP, real disk.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const h = require('./helpers');

async function send(port, src, lines) {
    const request = h.dataBody(src, lines);
    const response = await h.post(port, 'data', request.body);
    if (response.status !== 200) return { status: response.status };
    return { status: 200, ...h.verifyDataResponse(response.body, request.requestId, request.fullKey) };
}

test('two processes sharing one root neither lose, duplicate nor over-fill a queue', async (t) => {
    const root = h.tempRoot(t);
    const capacity = 60000;
    const first = await h.startProcess(t, root, { MAX_QUEUE_BYTES_PER_CLIENT: capacity });
    const second = await h.startProcess(t, root, { MAX_QUEUE_BYTES_PER_CLIENT: capacity });
    const receiver = '10.99.99.3';
    const senders = ['10.99.99.2', '10.99.99.4', '10.99.99.5', '10.99.99.6'];
    const accepted = [];
    let deferred = 0;
    let busy = 0;
    await Promise.all(senders.flatMap((src, i) => Array.from({ length: 6 }, async () => {
        const records = Array.from({ length: 2 }, () => h.record(src, receiver, Buffer.alloc(1500, i)));
        const result = await send(i % 2 ? second.port : first.port, src, records.map((r) => r.line));
        if (result.status === 503) {
            busy++;
            return;
        }
        assert.equal(result.status, 200);
        accepted.push(...result.result.acceptedMessageIds);
        deferred += result.result.deferredMessageIds.length;
    })));
    const stored = fs.readdirSync(path.join(first.buffer, '10_99_99_3'))
        .map((name) => h.relay.DiskStorage.parseBatchName(name, h.config(first.buffer)))
        .filter((meta) => meta && meta.state === 'data')
        .reduce((sum, meta) => sum + meta.bytes, 0);
    assert.ok(stored <= capacity, `stored ${stored} bytes`);
    assert.ok(deferred > 0, 'the capacity was reached');
    // Parallel polls through both processes: every accepted record exactly once.
    const delivered = [];
    for (let round = 0; round < 3; round++) {
        const results = await Promise.all(Array.from({ length: 6 }, (_, i) => send(i % 2 ? second.port : first.port, receiver, [])));
        for (const result of results) if (result.status === 200) delivered.push(...result.records.map((r) => r.messageId));
    }
    assert.equal(new Set(delivered).size, delivered.length, 'no batch was delivered twice');
    assert.deepEqual([...delivered].sort(), [...accepted].sort(), `busy responses: ${busy}`);
});

test('published batches survive a hard kill; claimed ones are not re-issued', async (t) => {
    const root = h.tempRoot(t);
    const server = await h.startProcess(t, root);
    const sender = '10.99.99.2';
    const receiver = '10.99.99.3';
    const kept = h.record(sender, receiver, Buffer.from('before restart'));
    assert.deepEqual((await send(server.port, sender, [kept.line])).result.acceptedMessageIds, [kept.messageId]);
    await server.kill();
    // A crash leftover of an already claimed batch must never come back.
    const directory = path.join(server.buffer, '10_99_99_3');
    const [published] = fs.readdirSync(directory).filter((name) => name.endsWith('.data'));
    const lost = published.replace(/_[0-9a-f-]{36}\.data$/, '_6b2b9f5e-8a7e-4f1a-9a55-5a7d1d6f0c11.sending');
    fs.copyFileSync(path.join(directory, published), path.join(directory, lost));
    const restarted = await h.startProcess(t, root);
    const result = await send(restarted.port, receiver, []);
    assert.equal(result.status, 200);
    assert.deepEqual(result.records.map((r) => r.messageId), [kept.messageId]);
    assert.deepEqual((await send(restarted.port, receiver, [])).records, []);
});

test('an orphaned lock blocks only its queue until offline recovery', async (t) => {
    const root = h.tempRoot(t);
    const server = await h.startProcess(t, root);
    const sender = '10.99.99.2';
    const keep = h.record(sender, '10.99.99.3');
    await send(server.port, sender, [keep.line]);
    await server.kill();
    fs.mkdirSync(path.join(server.buffer, '10_99_99_3', '.lock'));
    let restarted = await h.startProcess(t, root);
    assert.equal((await send(restarted.port, sender, [h.record(sender, '10.99.99.3').line])).status, 503);
    assert.equal((await send(restarted.port, sender, [h.record(sender, '10.99.99.4').line])).status, 200, 'other queues work');
    await restarted.kill();
    const configFile = path.join(root, 'recovery.json');
    fs.writeFileSync(configFile, JSON.stringify({ BUFFER_DIRECTORY: server.buffer }));
    const output = execFileSync(process.execPath, [h.SERVER, '--offline-recovery', '--config', configFile], { stdio: 'pipe', encoding: 'utf8' });
    assert.equal(output, '');
    restarted = await h.startProcess(t, root);
    const polled = await send(restarted.port, '10.99.99.3', []);
    assert.deepEqual(polled.records.map((r) => r.messageId), [keep.messageId], 'published data survived the recovery');
});

test('check mode validates the configuration and rejects unknown settings', (t) => {
    const root = h.tempRoot(t);
    const good = path.join(root, 'good.json');
    const bad = path.join(root, 'bad.json');
    fs.writeFileSync(good, JSON.stringify({ LISTEN_PORT: 9000 }));
    fs.writeFileSync(bad, JSON.stringify({ REDIS_URL: 'redis://x' }));
    execFileSync(process.execPath, [h.SERVER, '--check-config', '--config', good], { stdio: 'pipe' });
    assert.throws(() => execFileSync(process.execPath, [h.SERVER, '--check-config', '--config', bad], { stdio: 'pipe' }),
        (error) => error.status === 2 && /unknown setting REDIS_URL/.test(error.stderr.toString()));
});

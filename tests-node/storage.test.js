'use strict';
// DiskStorage semantics: atomic groups, capacity, TTL, claims, markers, locks, faults.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { relay, config, record, tempRoot } = require('./helpers');

const SRC = '10.99.99.2';
const DST = '10.99.99.3';

function clockAt(start = Date.now()) {
    const clock = { value: start, now: () => clock.value };
    return clock;
}

function storage(t, overrides = {}, options = {}) {
    const root = tempRoot(t);
    const cfg = config(root, overrides);
    const clock = options.clock || clockAt();
    return { root, cfg, clock, disk: new relay.DiskStorage(cfg, { clock, fsImpl: options.fsImpl }) };
}

function parsed(src, dst, count = 1, size = 16, cfg) {
    return Array.from({ length: count }, () => relay.parseDataRecord(Buffer.from(record(src, dst, Buffer.alloc(size, 1)).line), src, cfg));
}

const names = (root, address) => {
    try {
        return fs.readdirSync(path.join(root, address.replace(/\./g, '_'))).sort();
    } catch {
        return [];
    }
};

test('a published group is one .data batch with an expiry-only name and a private header', async (t) => {
    const { root, cfg, clock, disk } = storage(t);
    const group = parsed(SRC, DST, 3, 16, cfg);
    assert.equal(await disk.enqueue(DST, SRC, group), true);
    const files = names(root, DST);
    assert.equal(files.length, 1);
    const meta = relay.DiskStorage.parseBatchName(files[0], cfg);
    assert.equal(meta.state, 'data');
    assert.equal(meta.src, SRC);
    assert.equal(meta.bytes, group.reduce((sum, r) => sum + r.line.length + 1, 0));
    assert.equal(meta.expires, clock.value + 60000);
    assert.match(files[0], /^\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}\.\d{3}Z_\d+_10\.99\.99\.2_[0-9a-f-]{36}\.data$/);
    const content = fs.readFileSync(path.join(root, '10_99_99_3', files[0]));
    assert.ok(content.toString('latin1').startsWith('{"format":"phpvpn-node-1","bytes":'));
    const drained = await disk.drain(DST, clock.value);
    assert.equal(drained.length, 1);
    assert.deepEqual(drained[0].pairs, group.map((r) => [r.messageId, r.tag]));
    assert.equal(drained[0].body.toString('latin1'), group.map((r) => r.line.toString('latin1') + '\n').join(''));
    assert.deepEqual(names(root, DST), [], 'claimed batches are removed before the response');
    assert.deepEqual(await disk.drain(DST, clock.value), []);
});

test('capacity: exactly one targeted sweep and one retry, then the whole group is deferred', async (t) => {
    const { root, cfg, clock, disk } = storage(t, { MAX_QUEUE_BYTES_PER_CLIENT: 6000 });
    const first = parsed(SRC, DST, 1, 2000, cfg);
    assert.equal(await disk.enqueue(DST, SRC, first), true);
    let sweeps = 0;
    const sweepQueue = disk.sweepQueue.bind(disk);
    disk.sweepQueue = async (...args) => {
        sweeps++;
        return sweepQueue(...args);
    };
    assert.equal(await disk.enqueue(DST, SRC, parsed(SRC, DST, 2, 2000, cfg)), false);
    assert.equal(sweeps, 1);
    assert.equal(names(root, DST).length, 1, 'nothing of the deferred group was written');
    // A physically present expired batch no longer counts, so no sweep is needed.
    clock.value += 60000;
    assert.equal(await disk.enqueue(DST, SRC, parsed(SRC, DST, 2, 2000, cfg)), true);
    assert.equal(sweeps, 1);
    assert.equal(names(root, DST).length, 2);
    // Full again: one sweep removes the expired file, the retry still does not fit.
    assert.equal(await disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 2000, cfg)), false);
    assert.equal(sweeps, 2);
    assert.equal(names(root, DST).length, 1, 'the expired batch was swept');
    // A group that fits never sweeps.
    const other = '10.99.99.4';
    assert.equal(await disk.enqueue(other, SRC, parsed(SRC, other, 1, 10, cfg)), true);
    assert.equal(sweeps, 2);
});

test('expired and claimed batches do not count, markers and locks are ignored', async (t) => {
    const { root, cfg, clock, disk } = storage(t);
    await disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 100, cfg));
    const usage = await disk.usage(DST, clock.value);
    assert.ok(usage > 100);
    fs.writeFileSync(path.join(root, '10_99_99_3', 'info.json'), 'x'.repeat(5000));
    fs.mkdirSync(path.join(root, '10_99_99_3', '.lock'));
    assert.equal(await disk.usage(DST, clock.value), usage);
    assert.equal(await disk.usage(DST, clock.value + 60000), 0, 'expiry instant is exclusive');
    assert.deepEqual(await disk.drain(DST, clock.value + 60000).catch((e) => e.code), 'queue_busy');
    fs.rmdirSync(path.join(root, '10_99_99_3', '.lock'));
    assert.deepEqual(await disk.drain(DST, clock.value + 60000), [], 'an expired batch is never delivered');
    await disk.sweepQueue(DST, clock.value + 60000);
    assert.deepEqual(names(root, DST), ['info.json'], 'the marker survives the sweep');
});

test('the targeted sweep removes expired batches in every state', async (t) => {
    const { root, cfg, clock, disk } = storage(t);
    const directory = path.join(root, '10_99_99_3');
    fs.mkdirSync(directory, { recursive: true });
    const base = (ms) => `${relay.formatTimestamp(ms).replace(/:/g, '-')}_10_${SRC}_${crypto.randomUUID()}`;
    for (const state of ['receiving', 'data', 'sending']) {
        fs.writeFileSync(path.join(directory, `${base(clock.value - 1)}.${state}`), 'x');
        fs.writeFileSync(path.join(directory, `${base(clock.value + 5000)}.${state}`), 'x');
    }
    await disk.sweepQueue(DST, clock.value);
    const left = names(root, DST).map((name) => relay.DiskStorage.parseBatchName(name, cfg));
    assert.equal(left.length, 3);
    assert.ok(left.every((meta) => meta.expires > clock.value));
});

test('a restart keeps published unclaimed batches, never re-issues .sending or publishes .receiving', async (t) => {
    const { root, cfg, clock, disk } = storage(t);
    const kept = parsed(SRC, DST, 2, 50, cfg);
    await disk.enqueue(DST, SRC, kept);
    const directory = path.join(root, '10_99_99_3');
    const stamp = relay.formatTimestamp(clock.value + 30000).replace(/:/g, '-');
    fs.writeFileSync(path.join(directory, `${stamp}_10_${SRC}_6b2b9f5e-8a7e-4f1a-9a55-5a7d1d6f0c11.sending`), 'x');
    fs.writeFileSync(path.join(directory, `${stamp}_10_${SRC}_7b2b9f5e-8a7e-4f1a-9a55-5a7d1d6f0c11.receiving`), 'x');
    // A new instance models a restarted process: no state other than the disk.
    const restarted = new relay.DiskStorage(cfg, { clock });
    const drained = await restarted.drain(DST, clock.value + 1000);
    assert.equal(drained.length, 1);
    assert.deepEqual(drained[0].pairs, kept.map((r) => [r.messageId, r.tag]));
    assert.equal(names(root, DST).filter((n) => n.endsWith('.data')).length, 0);
});

test('damaged batches are dropped, valid neighbours are still delivered', async (t) => {
    const { root, cfg, clock, disk } = storage(t);
    await disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 10, cfg));
    await disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 10, cfg));
    const directory = path.join(root, '10_99_99_3');
    const [first] = names(root, DST);
    const file = path.join(directory, first);
    const content = fs.readFileSync(file);
    content[content.length - 5] = 0x21; // Breaks the canonical ciphertext of the stored line.
    fs.writeFileSync(file, content);
    const drained = await disk.drain(DST, clock.value);
    assert.equal(drained.length, 1);
    assert.deepEqual(names(root, DST), []);
});

test('a snapshot published after the initial scan waits for the next poll', async (t) => {
    const { cfg, clock, disk } = storage(t);
    await disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 10, cfg));
    const readdir = disk.fs.readdir;
    let injected = false;
    disk.fs = { ...fsp, readdir: async (...args) => {
        const result = await readdir(...args);
        if (!injected && String(args[0]).endsWith('10_99_99_3')) {
            injected = true;
            const late = new relay.DiskStorage(cfg, { clock });
            const group = parsed(SRC, DST, 1, 10, cfg);
            await late.writeBatch(path.join(cfg.bufferRoot, '10_99_99_3'), SRC, group, group[0].line.length + 1);
        }
        return result;
    } };
    assert.equal((await disk.drain(DST, clock.value)).length, 1);
    disk.fs = fsp;
    assert.equal((await disk.drain(DST, clock.value)).length, 1, 'the late batch is not lost');
});

test('concurrent drains in one process never deliver a batch twice', async (t) => {
    const { cfg, clock, disk } = storage(t);
    const published = [];
    for (let i = 0; i < 12; i++) {
        const group = parsed(SRC, DST, 4, 10, cfg);
        published.push(...group.map((r) => r.messageId));
        await disk.enqueue(DST, SRC, group);
    }
    const results = await Promise.all(Array.from({ length: 8 }, () => disk.drain(DST, clock.value).catch(() => [])));
    const delivered = results.flat().flatMap((batch) => batch.pairs.map(([id]) => id));
    assert.equal(new Set(delivered).size, delivered.length);
    const rest = (await disk.drain(DST, clock.value)).flatMap((batch) => batch.pairs.map(([id]) => id));
    assert.deepEqual([...delivered, ...rest].sort(), published.sort());
});

test('a held lock yields queue_busy after the lock timeout, never blocking', async (t) => {
    const { root, cfg, disk } = storage(t);
    fs.mkdirSync(path.join(root, '10_99_99_3', '.lock'), { recursive: true });
    const started = performance.now();
    await assert.rejects(disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 10, cfg)), (error) => error.code === 'queue_busy' && error.status === 503);
    const elapsed = performance.now() - started;
    assert.ok(elapsed >= 190 && elapsed < 1000, `elapsed ${elapsed}`);
    assert.deepEqual(names(root, DST), ['.lock']);
});

test('offline recovery removes orphaned locks and unpublished leftovers, keeps .data', async (t) => {
    const { root, cfg, clock, disk } = storage(t);
    await disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 10, cfg));
    const directory = path.join(root, '10_99_99_3');
    fs.mkdirSync(path.join(directory, '.lock'));
    const stamp = relay.formatTimestamp(clock.value + 30000).replace(/:/g, '-');
    fs.writeFileSync(path.join(directory, `${stamp}_10_${SRC}_6b2b9f5e-8a7e-4f1a-9a55-5a7d1d6f0c11.receiving`), 'x');
    fs.writeFileSync(path.join(directory, 'info.json'), '');
    assert.deepEqual(await disk.offlineRecovery(), { locks: 1, receiving: 1, sending: 0 });
    assert.equal(names(root, DST).filter((n) => n.endsWith('.data')).length, 1);
    assert.ok(names(root, DST).includes('info.json'));
    assert.equal((await disk.drain(DST, clock.value)).length, 1);
});

test('marker: empty on creation, 30000 ms untouched, 31000 ms touched, future untouched', async (t) => {
    const { root, clock, disk } = storage(t);
    const marker = path.join(root, '10_99_99_2', 'info.json');
    const t0 = Math.floor(clock.value / 1000) * 1000;
    await disk.touch(SRC, t0);
    assert.equal(fs.statSync(marker).size, 0);
    assert.equal(await disk.lastSeen(SRC), t0);
    await disk.touch(SRC, t0 + 30000);
    assert.equal(await disk.lastSeen(SRC), t0);
    await disk.touch(SRC, t0 + 31000);
    assert.equal(await disk.lastSeen(SRC), t0 + 31000);
    await disk.touch(SRC, t0 + 20000);
    assert.equal(await disk.lastSeen(SRC), t0 + 31000, 'a marker in the future is not changed');
});

test('ordinary touch preserves published contracts, explicit [] withdraws them', async (t) => {
    const { root, clock, disk } = storage(t);
    const hash = 'b'.repeat(64);
    await disk.publishContracts(SRC, [{ peer: DST, hash }], clock.value);
    const marker = path.join(root, '10_99_99_2', 'info.json');
    assert.equal(fs.readFileSync(marker, 'utf8'), `{"maskedCommunicationContracts":[{"peer":"${DST}","hash":"${hash}"}]}`);
    await disk.touch(SRC, clock.value + 40000);
    assert.equal(await disk.lastSeen(SRC), Math.floor(clock.value + 40000));
    assert.deepEqual((await disk.snapshot(SRC)).contracts, [{ peer: DST, hash }]);
    await disk.publishContracts(SRC, [], clock.value + 41000);
    const snapshot = await disk.snapshot(SRC);
    assert.deepEqual(snapshot.contracts, []);
    assert.equal(snapshot.seen, Math.floor(clock.value + 41000));
    fs.writeFileSync(marker, '{"maskedCommunicationContracts":[{"peer":"10.99.99.2","hash":"x"}]}');
    const damaged = await disk.snapshot(SRC);
    assert.deepEqual(damaged.contracts, []);
    assert.notEqual(damaged.seen, null, 'damaged content keeps a readable lastSeen');
    assert.equal(names(root, SRC).filter((n) => n.startsWith('.info-')).length, 0);
});

test('bounded sweep removes old info temp files and expired batches only', async (t) => {
    const { root, cfg, clock, disk } = storage(t);
    await disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 10, cfg));
    const directory = path.join(root, '10_99_99_3');
    const oldTemp = path.join(directory, '.info-6b2b9f5e-8a7e-4f1a-9a55-5a7d1d6f0c11.tmp');
    const newTemp = path.join(directory, '.info-7b2b9f5e-8a7e-4f1a-9a55-5a7d1d6f0c11.tmp');
    fs.writeFileSync(oldTemp, '');
    fs.writeFileSync(newTemp, '');
    fs.utimesSync(oldTemp, (clock.value - 301000) / 1000, (clock.value - 301000) / 1000);
    fs.writeFileSync(path.join(directory, 'info.json'), '');
    await disk.sweep();
    assert.equal(fs.existsSync(oldTemp), false);
    assert.equal(fs.existsSync(newTemp), true);
    assert.equal(names(root, DST).filter((n) => n.endsWith('.data')).length, 1);
    clock.value += 61000;
    disk.lastSweep = -Infinity;
    await disk.sweep();
    assert.equal(names(root, DST).filter((n) => n.endsWith('.data')).length, 0);
    assert.ok(fs.existsSync(path.join(directory, 'info.json')));
});

// --- Fault injection ------------------------------------------------------

function faultyFs(overrides) {
    return { ...fsp, ...overrides };
}

test('short or zero writes publish nothing and leave no temp file', async (t) => {
    const fsImpl = faultyFs({ open: async (file, flags) => {
        const handle = await fsp.open(file, flags);
        if (flags === 'wx' && String(file).endsWith('.receiving')) {
            return { write: async () => ({ bytesWritten: 0 }), close: () => handle.close(), sync: () => handle.sync() };
        }
        return handle;
    } });
    const { root, cfg, disk } = storage(t, {}, { fsImpl });
    await assert.rejects(disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 10, cfg)), relay.StorageError);
    assert.deepEqual(names(root, DST), []);
});

test('a failing publish rename or close publishes nothing', async (t) => {
    for (const failing of ['rename', 'close']) {
        const fsImpl = faultyFs({
            rename: async (from, to) => {
                if (failing === 'rename') throw Object.assign(new Error('EIO'), { code: 'EIO', syscall: 'rename' });
                return fsp.rename(from, to);
            },
            open: async (file, flags) => {
                const handle = await fsp.open(file, flags);
                if (failing === 'close' && flags === 'wx') {
                    return { write: (...a) => handle.write(...a), sync: () => handle.sync(), close: async () => {
                        await handle.close();
                        throw Object.assign(new Error('EIO'), { code: 'EIO', syscall: 'close' });
                    } };
                }
                return handle;
            },
        });
        const { root, cfg, disk } = storage(t, {}, { fsImpl });
        await assert.rejects(disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 10, cfg)), (e) => e.syscall === failing);
        assert.deepEqual(names(root, DST), [], failing);
    }
});

test('an unreadable queue is an error, never a falsely empty drain', async (t) => {
    const readdirFails = faultyFs({ readdir: async () => {
        throw Object.assign(new Error('EIO'), { code: 'EIO', syscall: 'scandir' });
    } });
    const a = storage(t, {}, { fsImpl: readdirFails });
    await assert.rejects(a.disk.drain(DST, a.clock.value), (e) => e.code === 'EIO');

    const b = storage(t);
    await b.disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 10, b.cfg));
    b.disk.fs = faultyFs({ readFile: async () => {
        throw Object.assign(new Error('EIO'), { code: 'EIO', syscall: 'read' });
    } });
    await assert.rejects(b.disk.drain(DST, b.clock.value), (e) => e.code === 'EIO');
    b.disk.fs = fsp;
    assert.deepEqual(await b.disk.drain(DST, b.clock.value), [], 'a claimed batch is never restored');
});

test('statistics failures are silent and change nothing else', async (t) => {
    const fsImpl = faultyFs({
        stat: async () => {
            throw Object.assign(new Error('EACCES'), { code: 'EACCES', syscall: 'stat' });
        },
        utimes: async () => {
            throw Object.assign(new Error('EACCES'), { code: 'EACCES', syscall: 'utime' });
        },
    });
    const { clock, disk } = storage(t, {}, { fsImpl });
    await disk.touch(SRC, clock.value);
    assert.equal(await disk.lastSeen(SRC), null);
});

test('a failed contract publication keeps the old marker and reports an error', async (t) => {
    const { root, clock, disk } = storage(t);
    const hash = 'c'.repeat(64);
    await disk.publishContracts(SRC, [{ peer: DST, hash }], clock.value);
    disk.fs = faultyFs({ rename: async () => {
        throw Object.assign(new Error('EPERM'), { code: 'EPERM', syscall: 'rename' });
    } });
    await assert.rejects(disk.publishContracts(SRC, [], clock.value), (e) => e.syscall === 'rename');
    disk.fs = fsp;
    assert.deepEqual((await disk.snapshot(SRC)).contracts, [{ peer: DST, hash }]);
    assert.equal(names(root, SRC).filter((n) => n.startsWith('.info-')).length, 0);
});

test('a symlinked client directory is refused', async (t) => {
    const { root, cfg, disk } = storage(t);
    const elsewhere = tempRoot(t);
    fs.mkdirSync(root, { recursive: true });
    try {
        fs.symlinkSync(elsewhere, path.join(root, '10_99_99_3'), 'junction');
    } catch {
        t.skip('symlink creation is not permitted here');
        return;
    }
    await assert.rejects(disk.enqueue(DST, SRC, parsed(SRC, DST, 1, 10, cfg)), relay.StorageError);
    assert.deepEqual(fs.readdirSync(elsewhere), []);
});

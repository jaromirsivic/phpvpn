'use strict';
// The real HTTP adapter around Application, on an ephemeral local port.
const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const h = require('./helpers');

const { relay } = h;
const JSON_TYPE = 'application/json; charset=utf-8';

function assertHeaders(response) {
    assert.equal(response.headers['cache-control'], 'no-store, no-cache, must-revalidate');
    assert.equal(response.headers.pragma, 'no-cache');
    assert.equal(response.headers['x-content-type-options'], 'nosniff');
    assert.equal(response.headers['x-powered-by'], undefined);
    assert.equal(response.headers.server, undefined);
}

function assertError(response, status, code) {
    assert.equal(response.status, status, response.body.toString());
    assertHeaders(response);
    assert.equal(response.headers['content-type'], JSON_TYPE);
    if (response.body.length) assert.deepEqual(JSON.parse(response.body), { type: 'error', code });
    if (status === 503) assert.equal(response.headers['retry-after'], '1');
    if (status === 405) assert.ok(response.headers.allow);
}

async function start(t, overrides = {}, options = {}) {
    const root = h.tempRoot(t);
    const logs = [];
    const buffer = path.join(root, 'buffer');
    const started = await h.startApp(t, { overrides: { BUFFER_DIRECTORY: buffer, ...overrides },
        log: (...args) => logs.push(args.join(' ')), ...options });
    return { ...started, root, buffer, logs };
}

test('status page: warning only for predictable secrets, HEAD without body, can be disabled', async (t) => {
    const insecure = await start(t);
    const page = await h.request(insecure.port, { method: 'GET' });
    assert.equal(page.status, 200);
    assertHeaders(page);
    assert.equal(page.headers['content-type'], 'text/html; charset=utf-8');
    assert.match(page.body.toString(), /<h1>PHPVPN server is running<\/h1>/);
    assert.match(page.body.toString(), /<aside><b>Warning:<\/b>/);
    const head = await h.request(insecure.port, { method: 'HEAD' });
    assert.equal(head.status, 200);
    assert.equal(head.body.length, 0);
    assert.equal(head.headers['content-length'], String(page.body.length));
    for (const secret of ['', '0000', String.fromCharCode(0, 0, 0)]) {
        const variant = await start(t, { SERVER_SECRET: secret });
        assert.match((await h.request(variant.port, { method: 'GET' })).body.toString(), /<aside>/, JSON.stringify(secret));
    }
    const secure = await start(t, { SERVER_SECRET: 'a real secret' });
    const clean = (await h.request(secure.port, { method: 'GET' })).body.toString();
    assert.doesNotMatch(clean, /<aside>/);
    assert.match(clean, /Generate key/);
    const disabled = await start(t, { STATUS_PAGE_ENABLED: false });
    assertError(await h.request(disabled.port, { method: 'GET' }), 404, 'not_found');
    assertError(await h.request(disabled.port, { method: 'HEAD' }), 404, 'not_found');
    const health = await h.post(disabled.port, 'health', Buffer.from(JSON.stringify({ type: 'auth-none', requestId: crypto.randomUUID() })));
    assert.equal(health.status, 200, 'the switch never restricts the API');
});

test('status page: embedded template matches the shared mockup', () => {
    const mockup = fs.readFileSync(path.join(__dirname, '..', '..', '..', 'assets', 'index.html'), 'utf8').replace(/\r?\n$/, '');
    assert.equal(relay.STATUS_HTML, mockup);
});

test('status page: Show HTTP Headers lists the received headers for the Configuration Wizard', async (t) => {
    const { port } = await start(t, { SERVER_SECRET: 'a real secret' });
    const page = await h.request(port, { method: 'GET', contentType: 'text/plain', headers: {
        'User-Agent': 'A<b>&"\'', Cookie: ['a=1', 'b=2'], 'X-Dup': ['1', '2'], 'X-Test': '', 'X-Bin': '\xff' } });
    const body = page.body.toString();
    assert.match(body, /<button id=s>Show HTTP Headers<\/button>/);
    assert.match(body, /<a href=https:\/\/www\.pilandrome\.com>www\.pilandrome\.com<\/a> \(MIT License\)/);
    assert.ok(!body.includes('A<b>'), 'header values are HTML escaped');
    const shown = /<textarea[^>]*>([^]*?)<\/textarea>/.exec(body)[1];
    const entities = { '&amp;': '&', '&lt;': '<', '&gt;': '>', '&quot;': '"', '&#39;': "'" };
    assert.equal(shown.replace(/&(amp|lt|gt|quot|#39);/g, (e) => entities[e]),
        'User-Agent: A<b>&"\'\nCookie: a=1; b=2\nX-Dup: 1, 2\nX-Test: \nX-Bin: �');
    assert.equal(page.headers['content-length'], String(page.body.length));
});

test('routing: exact path, known actions, methods with Allow, media types', async (t) => {
    const { port } = await start(t);
    assertError(await h.request(port, { method: 'GET', pathName: '/other' }), 404, 'not_found');
    assertError(await h.request(port, { method: 'GET', pathName: '/phpvpn.php/' }), 404, 'not_found');
    assertError(await h.request(port, { method: 'GET', pathName: '/phpvpn_data/10_99_99_2/info.json' }), 404, 'not_found');
    assertError(await h.request(port, { method: 'GET', query: '?action=nope' }), 404, 'not_found');
    assertError(await h.request(port, { method: 'POST', query: '?action[]=health', contentType: JSON_TYPE }), 404, 'not_found');
    const post = await h.request(port, { method: 'POST', body: Buffer.alloc(0) });
    assertError(post, 405, 'method_not_allowed');
    assert.equal(post.headers.allow, 'GET, HEAD');
    for (const method of ['GET', 'HEAD', 'PUT', 'DELETE']) {
        const response = await h.request(port, { method, query: '?action=health' });
        assertError(response, 405, 'method_not_allowed');
        assert.equal(response.headers.allow, 'POST');
    }
    assertError(await h.post(port, 'health', Buffer.from('{}'), 'text/plain'), 415, 'unsupported_media_type');
    assertError(await h.post(port, 'data', Buffer.from('{}'), JSON_TYPE), 415, 'unsupported_media_type');
    assertError(await h.post(port, 'info', Buffer.from('{}'), 'application/json; charset=latin1'), 415, 'unsupported_media_type');
});

test('health: the GMAC authenticates every field and the fresh requestId', async (t) => {
    const { port } = await start(t);
    const requestId = crypto.randomUUID();
    const response = await h.post(port, 'health', Buffer.from(JSON.stringify({ type: 'auth-none', requestId })));
    assert.equal(response.status, 200);
    const payload = JSON.parse(response.body);
    assert.deepEqual(Object.keys(payload), ['serverVersion', 'status', 'description', 'timestamp', 'requestId', 'iv', 'tag']);
    assert.equal(payload.requestId, requestId);
    assert.ok(Math.abs(relay.parseTimestamp(payload.timestamp) - Date.now()) < 5000);
    const aad = relay.healthAad(requestId, payload.serverVersion, payload.timestamp, payload.iv);
    const opened = relay.gcmOpen(relay.healthKey(h.SECRET, requestId), Buffer.from(payload.iv, 'base64url'), Buffer.alloc(0),
        Buffer.from(payload.tag, 'base64url'), aad);
    assert.deepEqual(opened, Buffer.alloc(0));
    for (const body of [`{"type":"auth-none","requestId":"${requestId}","tag":"x"}`, `{"type":"auth-none"}`,
        `{"type":"auth-none","requestId":"${requestId.toUpperCase()}"}`]) {
        assertError(await h.post(port, 'health', Buffer.from(body)), 400, 'bad_request');
    }
    assertError(await h.post(port, 'health', Buffer.alloc(4097, 0x20)), 413, 'payload_too_large');
});

test('data: self-send in the same response, exact result and a verifiable data-mac', async (t) => {
    const { port, buffer } = await start(t);
    const src = '10.99.99.2';
    const own = h.record(src, src);
    const other = h.record(src, '10.99.99.3');
    const { body, requestId, fullKey } = h.dataBody(src, [own.line, other.line]);
    const response = await h.post(port, 'data', body);
    assert.equal(response.status, 200);
    assert.equal(response.headers['content-type'], 'application/x-ndjson; charset=utf-8');
    const { result, records } = h.verifyDataResponse(response.body, requestId, fullKey);
    assert.deepEqual(Object.keys(result), ['type', 'requestId', 'acceptedMessageIds', 'deferredMessageIds', 'acceptedRecords', 'acceptedBytes']);
    assert.deepEqual(result.acceptedMessageIds, [own.messageId, other.messageId]);
    assert.equal(result.acceptedBytes, own.line.length + other.line.length + 2);
    assert.deepEqual(records.map((r) => r.messageId), [own.messageId]);
    assert.ok(fs.statSync(path.join(buffer, '10_99_99_2', 'info.json')).isFile());
    // Replay inside the window is processed again, no history exists.
    const replay = await h.post(port, 'data', body);
    assert.equal(replay.status, 200);
    const receiver = h.dataBody('10.99.99.3');
    const delivered = h.verifyDataResponse((await h.post(port, 'data', receiver.body)).body, receiver.requestId, receiver.fullKey);
    assert.deepEqual(delivered.records.map((r) => r.messageId), [other.messageId, other.messageId]);
});

test('authentication: skew edges, unknown source, spoofed record source, nothing stored before auth', async (t) => {
    const { port, buffer } = await start(t);
    const src = '10.99.99.2';
    const status = async (options, lines = []) => (await h.post(port, 'data', h.dataBody(src, lines, options).body)).status;
    assert.equal(await status({ now: Date.now() - 58000 }), 200);
    assert.equal(await status({ now: Date.now() + 58000 }), 200);
    assert.equal(await status({ now: Date.now() - 61000 }), 401);
    assert.equal(await status({ now: Date.now() + 61000 }), 401);
    assert.equal(await status({ key: Buffer.alloc(32) }), 401);
    const unknown = await h.post(port, 'data', h.dataBody('10.99.99.200').body);
    const wrongKey = await h.post(port, 'data', h.dataBody(src, [], { key: Buffer.alloc(32) }).body);
    assertError(unknown, 401, 'unauthorized');
    assert.deepEqual(unknown.body, wrongKey.body);
    const spoofed = h.record('10.99.99.4', '10.99.99.3');
    assert.equal(await status({}, [spoofed.line]), 401);
    const forged = h.dataBody(src, [h.record(src, '10.99.99.5').line], { key: Buffer.alloc(32) });
    assertError(await h.post(port, 'data', forged.body), 401, 'unauthorized');
    assert.equal(fs.existsSync(path.join(buffer, '10_99_99_5')), false, 'no storage change before authentication');
    const tooMany = Array.from({ length: 17 }, () => h.record(src, '10.99.99.6').line);
    assertError(await h.post(port, 'data', h.dataBody(src, tooMany).body), 413, 'payload_too_large');
    assert.equal(fs.existsSync(path.join(buffer, '10_99_99_6')), false);
});

test('full destination queue: 200 with deferred ids, inbound drain still happens', async (t) => {
    const { port } = await start(t, { MAX_QUEUE_BYTES_PER_CLIENT: 3000 });
    const src = '10.99.99.2';
    const dst = '10.99.99.3';
    const big = h.record(src, dst, Buffer.alloc(1400, 7));
    const fill = h.dataBody(src, [big.line]);
    const first = h.verifyDataResponse((await h.post(port, 'data', fill.body)).body, fill.requestId, fill.fullKey);
    assert.deepEqual(first.result.acceptedMessageIds, [big.messageId]);
    // A third client queues for the caller; a request by dst would drain dst's queue.
    const incoming = h.record('10.99.99.5', src);
    assert.equal((await h.post(port, 'data', h.dataBody('10.99.99.5', [incoming.line]).body)).status, 200);
    // Every outgoing group deferred: still 200, and the caller's inbound queue is drained.
    const again = h.record(src, dst, Buffer.alloc(1400, 8));
    const deferred = h.dataBody(src, [again.line]);
    const response = await h.post(port, 'data', deferred.body);
    assert.equal(response.status, 200);
    const parsed = h.verifyDataResponse(response.body, deferred.requestId, deferred.fullKey);
    assert.deepEqual(parsed.result.acceptedMessageIds, []);
    assert.deepEqual(parsed.result.deferredMessageIds, [again.messageId]);
    assert.equal(parsed.result.acceptedBytes, 0);
    assert.deepEqual(parsed.records.map((r) => r.messageId), [incoming.messageId]);
    // A full destination does not cancel the success of another destination.
    const other = h.record(src, '10.99.99.4');
    const mixed = h.dataBody(src, [again.line, other.line]);
    const result = h.verifyDataResponse((await h.post(port, 'data', mixed.body)).body, mixed.requestId, mixed.fullKey).result;
    assert.deepEqual(result.acceptedMessageIds, [other.messageId]);
    assert.deepEqual(result.deferredMessageIds, [again.messageId]);
    assert.equal(result.acceptedRecords, 1);
});

test('503 queue_busy and queue_unavailable are distinct, with Retry-After', async (t) => {
    const busy = await start(t);
    fs.mkdirSync(path.join(busy.buffer, '10_99_99_3', '.lock'), { recursive: true });
    const src = '10.99.99.2';
    assertError(await h.post(busy.port, 'data', h.dataBody(src, [h.record(src, '10.99.99.3').line]).body), 503, 'queue_busy');
    const unavailable = await start(t);
    fs.writeFileSync(unavailable.buffer, 'not a directory');
    assertError(await h.post(unavailable.port, 'data', h.dataBody(src).body), 503, 'queue_unavailable');
    assert.ok(unavailable.logs.every((line) => !line.includes(unavailable.buffer) && !line.includes('CHANGE_THIS')));
});

test('a broken configuration keeps the status page and answers the API with server_misconfigured', async (t) => {
    const { port } = await start(t, { MIN_DATA_POLL_INTERVAL_MS: 9999 });
    assert.equal((await h.request(port, { method: 'GET' })).status, 200);
    assertError(await h.post(port, 'health', Buffer.from('{}')), 503, 'server_misconfigured');
});

test('info: standard is metadata only, extended publication and privacy rules', async (t) => {
    const { port, buffer } = await start(t);
    const hash = (n) => n.toString(16).repeat(64).slice(0, 64);
    // .3 publishes relations with .2 and .4; .4 publishes a relation with .5.
    const publish3 = h.extendedInfoBody('10.99.99.3', { maskedCommunicationContracts: [
        { peer: '10.99.99.2', hash: hash(1) }, { peer: '10.99.99.4', hash: hash(2) }] });
    assert.equal((await h.post(port, 'info', publish3.body)).status, 200);
    const publish4 = h.extendedInfoBody('10.99.99.4', { maskedCommunicationContracts: [{ peer: '10.99.99.5', hash: hash(3) }] });
    assert.equal((await h.post(port, 'info', publish4.body)).status, 200);
    // A queued record for .2 must survive an include-only request.
    const queued = h.record('10.99.99.3', '10.99.99.2');
    await h.post(port, 'data', h.dataBody('10.99.99.3', [queued.line]).body);
    const read = h.extendedInfoBody('10.99.99.2', { includeMaskedCommunicationContracts: true });
    const response = await h.post(port, 'info', read.body);
    assert.equal(response.status, 200);
    const { value } = h.decryptInfo(JSON.parse(response.body), read.requestId);
    const byAddress = Object.fromEntries(value.clients.map((client) => [client.address, client]));
    assert.deepEqual(byAddress['10.99.99.2'].maskedCommunicationContracts, []);
    assert.deepEqual(byAddress['10.99.99.3'].maskedCommunicationContracts, [{ peer: '10.99.99.2', hash: hash(1) }]);
    assert.deepEqual(byAddress['10.99.99.4'].maskedCommunicationContracts, []);
    const poll = h.dataBody('10.99.99.2');
    const drained = h.verifyDataResponse((await h.post(port, 'data', poll.body)).body, poll.requestId, poll.fullKey);
    assert.deepEqual(drained.records.map((r) => r.messageId), [queued.messageId], 'info never drains the queue');
    const standard = h.semiBody();
    const plain = h.decryptInfo(JSON.parse((await h.post(port, 'info', standard.body)).body), standard.requestId);
    assert.ok(plain.value.clients.every((client) => Object.keys(client).join() === 'address,lastSeen'));
    assert.ok(plain.text.startsWith(`{\n    "serverVersion": "${h.relay.DEFAULT_CONFIG.SERVER_VERSION}",\n    "generatedAt": `));
    assert.deepEqual(plain.value.limits, { maxDataRecordsPerRequest: 16, maxRequestBodyBytes: 983040,
        maxQueueBytesPerClient: 16777216, queueTtlSeconds: 60, minDataPollIntervalMs: 50, maxDataPollIntervalMs: 3200 });
    assert.equal(fs.existsSync(path.join(buffer, '10_99_99_9')), false, 'info creates no directories for other clients');
    // Presence of false or [] still requires full authentication; auth-full without extension is invalid.
    const semiWithFalse = JSON.parse(h.semiBody().body);
    semiWithFalse.includeMaskedCommunicationContracts = false;
    assertError(await h.post(port, 'info', Buffer.from(JSON.stringify(semiWithFalse))), 400, 'bad_request');
    const bare = JSON.parse(h.extendedInfoBody('10.99.99.2', { includeMaskedCommunicationContracts: false }).body);
    delete bare.includeMaskedCommunicationContracts;
    assertError(await h.post(port, 'info', Buffer.from(JSON.stringify(bare))), 400, 'bad_request');
    const unknownSource = h.extendedInfoBody('10.99.99.200', { includeMaskedCommunicationContracts: true });
    assertError(await h.post(port, 'info', unknownSource.body), 401, 'unauthorized');
    const tooMany = h.extendedInfoBody('10.99.99.2', { maskedCommunicationContracts: Array.from({ length: 253 },
        (_, i) => ({ peer: `10.99.99.${i + 3 > 254 ? 1 : i + 3}`, hash: hash(1) })) });
    assertError(await h.post(port, 'info', tooMany.body), 413, 'payload_too_large');
});

test('info plaintext limit drops whole clients from the highest address', async (t) => {
    const { port, app } = await start(t, { MAX_INFO_PLAINTEXT_BYTES: 1024 });
    for (const address of app.cfg.addresses) await h.post(port, 'data', h.dataBody(address).body);
    const standard = h.semiBody();
    const { text, value } = h.decryptInfo(JSON.parse((await h.post(port, 'info', standard.body)).body), standard.requestId);
    assert.ok(Buffer.byteLength(text) <= 1024);
    const kept = value.clients.map((client) => client.address);
    assert.ok(kept.length > 0 && kept.length < 14);
    assert.deepEqual(kept, app.cfg.addresses.slice(0, kept.length));
    // One more client would exceed the limit: the result is maximal.
    const withNext = { ...value, clients: [...value.clients, { address: app.cfg.addresses[kept.length], lastSeen: value.generatedAt }] };
    assert.ok(relay.prettyJson(withNext).length > 1024);
});

test('oversized bodies are refused with 413 without buffering them', async (t) => {
    const { port } = await start(t);
    const huge = await h.request(port, { query: '?action=data', contentType: 'application/x-ndjson',
        headers: { 'Content-Length': String(64 * 1024 * 1024) } });
    assertError(huge, 413, 'payload_too_large');
    const body = Buffer.alloc(983041, 0x61);
    assertError(await h.post(port, 'data', body), 413, 'payload_too_large');
    const info = Buffer.alloc(65537, 0x20);
    assertError(await h.post(port, 'info', info), 413, 'payload_too_large');
});

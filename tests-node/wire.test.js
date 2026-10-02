'use strict';
// Known answers, canonical encodings and strict parsers of phpvpn.js itself.
const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const { relay, config, record, tempRoot } = require('./helpers');

const DEFAULT = Buffer.from('CHANGE_THIS_SERVER_SECRET');
const hex = (buffer) => buffer.toString('hex');

function referenceHkdf(ikm, salt, info) {
    // RFC 5869 with L = 32, one expand block, independent of crypto.hkdfSync.
    const prk = crypto.createHmac('sha256', salt).update(ikm).digest();
    return crypto.createHmac('sha256', prk).update(Buffer.concat([info, Buffer.from([1])])).digest();
}

test('HKDF matches RFC 5869 test case 1 and the reference construction', () => {
    const okm = relay.hkdf(Buffer.alloc(22, 0x0b), Buffer.from('000102030405060708090a0b0c', 'hex'),
        Buffer.from('f0f1f2f3f4f5f6f7f8f9', 'hex'));
    assert.equal(hex(okm), '3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf');
    const ikm = crypto.randomBytes(40);
    const salt = crypto.randomBytes(32);
    assert.deepEqual(relay.hkdf(ikm, salt, Buffer.from('x')), referenceHkdf(ikm, salt, Buffer.from('x')));
});

test('HMAC-SHA-256 matches RFC 4231 test case 2', () => {
    assert.equal(hex(relay.hmac(Buffer.from('Jefe'), Buffer.from('what do ya want for nothing?'))),
        '5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843');
});

test('AES-256-GCM known answers, tamper rejection and the startup self test', () => {
    assert.equal(relay.gcmSelfTest(), true);
    const key = Buffer.from('feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308', 'hex');
    const iv = Buffer.from('cafebabefacedbaddecaf888', 'hex');
    const plaintext = Buffer.from('d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39', 'hex');
    const aad = Buffer.from('feedfacedeadbeeffeedfacedeadbeefabaddad2', 'hex');
    const sealed = relay.gcmSeal(key, iv, plaintext, aad);
    assert.equal(hex(sealed.tag), '76fc6ece0f4e1768cddf8853bb2d551b');
    assert.deepEqual(relay.gcmOpen(key, iv, sealed.ciphertext, sealed.tag, aad), plaintext);
    for (const part of ['key', 'iv', 'aad', 'ciphertext', 'tag']) {
        const values = { key: Buffer.from(key), iv: Buffer.from(iv), aad: Buffer.from(aad),
            ciphertext: Buffer.from(sealed.ciphertext), tag: Buffer.from(sealed.tag) };
        values[part][0] ^= 1;
        assert.equal(relay.gcmOpen(values.key, values.iv, values.ciphertext, values.tag, values.aad), null, part);
    }
});

test('derivations use the normative domains, NUL separators and argument order', () => {
    assert.equal(hex(relay.semiKey(DEFAULT)), 'b4eacc4bdd452fbe309ecf9ca9597c0343ac71d12db83fd304383869c03b4128');
    assert.equal(hex(relay.fullAuthKey(DEFAULT, Buffer.alloc(32, 2), '10.99.99.2')),
        '1c8343e9561baf650c947f5456a3a8be10cfbbbdedc32b84d77f6812064d7469');
    assert.equal(hex(relay.healthKey(DEFAULT, 'c22cd6e8-14a9-4d83-97b7-54c26a72d1f0')),
        '1359ec3c87c677da219cc986d526a6d5bef5fd7a9961390feec6cd371e02531a');
    const sha = (text) => crypto.createHash('sha256').update(text).digest();
    const secret = Buffer.from('s3cret');
    const access = Buffer.alloc(32, 0xa1);
    const salt = relay.hmac(secret, Buffer.from('phpvpn/auth-full/salt\x0010.99.99.2', 'latin1'));
    assert.deepEqual(relay.fullAuthSalt(secret, '10.99.99.2'), salt);
    assert.deepEqual(relay.fullAuthKey(secret, access, '10.99.99.2'),
        referenceHkdf(access, salt, Buffer.from('phpvpn/auth-full/key\x0010.99.99.2', 'latin1')));
    assert.deepEqual(relay.semiKey(secret), referenceHkdf(secret, sha('phpvpn/auth-semi/salt'), Buffer.from('phpvpn/auth-semi/key')));
    const rid = '4e94de3a-b9ad-4bc3-9297-82b98a19a143';
    assert.deepEqual(relay.healthKey(secret, rid), referenceHkdf(secret,
        sha(Buffer.from('phpvpn/health-response/salt\x00' + rid, 'latin1')), Buffer.from('phpvpn/health-response/key\x00' + rid, 'latin1')));
    const infoSalt = Buffer.alloc(32, 0x5a);
    assert.deepEqual(relay.infoKey(secret, infoSalt, rid), referenceHkdf(secret, infoSalt, Buffer.from('phpvpn/info-response/' + rid)));
    // One changed byte of either secret changes the key.
    assert.notDeepEqual(relay.fullAuthKey(Buffer.from('s3crev'), access, '10.99.99.2'), relay.fullAuthKey(secret, access, '10.99.99.2'));
    assert.notDeepEqual(relay.fullAuthKey(secret, Buffer.alloc(32, 0xa0), '10.99.99.2'), relay.fullAuthKey(secret, access, '10.99.99.2'));
});

test('canonical strings and the data-mac layout', () => {
    assert.equal(relay.semiCanonical('T', 'R'), 'PHPVPN-AUTH-SEMI\nPOST\ninfo\nT\nR');
    assert.equal(relay.fullCanonical('data', 'S', 'T', 'R', 'H'), 'PHPVPN-AUTH-FULL\nPOST\ndata\nS\nT\nR\nH');
    assert.equal(relay.healthAad('R', '1.0.0', 'T', 'I').toString(), 'PHPVPN-HEALTH-RESPONSE\nhealth\nR\n1.0.0\nrunning\nOK\nT\nI');
    assert.equal(relay.infoAad('R', 'S', 'I').toString(), 'PHPVPN-INFO\ninfo\nR\nS\nI');
    const key = Buffer.alloc(32, 7);
    const result = Buffer.from('{"type":"data-result"}');
    const expectedEmpty = relay.hmac(key, Buffer.from('PHPVPN-DATA-RESPONSE\ndata\nR\n0\n' + relay.sha256hex(result)));
    assert.deepEqual(relay.dataResponseMac(key, 'R', [], result), expectedEmpty);
    const expectedTwo = relay.hmac(key, Buffer.from('PHPVPN-DATA-RESPONSE\ndata\nR\n2\na\nx\nb\ny\n' + relay.sha256hex(result)));
    assert.deepEqual(relay.dataResponseMac(key, 'R', [['a', 'x'], ['b', 'y']], result), expectedTwo);
    assert.notDeepEqual(relay.dataResponseMac(key, 'R', [['b', 'y'], ['a', 'x']], result), expectedTwo);
});

test('only the exactly empty secret is normalized', (t) => {
    const root = tempRoot(t);
    const secret = (value) => config(root, { SERVER_SECRET: value }).secret;
    assert.deepEqual(secret(''), DEFAULT);
    assert.deepEqual(relay.semiKey(secret('')), relay.semiKey(secret('CHANGE_THIS_SERVER_SECRET')));
    assert.deepEqual(secret('0'), Buffer.from('0'));
    assert.deepEqual(secret('0'.repeat(64)), Buffer.from('0'.repeat(64)));
    assert.deepEqual(secret(String.fromCharCode(0).repeat(32)), Buffer.alloc(32));
    assert.deepEqual(secret(String.fromCharCode(0x10d)), Buffer.from([0xc4, 0x8d]));
    for (const value of ['', 'CHANGE_THIS_SERVER_SECRET', '0', '000', String.fromCharCode(0, 0)]) {
        assert.equal(relay.isInsecureSecret(value), true, JSON.stringify(value));
    }
    assert.equal(relay.isInsecureSecret('0x0'), false);
    assert.throws(() => config(root, { SERVER_SECRET: String.fromCharCode(0xd800) }), relay.ConfigError);
});

test('configuration rejects unknown settings, wrong types and broken invariants', (t) => {
    const root = tempRoot(t);
    assert.throws(() => config(root, { STORAGE_DRIVER: 'memory' }), /unknown setting/);
    assert.throws(() => config(root, { LISTEN_PORT: '8080' }), relay.ConfigError);
    assert.throws(() => config(root, { MIN_DATA_POLL_INTERVAL_MS: 5000 }), relay.ConfigError);
    assert.throws(() => config(root, { ACCESS_KEYS: { '10.99.99.2': 'AB'.repeat(32) } }), relay.ConfigError);
    assert.throws(() => config(root, { ACCESS_KEYS: { '10.99.99.02': '02'.repeat(32) } }), relay.ConfigError);
    const cfg = config(root);
    assert.equal(cfg.addresses.length, 14);
    assert.equal(cfg.addresses[0], '10.99.99.2');
    assert.equal(cfg.addresses.at(-1), '10.99.99.15');
    assert.equal(relay.buildConfig({}).bufferRoot, require('node:path').resolve(__dirname, '..', 'phpvpn_data'));
});

test('base64url accepts only the canonical spelling', () => {
    assert.deepEqual(relay.b64decode('_w'), Buffer.from([0xff]));
    assert.deepEqual(relay.b64decode('__8'), Buffer.from([0xff, 0xff]));
    assert.deepEqual(relay.b64decode(''), Buffer.alloc(0));
    for (const text of ['_x', '__9', 'A', 'AAAAA', 'AA==', 'AA AA', 'AA+A', 'AA/A', 'AA\nA']) {
        assert.equal(relay.b64decode(text), null, text);
    }
    assert.equal(relay.b64decode('A'.repeat(16), 12)?.length, 12);
    assert.equal(relay.b64decode('A'.repeat(16), 16), null);
    for (let i = 0; i < 200; i++) {
        const bytes = crypto.randomBytes(i % 40);
        assert.deepEqual(relay.b64decode(bytes.toString('base64url')), bytes);
    }
});

test('timestamps: exact format and an explicit Gregorian calendar', () => {
    assert.equal(relay.parseTimestamp('1970-01-01T00:00:00.000Z'), 0);
    assert.equal(relay.parseTimestamp('2024-02-29T23:59:59.999Z'), Date.UTC(2024, 1, 29, 23, 59, 59, 999));
    assert.equal(relay.parseTimestamp('0000-01-01T00:00:00.000Z'), -62167219200000);
    for (const bad of ['2023-02-29T00:00:00.000Z', '2100-02-29T00:00:00.000Z', '2026-13-01T00:00:00.000Z',
        '2026-04-31T00:00:00.000Z', '2026-01-01T24:00:00.000Z', '2026-01-01T00:60:00.000Z', '2026-01-01T00:00:60.000Z',
        '2026-01-01t00:00:00.000Z', '2026-01-01T00:00:00.000z', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00.0000Z',
        '2026-01-01T00:00:00.000+00:00', ' 2026-01-01T00:00:00.000Z', 1786000530000]) {
        assert.equal(relay.parseTimestamp(bad), null, String(bad));
    }
    assert.equal(relay.formatTimestamp(1786000530000), '2026-08-06T07:15:30.000Z');
    assert.equal(relay.formatTimestamp(1786000530123.9), '2026-08-06T07:15:30.123Z');
});

test('flat objects reject duplicates, escapes, unknown keys and trailing bytes', () => {
    const keys = ['type', 'requestId'];
    const ok = relay.parseFlatObject(Buffer.from('{"type":"auth-none", "requestId":"x"}'), keys);
    assert.deepEqual({ ...ok }, { type: 'auth-none', requestId: 'x' });
    for (const text of ['{"type":"a","type":"b","requestId":"x"}', '{"type":"a"}', '{"type":"a","requestId":"x","tag":"y"}',
        '{"type":"a","requestId":"x"}\n', ' {"type":"a","requestId":"x"}', '{"type":"a","requestId":"x"}x',
        '{"type":"a","requestId":"x\\u0041"}', '{"type":"a","requestId":1}', '{"type":"a","requestId":["x"]}',
        '{"t\\u0079pe":"a","requestId":"x"}']) {
        assert.equal(relay.parseFlatObject(Buffer.from(text), keys), null, text);
    }
    assert.equal(relay.parseFlatObject(Buffer.from([0x7b, 0x22, 0x74, 0x79, 0x70, 0x65, 0x22, 0x3a, 0x22, 0xc3, 0xa9, 0x22, 0x7d]), ['type']), null);
});

test('info objects detect escaped duplicate names at every level and strict UTF-8', () => {
    const parse = (text) => relay.parseInfoObject(Buffer.from(text, 'utf8'), 65536);
    assert.notEqual(parse('{"a":[{"b":true,"c":null}],"d":"x"}'), null);
    assert.equal(parse('{"src":"a","\\u0073rc":"b"}'), null);
    assert.equal(parse('{"a":[{"peer":"x","p\\u0065er":"y"}]}'), null);
    assert.equal(parse('{"a":1}'), null, 'numbers never occur in the schema');
    assert.equal(parse('{"a":[[[[["deep"]]]]]}'), null);
    assert.notEqual(parse('{"a":[[["ok"]]]}'), null);
    assert.equal(parse('{"a":"\\ud800"}'), null, 'unpaired surrogate');
    assert.equal(parse('{"a\\u0000":"x"}'), null);
    assert.equal(parse('{"a":"x"} x'), null);
    assert.equal(parse('[]'), null);
    assert.equal(relay.parseInfoObject(Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), Buffer.from('{}')]), 65536), null, 'BOM');
    assert.equal(relay.parseInfoObject(Buffer.from([0x7b, 0x22, 0x61, 0x22, 0x3a, 0x22, 0xff, 0x22, 0x7d]), 65536), null, 'invalid UTF-8');
    assert.equal(relay.parseInfoObject(Buffer.from('{"a":"' + 'x'.repeat(100) + '"}'), 50), null, 'size bound');
    const proto = parse('{"__proto__":"x"}');
    assert.equal(proto.__proto__, 'x');
    assert.equal(Object.getPrototypeOf(proto), null);
});

test('masked contracts: shape, uniqueness, self and the count limit', (t) => {
    const cfg = config(tempRoot(t));
    const hash = 'a'.repeat(64);
    assert.deepEqual(relay.maskedContracts([{ hash, peer: '10.99.99.3' }], '10.99.99.2', cfg), [{ peer: '10.99.99.3', hash }]);
    const status = (value) => {
        try {
            relay.maskedContracts(value, '10.99.99.2', cfg);
            return 200;
        } catch (error) {
            return error.status;
        }
    };
    assert.equal(status({}), 400);
    assert.equal(status([{ peer: '10.99.99.2', hash }]), 400);
    assert.equal(status([{ peer: '10.99.99.3', hash }, { peer: '10.99.99.3', hash }]), 400);
    assert.equal(status([{ peer: '10.99.99.3', hash: 'A'.repeat(64) }]), 400);
    assert.equal(status([{ peer: '10.99.99.3', hash, extra: 'x' }]), 400);
    assert.equal(status([null]), 400);
    const peers = (count) => Array.from({ length: count }, (_, i) => ({ peer: `10.99.99.${i + 3}`, hash }));
    assert.equal(status(peers(252)), 200);
    assert.equal(status([...peers(252), { peer: '10.99.99.1', hash }]), 413);
});

test('data records: normative order, canonical values, 400 versus spoofed 401', (t) => {
    const cfg = config(tempRoot(t));
    const good = record('10.99.99.2', '10.99.99.3');
    const parsed = relay.parseDataRecord(Buffer.from(good.line), '10.99.99.2', cfg);
    assert.equal(parsed.dst, '10.99.99.3');
    assert.equal(parsed.tag, good.tag);
    const status = (line, src = '10.99.99.2') => {
        try {
            relay.parseDataRecord(Buffer.from(line, 'latin1'), src, cfg);
            return 200;
        } catch (error) {
            return error.status;
        }
    };
    assert.equal(status(good.line, '10.99.99.4'), 401);
    assert.equal(status(good.line.replace('"src":"10.99.99.2"', '"src":"10.99.99.22"')), 401);
    assert.equal(status(good.line.replace('"dst":"10.99.99.3"', '"dst":"10.99.99.255"')), 400);
    assert.equal(status(good.line.replace('{"type":"data",', '{"type": "data",')), 400);
    assert.equal(status(good.line.replace('"}', '","x":"y"}')), 400);
    assert.equal(status(good.line.replace(/"ciphertext":"[^"]*"/, '"ciphertext":""')), 400);
    assert.equal(status(good.line.replace(/"ciphertext":"([^"]*)"/, '"ciphertext":"$1="')), 400);
    const reordered = JSON.parse(good.line);
    assert.equal(status(JSON.stringify({ type: 'data', messageId: reordered.messageId, dst: reordered.dst, src: reordered.src,
        iv: reordered.iv, tag: reordered.tag, ciphertext: reordered.ciphertext })), 400);
});

test('record sequences: LF rules, count limit and per-request uniqueness', (t) => {
    const cfg = config(tempRoot(t));
    const lines = (count) => Array.from({ length: count }, () => record('10.99.99.2', '10.99.99.3').line);
    const status = (text) => {
        try {
            relay.parseRecords(Buffer.from(text, 'latin1'), '10.99.99.2', cfg);
            return 200;
        } catch (error) {
            return error.status;
        }
    };
    assert.equal(status(lines(16).join('\n') + '\n'), 200);
    assert.equal(status(lines(17).join('\n') + '\n'), 413);
    assert.equal(status('\n'.repeat(100000)), 413);
    assert.equal(status(lines(1)[0]), 400, 'missing final LF');
    assert.equal(status(lines(1)[0] + '\r\n'), 400);
    assert.equal(status(lines(1)[0] + '\n\n'), 400);
    const one = lines(1)[0];
    assert.equal(status(`${one}\n${one}\n`), 400, 'duplicate messageId');
});

test('media types tolerate parameters but only a UTF-8 charset', () => {
    assert.equal(relay.mediaTypeMatches('application/json; charset=utf-8', 'application/json'), true);
    assert.equal(relay.mediaTypeMatches('Application/JSON;charset="UTF8"', 'application/json'), true);
    assert.equal(relay.mediaTypeMatches('application/json; charset=latin1', 'application/json'), false);
    assert.equal(relay.mediaTypeMatches('application/x-ndjson', 'application/json'), false);
    assert.equal(relay.mediaTypeMatches(undefined, 'application/json'), false);
});

test('pretty info serialization matches the PHP JSON_PRETTY_PRINT layout', () => {
    const text = relay.prettyJson({ a: 'x/' + String.fromCharCode(0xe9), b: [], c: [{ d: 1 }] }).toString();
    assert.equal(text, '{\n    "a": "x/\\u00e9",\n    "b": [],\n    "c": [\n        {\n            "d": 1\n        }\n    ]\n}');
});

test('importing phpvpn.js opens no listener', () => {
    const { execFileSync } = require('node:child_process');
    const output = execFileSync(process.execPath, ['-e',
        `require(${JSON.stringify(require('./helpers').SERVER)}); console.log(process.getActiveResourcesInfo().filter((r) => r === 'TCPServerWrap').length)`]);
    assert.equal(output.toString().trim(), '0');
});

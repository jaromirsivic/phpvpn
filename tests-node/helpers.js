'use strict';
// Shared helpers for the deterministic Node server tests. No npm dependencies.
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const { spawn } = require('node:child_process');

const SERVER = path.resolve(__dirname, '..', 'phpvpn.js');
const relay = require(SERVER);

const SECRET = Buffer.from('CHANGE_THIS_SERVER_SECRET');

function accessKey(address) {
    const octet = Number(address.split('.')[3]);
    return Buffer.alloc(32, octet);
}

function tempRoot(t) {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'phpvpn-node-test-'));
    if (t) t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
    return directory;
}

function config(root, overrides = {}) {
    return relay.buildConfig({ BUFFER_DIRECTORY: root, ...overrides });
}

function stamp(ms) {
    return relay.formatTimestamp(ms);
}

/** A syntactically valid data record line. The payload is real AES-GCM. */
function record(src, dst, plaintext = Buffer.from('payload'), messageId = crypto.randomUUID()) {
    const key = Buffer.alloc(32, 0x55);
    const iv = crypto.randomBytes(12);
    const aad = Buffer.from(`PHPVPN-DATA\ndata\n${messageId}\n${src}\n${dst}\n${iv.toString('base64url')}`);
    const sealed = relay.gcmSeal(key, iv, plaintext, aad);
    const line = `{"type":"data","messageId":"${messageId}","src":"${src}","dst":"${dst}","iv":"${iv.toString('base64url')}",`
        + `"tag":"${sealed.tag.toString('base64url')}","ciphertext":"${sealed.ciphertext.toString('base64url')}"}`;
    return { messageId, line, tag: sealed.tag.toString('base64url') };
}

function dataBody(src, lines = [], { now = Date.now(), requestId = crypto.randomUUID(), key = accessKey(src), secret = SECRET } = {}) {
    const rest = Buffer.from(lines.map((line) => line + '\n').join(''), 'latin1');
    const timestamp = stamp(now);
    const fullKey = relay.fullAuthKey(secret, key, src);
    const canonical = relay.fullCanonical('data', src, timestamp, requestId, relay.sha256hex(rest));
    const tag = relay.hmac(fullKey, Buffer.from(canonical)).toString('base64url');
    const auth = `{"type":"auth-full","src":"${src}","timestamp":"${timestamp}","requestId":"${requestId}","tag":"${tag}"}`;
    return { body: Buffer.concat([Buffer.from(auth + '\n'), rest]), requestId, fullKey };
}

function semiBody({ now = Date.now(), requestId = crypto.randomUUID(), secret = SECRET } = {}) {
    const timestamp = stamp(now);
    const tag = relay.hmac(relay.semiKey(secret), Buffer.from(relay.semiCanonical(timestamp, requestId))).toString('base64url');
    return { body: Buffer.from(JSON.stringify({ type: 'auth-semi', timestamp, requestId, tag })), requestId };
}

function extendedInfoBody(src, extensions, { now = Date.now(), requestId = crypto.randomUUID(), secret = SECRET } = {}) {
    const timestamp = stamp(now);
    const fullKey = relay.fullAuthKey(secret, accessKey(src), src);
    const hash = relay.sha256hex(Buffer.from(JSON.stringify(extensions)));
    const tag = relay.hmac(fullKey, Buffer.from(relay.fullCanonical('info', src, timestamp, requestId, hash))).toString('base64url');
    return { body: Buffer.from(JSON.stringify({ type: 'auth-full', src, timestamp, requestId, tag, ...extensions })), requestId };
}

/** Parses and verifies a data response exactly like a client, chapter 9.5. */
function verifyDataResponse(buffer, requestId, fullKey) {
    const text = buffer.toString('latin1');
    if (!text.endsWith('\n')) throw new Error('response without final LF');
    const lines = text.slice(0, -1).split('\n');
    const mac = JSON.parse(lines.pop());
    const resultLine = lines.pop();
    const result = JSON.parse(resultLine);
    const records = lines.map((line) => JSON.parse(line));
    const expected = relay.dataResponseMac(fullKey, requestId, records.map((r) => [r.messageId, r.tag]), Buffer.from(resultLine));
    if (mac.type !== 'data-mac' || mac.requestId !== requestId || result.requestId !== requestId
        || mac.tag !== expected.toString('base64url')) {
        throw new Error('data-mac verification failed');
    }
    return { result, records };
}

function decryptInfo(payload, requestId, secret = SECRET) {
    const salt = Buffer.from(payload.salt, 'base64url');
    const iv = Buffer.from(payload.iv, 'base64url');
    const key = relay.infoKey(secret, salt, requestId);
    const plaintext = relay.gcmOpen(key, iv, Buffer.from(payload.ciphertext, 'base64url'), Buffer.from(payload.tag, 'base64url'),
        relay.infoAad(requestId, payload.salt, payload.iv));
    if (plaintext === null) throw new Error('info tag invalid');
    return { text: plaintext.toString('utf8'), value: JSON.parse(plaintext.toString('utf8')) };
}

function request(port, { method = 'POST', query = '', body = null, contentType, headers = {}, pathName = '/phpvpn.php' } = {}) {
    return new Promise((resolve, reject) => {
        const req = http.request({ host: '127.0.0.1', port, method, path: pathName + query, agent: false,
            headers: { ...(contentType ? { 'Content-Type': contentType } : {}), ...headers } }, (res) => {
            const chunks = [];
            res.on('data', (chunk) => chunks.push(chunk));
            res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) }));
        });
        req.on('error', reject);
        if (body !== null) req.write(body);
        req.end();
    });
}

const post = (port, action, body, contentType) => request(port, {
    query: `?action=${action}`, body,
    contentType: contentType || (action === 'data' ? 'application/x-ndjson; charset=utf-8' : 'application/json; charset=utf-8'),
});

/** Starts an in-process HTTP server around an Application on an ephemeral port. */
async function startApp(t, options) {
    const app = new relay.Application(options);
    const server = relay.createHttpServer(app);
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    t.after(() => new Promise((resolve) => {
        server.closeAllConnections();
        server.close(resolve);
    }));
    return { app, server, port: server.address().port };
}

/** Starts phpvpn.js as a separate process with a JSON configuration file. */
async function startProcess(t, root, overrides = {}) {
    const configFile = path.join(root, `config-${crypto.randomUUID()}.json`);
    const buffer = path.join(root, 'buffer');
    fs.writeFileSync(configFile, JSON.stringify({ BUFFER_DIRECTORY: buffer, LISTEN_PORT: 0, ...overrides }));
    const child = spawn(process.execPath, [SERVER, '--config', configFile], { stdio: ['ignore', 'ignore', 'pipe'] });
    let log = '';
    const port = await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('server did not start: ' + log)), 10000);
        child.stderr.on('data', (chunk) => {
            log += chunk;
            const match = /listening http:\/\/127\.0\.0\.1:(\d+)\//.exec(log);
            if (match) {
                clearTimeout(timer);
                resolve(Number(match[1]));
            }
        });
        child.on('exit', () => reject(new Error('server exited: ' + log)));
    });
    const handle = { child, port, buffer, log: () => log, kill: () => new Promise((resolve) => {
        if (child.exitCode !== null || child.signalCode !== null) return resolve();
        child.once('exit', resolve);
        child.kill('SIGKILL');
    }) };
    if (t) t.after(handle.kill);
    return handle;
}

module.exports = {
    relay, SERVER, SECRET, accessKey, tempRoot, config, stamp, record, dataBody, semiBody, extendedInfoBody,
    verifyDataResponse, decryptInfo, request, post, startApp, startProcess,
};

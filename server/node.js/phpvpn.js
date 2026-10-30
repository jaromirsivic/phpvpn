#!/usr/bin/env node
// PHP VPN 1.0.2 (MIT License, Copyright (c) www.pilandrome.com)
'use strict';
/*
 * PHPVPN v1 relay for Node.js. MIT licensed.
 *
 * One deployable file, built-in modules only: no npm install, no build step,
 * no assets. The normative wire contract is protocol.md. The only storage
 * backend is the local disk. The on-disk format is private to this server and
 * is not compatible with phpvpn.php or phpvpn.py; start with an empty root.
 *
 *   node phpvpn.js                           start with the header below
 *   node phpvpn.js --config server.json      header values overridden by JSON
 *   node phpvpn.js --check-config [...]      validate the configuration, exit
 *   node phpvpn.js --offline-recovery [...]  remove stale locks after a crash,
 *                                            only while every server is stopped
 *
 * Supported runtime: Node.js 24 LTS. See README.md.
 */

// ---------------------------------------------------------------------------
// 1. Configuration header. Administrators edit only this block, or pass the
//    same keys in a JSON file with --config. Secrets never go on the command line.
// ---------------------------------------------------------------------------

const DEFAULT_CONFIG = Object.freeze({
    SERVER_VERSION: '1.0.2',
    // Empty means the public default, not an absent secret. Replace before use.
    SERVER_SECRET: 'CHANGE_THIS_SERVER_SECRET',
    // VPN address -> exactly 64 lowercase hex characters (32 bytes).
    ACCESS_KEYS: Object.freeze({
        '10.99.99.2': '0202020202020202020202020202020202020202020202020202020202020202',
        '10.99.99.3': '0303030303030303030303030303030303030303030303030303030303030303',
        '10.99.99.4': '0404040404040404040404040404040404040404040404040404040404040404',
        '10.99.99.5': '0505050505050505050505050505050505050505050505050505050505050505',
        '10.99.99.6': '0606060606060606060606060606060606060606060606060606060606060606',
        '10.99.99.7': '0707070707070707070707070707070707070707070707070707070707070707',
        '10.99.99.8': '0808080808080808080808080808080808080808080808080808080808080808',
        '10.99.99.9': '0909090909090909090909090909090909090909090909090909090909090909',
        '10.99.99.10': '0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a',
        '10.99.99.11': '0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b',
        '10.99.99.12': '0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c',
        '10.99.99.13': '0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d',
        '10.99.99.14': '0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e',
        '10.99.99.15': '0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f',
    }),
    // Disk storage root, relative paths resolve against the directory of this file.
    // Keep it outside any web document root and writable only by the server account.
    BUFFER_DIRECTORY: './phpvpn_data',
    STATUS_PAGE_ENABLED: true,
    VPN_ADDRESS_PREFIX: '10.99.99.',
    VPN_HOST_MIN: 2,
    VPN_HOST_MAX: 254,
    MAX_DATA_RECORDS_PER_REQUEST: 16,
    MAX_AUTH_RECORD_BYTES: 4096,
    MAX_REQUEST_BODY_BYTES: 983040,
    MAX_INFO_PLAINTEXT_BYTES: 262144,
    MAX_INFO_REQUEST_BODY_BYTES: 65536,
    MAX_MASKED_COMMUNICATION_CONTRACTS: 252,
    INFO_TEMP_TTL_SECONDS: 300,
    MAX_QUEUE_BYTES_PER_CLIENT: 16777216,
    AUTH_TIMESTAMP_SKEW_SECONDS: 60,
    QUEUE_TTL_SECONDS: 60,
    LOCK_TIMEOUT_MS: 200,
    MIN_DATA_POLL_INTERVAL_MS: 50,
    MAX_DATA_POLL_INTERVAL_MS: 3200,
    CLIENT_STATS_MIN_UPDATE_SECONDS: 30,
    // Bounded opportunistic maintenance after data responses.
    SWEEP_MIN_INTERVAL_SECONDS: 5,
    SWEEP_MAX_DIRECTORIES: 8,
    SWEEP_MAX_FILES: 256,
    SWEEP_MAX_MILLISECONDS: 20,
    // true adds fsync before publication. Atomic publication survives a process
    // crash either way; fsync only adds protection against power loss.
    SYNC_WRITES: false,
    // Client download links shown on the status page.
    CLIENT_WINDOWS_DOWNLOAD_URL: 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-win-x64.zip',
    CLIENT_MACOS_DOWNLOAD_URL: 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-macos.zip',
    CLIENT_LINUX_DOWNLOAD_URL: 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-linux.zip',
    // HTTP listener. Terminate HTTPS in a reverse proxy, see README.md.
    LISTEN_HOST: '127.0.0.1',
    LISTEN_PORT: 8080,
    SERVER_PATH: '/phpvpn.php',
    REQUEST_TIMEOUT_SECONDS: 30,
});
// End of administrator configuration.

const crypto = require('node:crypto');
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const http = require('node:http');
const path = require('node:path');
const { performance } = require('node:perf_hooks');

const DEFAULT_SECRET = 'CHANGE_THIS_SERVER_SECRET';
const JSON_TYPE = 'application/json; charset=utf-8';
const NDJSON_TYPE = 'application/x-ndjson; charset=utf-8';
const HTML_TYPE = 'text/html; charset=utf-8';
const BATCH_FORMAT = 'phpvpn-node-1';
const MARKER = 'info.json';
const LOCK = '.lock';
const LF = Buffer.from('\n');
// Upper bound for records of one stored batch, independent of later config changes.
const STORED_RECORDS_LIMIT = 1024;
// Oversized bodies are discarded up to this many extra bytes, so ordinary
// clients still read the 413 before the connection is recycled.
const DISCARD_ALLOWANCE_BYTES = 16 * 1024 * 1024;

// ---------------------------------------------------------------------------
// 2. Errors and configuration validation
// ---------------------------------------------------------------------------

const ERROR_CODES = {
    400: 'bad_request', 401: 'unauthorized', 404: 'not_found', 405: 'method_not_allowed',
    413: 'payload_too_large', 415: 'unsupported_media_type', 500: 'internal_error', 503: 'queue_unavailable',
};

class ProtocolError extends Error {
    constructor(status, code, headers) {
        super(code || ERROR_CODES[status]);
        this.status = status;
        this.code = code || ERROR_CODES[status];
        this.headers = headers || {};
    }
}

class ConfigError extends Error {}

/** A storage condition that must surface as 503 queue_unavailable. */
class StorageError extends Error {
    constructor(reason) {
        super(reason);
        this.reason = reason;
    }
}

const INTEGER_KEYS = new Set(Object.keys(DEFAULT_CONFIG).filter((k) => typeof DEFAULT_CONFIG[k] === 'number'));

function isPlainObject(value) {
    return value !== null && typeof value === 'object' && !Array.isArray(value)
        && (Object.getPrototypeOf(value) === Object.prototype || Object.getPrototypeOf(value) === null);
}

function checkOverrideNames(overrides) {
    if (!isPlainObject(overrides)) throw new ConfigError('configuration must be a JSON object');
    for (const key of Object.keys(overrides)) {
        if (!Object.hasOwn(DEFAULT_CONFIG, key)) throw new ConfigError(`unknown setting ${key}`);
    }
}

/** Validated immutable configuration. Construction never touches the filesystem. */
function buildConfig(overrides = {}, baseDirectory = __dirname) {
    checkOverrideNames(overrides);
    const raw = { ...DEFAULT_CONFIG, ...overrides };
    for (const [key, value] of Object.entries(raw)) {
        const expected = key === 'ACCESS_KEYS' ? 'object' : typeof DEFAULT_CONFIG[key];
        if (expected === 'object' ? !isPlainObject(value) : typeof value !== expected) throw new ConfigError(key);
        if (INTEGER_KEYS.has(key) && (!Number.isSafeInteger(value) || value < (key === 'LISTEN_PORT' ? 0 : 1))) {
            throw new ConfigError(key);
        }
        if (typeof value === 'string' && !value.isWellFormed()) throw new ConfigError(key);
    }
    const prefixParts = raw.VPN_ADDRESS_PREFIX.slice(0, -1).split('.');
    if (raw.MIN_DATA_POLL_INTERVAL_MS > raw.MAX_DATA_POLL_INTERVAL_MS
        || raw.MAX_AUTH_RECORD_BYTES < 256
        || raw.MAX_REQUEST_BODY_BYTES < raw.MAX_AUTH_RECORD_BYTES + raw.MAX_DATA_RECORDS_PER_REQUEST * 160
        || raw.MAX_INFO_PLAINTEXT_BYTES < 1024
        || raw.VPN_HOST_MIN > raw.VPN_HOST_MAX || raw.VPN_HOST_MAX > 254
        || !/^(?:[0-9]{1,3}\.){3}$/.test(raw.VPN_ADDRESS_PREFIX)
        || prefixParts.some((p) => Number(p) > 255 || String(Number(p)) !== p)
        || !raw.SERVER_PATH.startsWith('/') || /[?#\s]/.test(raw.SERVER_PATH)
        || raw.LISTEN_HOST === '' || raw.LISTEN_PORT > 65535 || raw.BUFFER_DIRECTORY === '') {
        throw new ConfigError('limits or address');
    }
    const cfg = { ...raw };
    cfg.configuredSecret = raw.SERVER_SECRET;
    // Chapter 3.2: only the exactly empty value is normalized, never "0" or NUL bytes.
    cfg.secret = Buffer.from(raw.SERVER_SECRET === '' ? DEFAULT_SECRET : raw.SERVER_SECRET, 'utf8');
    cfg.accessKeys = new Map();
    for (const [address, key] of Object.entries(raw.ACCESS_KEYS)) {
        if (!isVpnAddress(address, cfg) || typeof key !== 'string' || !/^[0-9a-f]{64}$/.test(key)) {
            throw new ConfigError('access key');
        }
        cfg.accessKeys.set(address, Buffer.from(key, 'hex'));
    }
    cfg.addresses = [...cfg.accessKeys.keys()].sort((a, b) => addressKey(a) - addressKey(b));
    cfg.bufferRoot = path.resolve(baseDirectory, raw.BUFFER_DIRECTORY);
    return Object.freeze(cfg);
}

function isVpnAddress(value, cfg) {
    if (typeof value !== 'string' || !value.startsWith(cfg.VPN_ADDRESS_PREFIX)) return false;
    const host = value.slice(cfg.VPN_ADDRESS_PREFIX.length);
    if (!/^[1-9][0-9]{0,2}$/.test(host)) return false;
    const number = Number(host);
    return number >= cfg.VPN_HOST_MIN && number <= cfg.VPN_HOST_MAX;
}

function addressKey(address) {
    return address.split('.').reduce((acc, part) => acc * 256 + Number(part), 0);
}

function isInsecureSecret(value) {
    return typeof value === 'string' && (value === '' || value === DEFAULT_SECRET
        || /^0+$/.test(value) || /^\0+$/.test(value));
}

// ---------------------------------------------------------------------------
// 3. Time, identifiers and canonical base64url
// ---------------------------------------------------------------------------

const TIMESTAMP_PATTERN = /^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})\.([0-9]{3})Z$/;

function daysFromCivil(year, month, day) {
    year -= month <= 2 ? 1 : 0;
    const era = Math.floor(year / 400);
    const yoe = year - era * 400;
    const doy = Math.floor((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5) + day - 1;
    const doe = yoe * 365 + Math.floor(yoe / 4) - Math.floor(yoe / 100) + doy;
    return era * 146097 + doe - 719468;
}

/** Strict YYYY-MM-DDTHH:MM:SS.sssZ, explicit calendar check, returns epoch ms or null. */
function parseTimestamp(text) {
    const m = typeof text === 'string' ? TIMESTAMP_PATTERN.exec(text) : null;
    if (m === null) return null;
    const [year, month, day, hour, minute, second, milli] = m.slice(1).map(Number);
    const leap = (year % 4 === 0 && year % 100 !== 0) || year % 400 === 0;
    const lengths = [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    if (month < 1 || month > 12 || day < 1 || day > lengths[month - 1] || hour > 23 || minute > 59 || second > 59) {
        return null;
    }
    return (daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second) * 1000 + milli;
}

/** Returns the protocol timestamp, or null when the instant is not representable. */
function formatTimestamp(ms) {
    if (!Number.isFinite(ms)) return null;
    const date = new Date(Math.floor(ms));
    const year = date.getUTCFullYear();
    if (Number.isNaN(year) || year < 0 || year > 9999) return null;
    return date.toISOString();
}

function fileStamp(ms) {
    return formatTimestamp(ms).replace(/:/g, '-');
}

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

function isUuid(value) {
    return typeof value === 'string' && UUID_PATTERN.test(value);
}

const BASE64URL_PATTERN = /^[A-Za-z0-9_-]*$/;

function b64encode(bytes) {
    return Buffer.from(bytes).toString('base64url');
}

/**
 * Chapter 6.1: alphabet, length rule and decode/re-encode equality. The native
 * decoder is only a converter, never the validator. Returns bytes or null.
 */
function b64decode(text, size) {
    if (typeof text !== 'string' || text.length % 4 === 1 || !BASE64URL_PATTERN.test(text)) return null;
    if (size !== undefined && text.length !== Math.floor(size / 3) * 4 + [0, 2, 3][size % 3]) return null;
    const bytes = Buffer.from(text, 'base64url');
    if (bytes.toString('base64url') !== text) return null;
    return bytes;
}

function isCanonicalBase64(text) {
    return b64decode(text) !== null;
}

// ---------------------------------------------------------------------------
// 4. Cryptography, node:crypto. Domains, separators and lengths are normative.
// ---------------------------------------------------------------------------

function sha256(data) {
    return crypto.createHash('sha256').update(data).digest();
}

function sha256hex(data) {
    return crypto.createHash('sha256').update(data).digest('hex');
}

function hmac(key, data) {
    return crypto.createHmac('sha256', key).update(data).digest();
}

function hkdf(ikm, salt, info) {
    return Buffer.from(crypto.hkdfSync('sha256', ikm, salt, info, 32));
}

const DUMMY_ACCESS_KEY = Buffer.from('9b1d36c754a20ef86341d9257c0bae521877f34c2a906dbb05e43981ca6f1728', 'hex');

function semiKey(secret) {
    return hkdf(secret, sha256(Buffer.from('phpvpn/auth-semi/salt')), Buffer.from('phpvpn/auth-semi/key'));
}

function fullAuthSalt(secret, src) {
    return hmac(secret, Buffer.concat([Buffer.from('phpvpn/auth-full/salt\0'), Buffer.from(src, 'ascii')]));
}

function fullAuthKey(secret, accessKey, src) {
    const info = Buffer.concat([Buffer.from('phpvpn/auth-full/key\0'), Buffer.from(src, 'ascii')]);
    return hkdf(accessKey, fullAuthSalt(secret, src), info);
}

function healthKey(secret, requestId) {
    const rid = Buffer.from(requestId, 'ascii');
    const salt = sha256(Buffer.concat([Buffer.from('phpvpn/health-response/salt\0'), rid]));
    return hkdf(secret, salt, Buffer.concat([Buffer.from('phpvpn/health-response/key\0'), rid]));
}

function infoKey(secret, salt, requestId) {
    return hkdf(secret, salt, Buffer.from('phpvpn/info-response/' + requestId, 'ascii'));
}

function gcmSeal(key, iv, plaintext, aad) {
    const cipher = crypto.createCipheriv('aes-256-gcm', key, iv, { authTagLength: 16 });
    cipher.setAAD(aad);
    const ciphertext = Buffer.concat([cipher.update(plaintext), cipher.final()]);
    return { ciphertext, tag: cipher.getAuthTag() };
}

function gcmOpen(key, iv, ciphertext, tag, aad) {
    try {
        const decipher = crypto.createDecipheriv('aes-256-gcm', key, iv, { authTagLength: 16 });
        decipher.setAAD(aad);
        decipher.setAuthTag(tag);
        return Buffer.concat([decipher.update(ciphertext), decipher.final()]);
    } catch {
        return null;
    }
}

/** NIST AES-256-GCM known answers (test cases 13 and 14), run once per process. */
function gcmSelfTest() {
    try {
        const key = Buffer.alloc(32);
        const iv = Buffer.alloc(12);
        const empty = gcmSeal(key, iv, Buffer.alloc(0), Buffer.alloc(0));
        const block = gcmSeal(key, iv, Buffer.alloc(16), Buffer.alloc(0));
        return empty.ciphertext.length === 0
            && empty.tag.toString('hex') === '530f8afbc74536b9a963b4f1c4cb738b'
            && block.ciphertext.toString('hex') === 'cea7403d4d606b6e074ec5d3baf39d18'
            && block.tag.toString('hex') === 'd0d1c8a799996bf0265b98b5d48ab919'
            && gcmOpen(key, iv, block.ciphertext, block.tag, Buffer.alloc(0))?.equals(Buffer.alloc(16)) === true
            && gcmOpen(key, iv, block.ciphertext, Buffer.alloc(16), Buffer.alloc(0)) === null;
    } catch {
        return false;
    }
}

function constantTimeEqual(a, b) {
    return a.length === b.length && crypto.timingSafeEqual(a, b);
}

function semiCanonical(timestamp, requestId) {
    return `PHPVPN-AUTH-SEMI\nPOST\ninfo\n${timestamp}\n${requestId}`;
}

function fullCanonical(action, src, timestamp, requestId, hashHex) {
    return `PHPVPN-AUTH-FULL\nPOST\n${action}\n${src}\n${timestamp}\n${requestId}\n${hashHex}`;
}

function healthAad(requestId, version, timestamp, iv64) {
    return Buffer.from(`PHPVPN-HEALTH-RESPONSE\nhealth\n${requestId}\n${version}\nrunning\nOK\n${timestamp}\n${iv64}`, 'utf8');
}

function infoAad(requestId, salt64, iv64) {
    return Buffer.from(`PHPVPN-INFO\ninfo\n${requestId}\n${salt64}\n${iv64}`, 'ascii');
}

function dataResponseMac(key, requestId, pairs, resultLine) {
    const context = crypto.createHmac('sha256', key);
    context.update(`PHPVPN-DATA-RESPONSE\ndata\n${requestId}\n${pairs.length}\n`, 'ascii');
    for (const [messageId, tag] of pairs) context.update(`${messageId}\n${tag}\n`, 'ascii');
    context.update(sha256hex(resultLine), 'ascii');
    return context.digest();
}

// ---------------------------------------------------------------------------
// 5. Strict JSON and NDJSON wire layer
// ---------------------------------------------------------------------------

/** PHP json_encode default: escape every non-ASCII code unit as \uXXXX. */
function asciiJson(text) {
    let out = '';
    for (let i = 0; i < text.length; i++) {
        const code = text.charCodeAt(i);
        out += code < 0x80 ? text[i] : '\\u' + code.toString(16).padStart(4, '0');
    }
    return out;
}

function compactJson(value) {
    return Buffer.from(asciiJson(JSON.stringify(value)), 'ascii');
}

function prettyJson(value) {
    // Matches PHP JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES: four spaces, "key": value.
    return Buffer.from(asciiJson(JSON.stringify(value, null, 4)), 'ascii');
}

function isJsonWhitespace(code) {
    return code === 0x20 || code === 0x09 || code === 0x0a || code === 0x0d;
}

/**
 * One flat JSON object of plain printable ASCII strings with exactly the given
 * keys. Duplicate, unknown or missing keys, escapes, nested values and trailing
 * bytes are rejected. Operates on raw bytes. Returns an object or null.
 */
function parseFlatObject(bytes, keys) {
    const text = bytes.toString('latin1');
    const length = text.length;
    let i = 0;
    const skip = () => {
        while (i < length && isJsonWhitespace(text.charCodeAt(i))) i++;
    };
    const readString = () => {
        if (text.charCodeAt(i) !== 0x22) return null;
        const start = ++i;
        while (i < length) {
            const code = text.charCodeAt(i);
            if (code === 0x22) return text.slice(start, i++);
            if (code === 0x5c || code < 0x20 || code > 0x7e) return null;
            i++;
        }
        return null;
    };
    if (text.charCodeAt(0) !== 0x7b) return null;
    i = 1;
    const result = Object.create(null);
    skip();
    if (text.charCodeAt(i) === 0x7d) {
        i++;
        return i === length && keys.length === 0 ? result : null;
    }
    for (;;) {
        skip();
        const key = readString();
        if (key === null || !keys.includes(key) || Object.hasOwn(result, key)) return null;
        skip();
        if (text.charCodeAt(i) !== 0x3a) return null;
        i++;
        skip();
        const value = readString();
        if (value === null) return null;
        result[key] = value;
        skip();
        const separator = text.charCodeAt(i++);
        if (separator === 0x2c) continue;
        if (separator === 0x7d) break;
        return null;
    }
    return i === length && Object.keys(result).length === keys.length ? result : null;
}

const UTF8 = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true });

/**
 * Bounded JSON reader for extended info and stored contract markers. Strict
 * UTF-8, no BOM, depth at most 4, no numbers, duplicate decoded names rejected
 * at every level (including escaped spellings). Returns a null-prototype
 * object, or null for anything else.
 */
function parseInfoObject(bytes, maxBytes) {
    if (bytes.length > maxBytes) return null;
    let text;
    try {
        text = UTF8.decode(bytes);
    } catch {
        return null;
    }
    const length = text.length;
    let i = 0;
    const fail = () => {
        throw new SyntaxError('info');
    };
    const skip = () => {
        while (i < length && isJsonWhitespace(text.charCodeAt(i))) i++;
    };
    const value = (depth) => {
        skip();
        if (depth > 4 || i >= length) fail();
        const c = text[i++];
        if (c === '"') {
            const start = i - 1;
            while (i < length) {
                const d = text[i++];
                if (d === '\\') {
                    i++;
                    continue;
                }
                if (d === '"') {
                    const decoded = JSON.parse(text.slice(start, i));
                    if (!decoded.isWellFormed()) fail();
                    return decoded;
                }
            }
            fail();
        }
        if (c === '{' || c === '[') {
            const object = c === '{';
            const end = object ? '}' : ']';
            const result = object ? Object.create(null) : [];
            skip();
            if (text[i] === end) {
                i++;
                return result;
            }
            for (;;) {
                if (object) {
                    const key = value(depth + 1);
                    if (typeof key !== 'string' || key.includes('\0') || Object.hasOwn(result, key)) fail();
                    skip();
                    if (text[i++] !== ':') fail();
                    result[key] = value(depth + 1);
                } else {
                    result.push(value(depth + 1));
                }
                skip();
                const separator = text[i++];
                if (separator === end) return result;
                if (separator !== ',') fail();
            }
        }
        i--;
        for (const [literal, literalValue] of [['true', true], ['false', false], ['null', null]]) {
            if (text.startsWith(literal, i)) {
                i += literal.length;
                return literalValue;
            }
        }
        return fail();
    };
    try {
        const result = value(0);
        skip();
        return i === length && result !== null && typeof result === 'object' && !Array.isArray(result) ? result : null;
    } catch {
        return null;
    }
}

/** Validates masked contracts, returns normalized member order or throws 400/413. */
function maskedContracts(value, src, cfg) {
    if (!Array.isArray(value)) throw new ProtocolError(400);
    if (value.length > Math.min(cfg.MAX_MASKED_COMMUNICATION_CONTRACTS, cfg.VPN_HOST_MAX - cfg.VPN_HOST_MIN)) {
        throw new ProtocolError(413);
    }
    const seen = new Set();
    return value.map((item) => {
        if (item === null || typeof item !== 'object' || Array.isArray(item)
            || Object.keys(item).length !== 2 || typeof item.peer !== 'string' || typeof item.hash !== 'string'
            || !isVpnAddress(item.peer, cfg) || item.peer === src || seen.has(item.peer)
            || !/^[0-9a-f]{64}$/.test(item.hash)) {
            throw new ProtocolError(400);
        }
        seen.add(item.peer);
        return { peer: item.peer, hash: item.hash };
    });
}

const DATA_HEAD = '{"type":"data","messageId":"';

/**
 * Chapter 9.3: the member order is normative, so the record is a sequence of
 * literal comparisons. A wrong source is 401, anything else malformed is 400.
 */
function parseDataRecord(lineBytes, src, cfg) {
    const line = lineBytes.toString('latin1');
    let position = DATA_HEAD.length;
    if (!line.startsWith(DATA_HEAD) || line.length < position + 36) throw new ProtocolError(400);
    const messageId = line.slice(position, position + 36);
    if (!isUuid(messageId)) throw new ProtocolError(400);
    position += 36;
    if (!line.startsWith('","src":"', position)) throw new ProtocolError(400);
    position += 9;
    // A longer src that merely starts with the authenticated address is spoofed too.
    if (!line.startsWith(src + '","dst":"', position)) throw new ProtocolError(401);
    position += src.length + 9;
    const quote = line.indexOf('"', position);
    if (quote < 0) throw new ProtocolError(400);
    const dst = line.slice(position, quote);
    if (!isVpnAddress(dst, cfg)) throw new ProtocolError(400);
    position = quote;
    if (!line.startsWith('","iv":"', position)) throw new ProtocolError(400);
    position += 8;
    if (b64decode(line.slice(position, position + 16), 12) === null) throw new ProtocolError(400);
    position += 16;
    if (!line.startsWith('","tag":"', position)) throw new ProtocolError(400);
    position += 9;
    const tag = line.slice(position, position + 22);
    if (b64decode(tag, 16) === null) throw new ProtocolError(400);
    position += 22;
    if (!line.startsWith('","ciphertext":"', position)) throw new ProtocolError(400);
    position += 16;
    if (line.length < position + 3 || !line.endsWith('"}')) throw new ProtocolError(400);
    const ciphertext = line.slice(position, line.length - 2);
    if (ciphertext === '' || !isCanonicalBase64(ciphertext)) throw new ProtocolError(400);
    return { messageId, dst, tag, line: lineBytes };
}

/** Records after the auth line. Everything is validated before any storage change. */
function parseRecords(rest, src, cfg, limit = cfg.MAX_DATA_RECORDS_PER_REQUEST) {
    if (rest.length === 0) return [];
    if (rest[rest.length - 1] !== 0x0a) throw new ProtocolError(400);
    // Bounded split, a body of line feeds must not allocate one entry per byte.
    const lines = [];
    let start = 0;
    while (start < rest.length) {
        if (lines.length === limit) throw new ProtocolError(413);
        const end = rest.indexOf(0x0a, start);
        lines.push(rest.subarray(start, end));
        start = end + 1;
    }
    const seen = new Set();
    return lines.map((line) => {
        if (line.length === 0 || line.includes(0x0d)) throw new ProtocolError(400);
        const record = parseDataRecord(line, src, cfg);
        if (seen.has(record.messageId)) throw new ProtocolError(400);
        seen.add(record.messageId);
        return record;
    });
}

function mediaTypeMatches(header, expected) {
    if (typeof header !== 'string') return false;
    const parts = header.split(';');
    if (parts[0].trim().toLowerCase() !== expected) return false;
    for (const part of parts.slice(1)) {
        const parameter = part.trim();
        if (parameter.toLowerCase().startsWith('charset=')) {
            const charset = parameter.slice(8).replace(/^"+|"+$/g, '').toLowerCase();
            if (charset !== 'utf-8' && charset !== 'utf8') return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 6. Disk storage. The disk is the only persistent state: no in-memory queue,
//    counter or replay history. Paths derive only from validated addresses.
// ---------------------------------------------------------------------------

const BATCH_NAME = /^([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}-[0-9]{2}-[0-9]{2}\.[0-9]{3}Z)_([1-9][0-9]{0,11})_([0-9.]{7,15})_([0-9a-f-]{36})\.(receiving|data|sending)$/;
const INFO_TEMP_NAME = /^\.info-([0-9a-f-]{36})\.tmp$/;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

/** In-process FIFO mutex with a deadline, in front of the cross-process disk lock. */
class LocalMutex {
    constructor() {
        this.locked = false;
        this.waiters = [];
    }

    tryAcquire() {
        if (this.locked) return false;
        this.locked = true;
        return true;
    }

    acquire(timeoutMs) {
        if (this.tryAcquire()) return Promise.resolve(true);
        return new Promise((resolve) => {
            const waiter = { resolve, timer: null };
            waiter.timer = setTimeout(() => {
                this.waiters.splice(this.waiters.indexOf(waiter), 1);
                resolve(false);
            }, Math.max(0, timeoutMs));
            this.waiters.push(waiter);
        });
    }

    release() {
        const next = this.waiters.shift();
        if (next) {
            clearTimeout(next.timer);
            next.resolve(true);
        } else {
            this.locked = false;
        }
    }
}

class DiskStorage {
    constructor(cfg, { clock = defaultClock, fsImpl = fsp, log = () => {} } = {}) {
        this.cfg = cfg;
        this.clock = clock;
        this.fs = fsImpl;
        this.log = log;
        this.root = cfg.bufferRoot;
        this.queueMutexes = new Map();
        this.statsMutexes = new Map();
        this.lastSweep = -Infinity;
        this.sweeping = false;
        this.sweepCursor = 0;
    }

    directory(address) {
        return path.join(this.root, address.replace(/\./g, '_'));
    }

    static parseBatchName(name, cfg) {
        const m = BATCH_NAME.exec(name);
        if (m === null || !isUuid(m[4]) || !isVpnAddress(m[3], cfg)) return null;
        const stamp = m[1];
        const expires = parseTimestamp(stamp.slice(0, 11) + stamp.slice(11).replace(/-/g, ':'));
        if (expires === null) return null;
        return { expires, bytes: Number(m[2]), src: m[3], state: m[5] };
    }

    mutex(map, address) {
        let mutex = map.get(address);
        if (mutex === undefined) {
            mutex = new LocalMutex();
            map.set(address, mutex);
        }
        return mutex;
    }

    /** Creates the root lazily; failure means the backend cannot be used safely. */
    async ensureRoot() {
        await this.fs.mkdir(this.root, { recursive: true });
        // The administrator may point the root itself at a symlinked directory.
        const stat = await this.fs.stat(this.root);
        if (!stat.isDirectory()) throw new StorageError('root is not a directory');
    }

    /** Creates a validated client directory lazily and refuses symlinks or reparse points. */
    async ensureDirectory(address) {
        const directory = this.directory(address);
        await this.fs.mkdir(directory, { recursive: true });
        const stat = await this.fs.lstat(directory);
        if (!stat.isDirectory() || stat.isSymbolicLink()) throw new StorageError('client directory is not a directory');
        return directory;
    }

    async entries(directory) {
        try {
            return (await this.fs.readdir(directory)).sort();
        } catch (error) {
            if (error.code === 'ENOENT') return [];
            throw error;
        }
    }

    async unlinkQuietly(file) {
        try {
            await this.fs.unlink(file);
        } catch {
            // Best effort, TTL maintenance removes leftovers later.
        }
    }

    /**
     * Cross-process per-destination lock: atomic mkdir of a dedicated directory,
     * single owner, released in finally. A lock is never stolen by age; a lock
     * orphaned by a hard crash needs offline recovery (README.md).
     */
    async withQueueLock(address, directory, action) {
        const deadline = performance.now() + this.cfg.LOCK_TIMEOUT_MS;
        const local = this.mutex(this.queueMutexes, address);
        if (!(await local.acquire(deadline - performance.now()))) throw new ProtocolError(503, 'queue_busy');
        try {
            const lock = path.join(directory, LOCK);
            let wait = 1;
            for (;;) {
                try {
                    await this.fs.mkdir(lock);
                    break;
                } catch (error) {
                    // Windows reports a directory in delete-pending state as EPERM.
                    if (error.code !== 'EEXIST' && !(process.platform === 'win32' && error.code === 'EPERM')) throw error;
                }
                const remaining = deadline - performance.now();
                if (remaining <= 0) throw new ProtocolError(503, 'queue_busy');
                await sleep(Math.min(wait, remaining));
                wait = Math.min(wait * 2, 10);
            }
            try {
                return await action();
            } finally {
                try {
                    await this.fs.rmdir(lock);
                } catch (error) {
                    this.log('lock_release_failed', error.code);
                }
            }
        } finally {
            local.release();
        }
    }

    /** Logical bytes of unexpired, unclaimed batches. Markers, locks and headers do not count. */
    async usage(address, now) {
        let total = 0;
        for (const name of await this.entries(this.directory(address))) {
            const meta = DiskStorage.parseBatchName(name, this.cfg);
            if (meta !== null && meta.expires > now && (meta.state === 'data' || meta.state === 'receiving')) {
                total += meta.bytes;
            }
        }
        return total;
    }

    /** Targeted sweep of one queue: every expired batch in every state. */
    async sweepQueue(address, now) {
        const directory = this.directory(address);
        for (const name of await this.entries(directory)) {
            const meta = DiskStorage.parseBatchName(name, this.cfg);
            if (meta !== null && meta.expires <= now) await this.unlinkQuietly(path.join(directory, name));
        }
    }

    /**
     * Publishes one whole destination group atomically, or nothing. Returns false
     * when capacity is missing after exactly one sweep and one retry.
     */
    async enqueue(dst, src, records) {
        const size = records.reduce((sum, record) => sum + record.line.length + 1, 0);
        const directory = await this.ensureDirectory(dst);
        return this.withQueueLock(dst, directory, async () => {
            const now = this.clock.now();
            if (await this.usage(dst, now) + size > this.cfg.MAX_QUEUE_BYTES_PER_CLIENT) {
                await this.sweepQueue(dst, now);
                if (await this.usage(dst, now) + size > this.cfg.MAX_QUEUE_BYTES_PER_CLIENT) return false;
            }
            await this.writeBatch(directory, src, records, size);
            return true;
        });
    }

    async writeAll(handle, buffer) {
        let offset = 0;
        while (offset < buffer.length) {
            const { bytesWritten } = await handle.write(buffer, offset, buffer.length - offset, null);
            if (!(bytesWritten > 0)) throw new StorageError('short write');
            offset += bytesWritten;
        }
    }

    async writeBatch(directory, src, records, size) {
        const ttl = this.cfg.QUEUE_TTL_SECONDS * 1000;
        const suffix = `_${size}_${src}_${crypto.randomUUID()}`;
        const temp = path.join(directory, fileStamp(this.clock.now() + ttl) + suffix + '.receiving');
        const header = compactJson({ format: BATCH_FORMAT, bytes: size, records: records.map((r) => [r.messageId, r.tag]) });
        const parts = [header, LF];
        for (const record of records) parts.push(record.line, LF);
        const content = Buffer.concat(parts);
        let handle = null;
        let published = false;
        try {
            handle = await this.fs.open(temp, 'wx');
            await this.writeAll(handle, content);
            if (this.cfg.SYNC_WRITES) await handle.sync();
            const closing = handle;
            handle = null;
            await closing.close();
            // Expiry starts at publication, after the complete write and close.
            const final = path.join(directory, fileStamp(this.clock.now() + ttl) + suffix + '.data');
            await this.fs.rename(temp, final);
            published = true;
        } finally {
            if (handle !== null) await handle.close().catch(() => {});
            if (!published) await this.unlinkQuietly(temp);
        }
    }

    /**
     * Claims every unexpired published batch of one initial snapshot. Each claim
     * is an atomic .data -> .sending rename; a batch claimed by someone else is
     * skipped. Claimed files are read, validated and removed before the response
     * is sent and are never restored (at-most-once).
     */
    async drain(address, now) {
        const directory = await this.ensureDirectory(address);
        // Windows lets concurrent renames of one source to one target all succeed,
        // so claims are exclusive only under the per-client lock.
        const claims = await this.withQueueLock(address, directory, async () => {
            const candidates = (await this.fs.readdir(directory))
                .map((name) => ({ name, meta: DiskStorage.parseBatchName(name, this.cfg) }))
                .filter(({ meta }) => meta !== null && meta.state === 'data' && meta.expires > now)
                .sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
            const taken = [];
            for (const { name, meta } of candidates) {
                const from = path.join(directory, name);
                const to = from.slice(0, -'.data'.length) + '.sending';
                try {
                    await this.fs.rename(from, to);
                } catch (error) {
                    // Gone meanwhile, or a transient Windows sharing conflict: stays for a later poll.
                    if (error.code === 'ENOENT' || (process.platform === 'win32'
                        && ['EPERM', 'EACCES', 'EBUSY'].includes(error.code))) continue;
                    if (taken.length === 0) throw error;
                    // Deliver what is already claimed, the rest stays published.
                    this.log('claim_failed', error.code);
                    break;
                }
                taken.push({ file: to, meta });
            }
            return taken;
        });
        const claimed = [];
        let failure = null;
        for (const { file, meta } of claims) {
            let content = null;
            try {
                content = await this.fs.readFile(file);
            } catch (error) {
                if (error.code !== 'ENOENT') failure = error;
            }
            // Removed before the response is sent and never restored, even on failure.
            await this.unlinkQuietly(file);
            if (content === null) continue;
            const batch = this.decodeBatch(content, meta);
            if (batch === null) {
                this.log('damaged_batch_dropped');
                continue;
            }
            claimed.push(batch);
        }
        // An unreadable backend is never reported as an empty queue.
        if (failure !== null && claimed.length === 0) throw failure;
        if (failure !== null) this.log('claimed_read_failed', failure.code);
        return claimed;
    }

    /** Validates the private header against the stored lines. Returns null when damaged. */
    decodeBatch(content, meta) {
        const lf = content.indexOf(0x0a);
        if (lf <= 0 || lf > 65536) return null;
        let header;
        try {
            header = JSON.parse(content.subarray(0, lf).toString('ascii'));
        } catch {
            return null;
        }
        const body = content.subarray(lf + 1);
        if (!isPlainObject(header) || header.format !== BATCH_FORMAT || header.bytes !== meta.bytes
            || body.length !== meta.bytes || !Array.isArray(header.records)
            || header.records.length < 1 || header.records.length > STORED_RECORDS_LIMIT) {
            return null;
        }
        let records;
        try {
            records = parseRecords(body, meta.src, this.cfg, STORED_RECORDS_LIMIT);
        } catch {
            return null;
        }
        if (records.length !== header.records.length) return null;
        for (let i = 0; i < records.length; i++) {
            const pair = header.records[i];
            if (!Array.isArray(pair) || pair.length !== 2 || pair[0] !== records[i].messageId || pair[1] !== records[i].tag) {
                return null;
            }
        }
        return { pairs: header.records.map(([id, tag]) => [id, tag]), body };
    }

    /** Standard info: marker metadata only, never its content, never creates anything. */
    async lastSeen(address) {
        try {
            const stat = await this.fs.stat(path.join(this.directory(address), MARKER));
            return stat.isFile() ? Math.floor(stat.mtimeMs) : null;
        } catch {
            return null;
        }
    }

    /** Extended info: content and mtime from one handle. Damaged content yields []. */
    async snapshot(address) {
        const mutex = this.mutex(this.statsMutexes, address);
        const owned = await mutex.acquire(this.cfg.LOCK_TIMEOUT_MS);
        let seen = null;
        let body = null;
        let handle = null;
        try {
            handle = await this.fs.open(path.join(this.directory(address), MARKER), 'r');
            const stat = await handle.stat();
            if (!stat.isFile()) return { seen: null, contracts: [] };
            seen = Math.floor(stat.mtimeMs);
            const limit = this.cfg.MAX_INFO_REQUEST_BODY_BYTES;
            const buffer = Buffer.alloc(limit + 1);
            let length = 0;
            while (length < buffer.length) {
                const { bytesRead } = await handle.read(buffer, length, buffer.length - length, length);
                if (bytesRead === 0) break;
                length += bytesRead;
            }
            body = buffer.subarray(0, length);
        } catch {
            return { seen, contracts: [] };
        } finally {
            if (handle !== null) await handle.close().catch(() => {});
            if (owned) mutex.release();
        }
        const object = parseInfoObject(body, this.cfg.MAX_INFO_REQUEST_BODY_BYTES);
        if (object !== null && Object.keys(object).length === 1 && Object.hasOwn(object, 'maskedCommunicationContracts')) {
            try {
                return { seen, contracts: maskedContracts(object.maskedCommunicationContracts, address, this.cfg) };
            } catch {
                // Invalid stored content is reported as an empty list.
            }
        }
        return { seen, contracts: [] };
    }

    /**
     * Confirmed full-auth publication: unique temp file, complete write, mtime,
     * then atomic replace without unlinking the old marker. Errors throw (503)
     * and keep the old marker.
     */
    async publishContracts(address, contracts, now) {
        const body = compactJson({ maskedCommunicationContracts: contracts });
        if (body.length > this.cfg.MAX_INFO_REQUEST_BODY_BYTES) throw new StorageError('marker too large');
        const mutex = this.mutex(this.statsMutexes, address);
        if (!(await mutex.acquire(this.cfg.LOCK_TIMEOUT_MS))) throw new StorageError('marker busy');
        try {
            const directory = await this.ensureDirectory(address);
            const temp = path.join(directory, `.info-${crypto.randomUUID()}.tmp`);
            let handle = null;
            let replaced = false;
            try {
                handle = await this.fs.open(temp, 'wx');
                await this.writeAll(handle, body);
                if (this.cfg.SYNC_WRITES) await handle.sync();
                const closing = handle;
                handle = null;
                await closing.close();
                await this.fs.utimes(temp, now / 1000, now / 1000);
                await this.fs.rename(temp, path.join(directory, MARKER));
                replaced = true;
            } finally {
                if (handle !== null) await handle.close().catch(() => {});
                if (!replaced) await this.unlinkQuietly(temp);
            }
        } finally {
            mutex.release();
        }
    }

    /**
     * Best-effort lastSeen: one metadata read, at most one touch, no retry, no
     * waiting, no error. A missing marker is created empty without truncation.
     */
    async touch(address, now) {
        const mutex = this.mutex(this.statsMutexes, address);
        if (!mutex.tryAcquire()) return;
        try {
            const directory = await this.ensureDirectory(address);
            const marker = path.join(directory, MARKER);
            let seen = null;
            try {
                seen = Math.floor((await this.fs.stat(marker)).mtimeMs);
            } catch {
                seen = null;
            }
            if (seen !== null && now - seen <= this.cfg.CLIENT_STATS_MIN_UPDATE_SECONDS * 1000) return;
            if (seen === null) {
                const handle = await this.fs.open(marker, 'a');
                await handle.close();
            }
            await this.fs.utimes(marker, now / 1000, now / 1000);
        } catch (error) {
            this.log('stats_touch_skipped', error.code);
        } finally {
            mutex.release();
        }
    }

    /** Bounded opportunistic maintenance: time, directory and file budgets. */
    async sweep() {
        const started = performance.now();
        if (this.sweeping || started - this.lastSweep < this.cfg.SWEEP_MIN_INTERVAL_SECONDS * 1000) return;
        this.sweeping = true;
        this.lastSweep = started;
        try {
            const deadline = started + this.cfg.SWEEP_MAX_MILLISECONDS;
            const addresses = (await this.entries(this.root))
                .map((name) => name.replace(/_/g, '.'))
                .filter((address) => isVpnAddress(address, this.cfg));
            let files = 0;
            for (let visited = 0; visited < Math.min(addresses.length, this.cfg.SWEEP_MAX_DIRECTORIES); visited++) {
                if (files >= this.cfg.SWEEP_MAX_FILES || performance.now() > deadline) return;
                const address = addresses[this.sweepCursor % addresses.length];
                this.sweepCursor = (this.sweepCursor + 1) % addresses.length;
                const directory = this.directory(address);
                const now = this.clock.now();
                for (const name of await this.entries(directory)) {
                    if (files >= this.cfg.SWEEP_MAX_FILES || performance.now() > deadline) return;
                    files++;
                    const temp = INFO_TEMP_NAME.exec(name);
                    if (temp !== null && isUuid(temp[1])) {
                        try {
                            const stat = await this.fs.stat(path.join(directory, name));
                            if (now - stat.mtimeMs > this.cfg.INFO_TEMP_TTL_SECONDS * 1000) {
                                await this.unlinkQuietly(path.join(directory, name));
                            }
                        } catch {
                            // Vanished or unreadable, try again in a later sweep.
                        }
                        continue;
                    }
                    const meta = DiskStorage.parseBatchName(name, this.cfg);
                    if (meta !== null && meta.expires <= now) await this.unlinkQuietly(path.join(directory, name));
                }
            }
        } catch (error) {
            this.log('sweep_skipped', error.code);
        } finally {
            this.sweeping = false;
        }
    }

    /**
     * Offline recovery after a hard crash, only while every process using this
     * root is stopped: removes lock directories and unpublished (.receiving) or
     * already claimed (.sending) batches. Published .data batches are kept.
     */
    async offlineRecovery() {
        const summary = { locks: 0, receiving: 0, sending: 0 };
        for (const entry of await this.entries(this.root)) {
            const address = entry.replace(/_/g, '.');
            if (!isVpnAddress(address, this.cfg)) continue;
            const directory = this.directory(address);
            const stat = await this.fs.lstat(directory);
            if (!stat.isDirectory()) continue;
            for (const name of await this.entries(directory)) {
                if (name === LOCK) {
                    await this.fs.rmdir(path.join(directory, name));
                    summary.locks++;
                    continue;
                }
                const meta = DiskStorage.parseBatchName(name, this.cfg);
                if (meta !== null && (meta.state === 'receiving' || meta.state === 'sending')) {
                    await this.fs.unlink(path.join(directory, name));
                    summary[meta.state]++;
                }
            }
        }
        return summary;
    }
}

// ---------------------------------------------------------------------------
// 7. Handlers
// ---------------------------------------------------------------------------

const defaultClock = Object.freeze({ now: () => Date.now() });

const STATUS_HTML = "<!doctype html><html lang=en><meta name=viewport content=\"width=device-width\"><title>VPN server is running</title><style>*{box-sizing:border-box}body{margin:0;padding:2rem 1rem;min-height:100vh;display:flex;flex-direction:column;align-items:center;background:#eee;font:16px Arial,sans-serif}main{width:920px;max-width:100%;padding:1.5rem;background:#fff;border-radius:1rem;box-shadow:0 .4rem 1.5rem #0003}h1,h2{margin-top:0}aside{padding:1rem;background:#fee;color:#900;border:1px solid #e00;border-radius:.6rem}button{padding:.6rem;border:0;border-radius:.5rem;background:#06c;color:#fff;cursor:pointer}aside button{background:#e00;color:#fff}hr{border:0;border-top:1px solid #ddd;margin:1.5rem 0}.setup{display:grid;grid-template-columns:minmax(0,1fr) 240px;gap:1.5rem;align-items:start}.setup img{width:100%;max-width:240px;height:auto;border:1px solid #bbb}.setup p:first-child{margin-top:0}.setup li{margin:.75rem 0}#u{display:block;margin:.5rem 0;padding:.75rem;background:#eee;border-radius:.4rem;font-family:monospace;overflow-wrap:anywhere}@media(max-width:640px){.setup{grid-template-columns:1fr}}.downloads{display:flex;flex-wrap:wrap;gap:.75rem}.downloads a{padding:.6rem 1rem;border-radius:.5rem;background:#06c;color:#fff;text-decoration:none}#r:not([hidden]){display:flex;gap:.5rem}#k{min-width:0;flex:1;font-family:monospace}dialog{width:900px;max-width:calc(100% - 2rem);border:0;border-radius:1rem;padding:1.5rem}dialog::backdrop{background:#0006}#x,#z{float:right;padding:0 .5rem;background:none;color:#000;font-size:1.5rem}#t{width:100%;height:14rem;font-family:monospace;white-space:pre}#b,#a{text-align:right}#f{color:#777;font-size:.9rem}#f a{color:inherit}</style><main><h1>VPN server is running</h1><p>This server implements the Packet Handoff Protocol for virtual private networks (PHPVPN). The client application can be configured by following the instructions below.</p><aside><b>Warning:</b> The default value of SERVER_SECRET is still being used. The SERVER_SECRET setting in the server script or its JSON configuration file must be set to a secure value immediately. A secure value can be created using the Generate key button below.<p>Secure hexadecimal key: <button id=g>Generate key</button></p><div id=r hidden><input id=k aria-label=\"Generated key\" readonly><button id=c>Copy key</button></div></aside><hr><section class=setup><div><p><strong>Step 1 - Download and install the client app</strong></p><p>To connect to this VPN server, the client application for your operating system must first be downloaded and installed.</p><div class=downloads><a href=\"__CLIENT_WINDOWS_URL__\">Windows</a><a href=\"__CLIENT_MACOS_URL__\">macOS</a><a href=\"__CLIENT_LINUX_URL__\">Linux</a></div><p>After installation, the PHPVPN client app should be launched. On Windows, it can be opened from the system tray or located by searching for phpvpn.exe in the Start menu. A similar procedure can be followed on macOS and Linux.</p></div><img src=\"data:image/png;base64,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\" alt=\"Windows system tray menu with the Configuration Wizard and Edit Config options highlighted\" width=240 height=180></section><hr><section><p><strong>Step 2A - Configure the client app</strong></p><p>If a configuration file has been provided by your administrator, its contents can be pasted into the configuration form accessed through <b>Edit Config</b>.</p><p><strong>Step 2B - Use the Configuration Wizard</strong></p><p>If no configuration file has been provided, the <b>Configuration Wizard</b> can be opened from the client menu. The following steps should then be completed:</p><ul><li>The VPN server URL should be set to the address of this page:<code id=u></code></li><li>The connection to the server can be checked by clicking the &quot;Verify &amp; continue&quot; button in the Configuration Wizard. If the connection fails, the HTTP request headers can be viewed using <button id=s>Show HTTP Headers</button> and then copied into the Configuration Wizard.</li><li>The correct Server Secret should be entered, or the default used if applicable.</li><li>The correct IP Address and the corresponding Access Token should be entered, or the defaults used if applicable.</li><li>The default Communication Contracts should be generated.</li><li>The Server Secret and, where required, the IP Address and Access Token should be provided by your administrator.</li></ul></section><noscript>JavaScript must be enabled so that a key can be generated and copied and HTTP headers can be viewed.</noscript></main><p id=f>Developed by <a href=https://www.pilandrome.com>www.pilandrome.com</a> (MIT License)</p><dialog id=d><form method=dialog><button id=x aria-label=Close>&times;</button><h2>HTTP headers</h2><p>The HTTP request headers received by this script may be incomplete. For exact values, your browser&apos;s developer tools can be opened with F12, and the request headers can be copied from the Network tab.</p><textarea id=t aria-label=\"HTTP headers\" readonly></textarea><p id=b><button id=y type=button>Copy</button> <button>OK</button></p></form></dialog><dialog id=j aria-labelledby=v><form method=dialog><button id=z aria-label=Close>&times;</button><h2 id=v>SERVER_SECRET copied successfully</h2><p>The key generated by JavaScript in the web browser has been copied to the clipboard. You should now replace the value of SERVER_SECRET in the server script or its JSON configuration file with this key to improve the security of the VPN server.</p><p id=a><button>OK</button></p></form></dialog><script>document.getElementById('u').textContent=location.href;C=async e=>{e.select();try{if(navigator.clipboard){await navigator.clipboard.writeText(e.value);return true}}catch{}try{e.select();return document.execCommand('copy')}catch{return false}};if(document.getElementById('g')){g.onclick=()=>{k.value=[...crypto.getRandomValues(new Uint8Array(32))].map(x=>x.toString(16).padStart(2,0)).join('');r.hidden=false};c.onclick=async()=>{if(await C(k))j.showModal()}}s.onclick=()=>{d.showModal();t.select()};y.onclick=()=>C(t)</script>";
// The client owns these names and its HTTP headers dialog rejects them.
const CLIENT_MANAGED_HEADERS = /^(host|connection|content-(type|length)|transfer-encoding)$/i;
const HTML_ESCAPES = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };

/** Escaped `Name: value` lines of the request headers for the Show HTTP Headers dialog. */
function statusHeaders(rawHeaders) {
    const merged = new Map();
    for (let i = 0; i + 1 < rawHeaders.length; i += 2) {
        const key = rawHeaders[i].toLowerCase();
        // Node decodes header bytes as latin1, the dialog shows them as UTF-8 like PHP and Python.
        const value = Buffer.from(rawHeaders[i + 1], 'latin1').toString('utf8');
        const seen = merged.get(key);
        merged.set(key, seen ? [seen[0], seen[1] + (key === 'cookie' ? '; ' : ', ') + value] : [rawHeaders[i], value]);
    }
    return [...merged].filter(([key]) => !CLIENT_MANAGED_HEADERS.test(key))
        .map(([, [name, value]]) => `${name}: ${value}`).join('\n').replace(/[&<>"']/g, (c) => HTML_ESCAPES[c]);
}

function response(status, contentType, body, headers = {}) {
    return { status, contentType, body: Array.isArray(body) ? body : [body], headers };
}

function errorResponse(error) {
    const headers = { ...error.headers };
    if (error.status === 503) headers['Retry-After'] = '1';
    return response(error.status, JSON_TYPE, compactJson({ type: 'error', code: error.code }), headers);
}

class Application {
    constructor({ config, overrides, baseDirectory = __dirname, clock = defaultClock,
        randomBytes = crypto.randomBytes, storage, fsImpl, log = () => {} } = {}) {
        this.clock = clock;
        this.randomBytes = randomBytes;
        this.log = log;
        this.raw = { ...DEFAULT_CONFIG, ...(overrides || {}) };
        this.configError = false;
        try {
            this.cfg = config || buildConfig(overrides || {}, baseDirectory);
            this.raw = this.cfg;
        } catch (error) {
            if (!(error instanceof ConfigError)) throw error;
            this.cfg = null;
            this.configError = true;
            log('server_misconfigured');
        }
        this.cryptoReady = gcmSelfTest();
        this.storage = storage || (this.cfg ? new DiskStorage(this.cfg, { clock, fsImpl, log }) : null);
    }

    /** Routing and checks that precede reading the body. Returns a response or the body limit. */
    preflight(method, rawUrl, contentType, rawHeaders = []) {
        const question = rawUrl.indexOf('?');
        const requestPath = question < 0 ? rawUrl : rawUrl.slice(0, question);
        if (requestPath !== this.raw.SERVER_PATH) throw new ProtocolError(404);
        const params = new URLSearchParams(question < 0 ? '' : rawUrl.slice(question + 1));
        let action = params.has('action') ? params.getAll('action').at(-1) : null;
        // PHP turns action[]=x into an array, which never names an action.
        for (const key of params.keys()) if (key.startsWith('action[')) action = '\0invalid';
        if (action === null || action === '') {
            if (method !== 'GET' && method !== 'HEAD') throw new ProtocolError(405, null, { Allow: 'GET, HEAD' });
            if (!this.raw.STATUS_PAGE_ENABLED) throw new ProtocolError(404);
            let html = isInsecureSecret(this.raw.SERVER_SECRET ?? this.raw.configuredSecret)
                ? STATUS_HTML : STATUS_HTML.replace(/<aside>.*?<\/aside>/s, '');
            html = html.replace(/__CLIENT_(WINDOWS|MACOS|LINUX)_URL__/g, (_, platform) =>
                String(this.raw[`CLIENT_${platform}_DOWNLOAD_URL`]).replace(/[&<>"']/g, (c) => HTML_ESCAPES[c]));
            // A replacer function keeps $ patterns in header values literal.
            const page = html.replace('readonly></textarea>', () => `readonly>${statusHeaders(rawHeaders)}</textarea>`);
            return { response: response(200, HTML_TYPE, Buffer.from(page, 'utf8')) };
        }
        if (action !== 'health' && action !== 'info' && action !== 'data') throw new ProtocolError(404);
        if (method !== 'POST') throw new ProtocolError(405, null, { Allow: 'POST' });
        if (this.configError) throw new ProtocolError(503, 'server_misconfigured');
        const cfg = this.cfg;
        if (!mediaTypeMatches(contentType, action === 'data' ? 'application/x-ndjson' : 'application/json')) {
            throw new ProtocolError(415);
        }
        if (action === 'info' && (cfg.MAX_INFO_REQUEST_BODY_BYTES > cfg.MAX_REQUEST_BODY_BYTES
            || cfg.MAX_INFO_REQUEST_BODY_BYTES < cfg.MAX_AUTH_RECORD_BYTES)) {
            throw new ProtocolError(503, 'server_misconfigured');
        }
        const limit = { health: cfg.MAX_AUTH_RECORD_BYTES, info: cfg.MAX_INFO_REQUEST_BODY_BYTES,
            data: cfg.MAX_REQUEST_BODY_BYTES }[action];
        return { action, limit };
    }

    async dispatch(action, body) {
        try {
            if (action !== 'data' && !this.cryptoReady) throw new ProtocolError(500);
            if (action === 'health') return this.health(body);
            if (action === 'info') return await this.info(body);
            return await this.data(body);
        } catch (error) {
            return this.failure(error);
        }
    }

    failure(error) {
        if (error instanceof ProtocolError) return { response: errorResponse(error) };
        if (error instanceof StorageError || (error && typeof error.syscall === 'string')) {
            this.log('queue_unavailable', error.code || error.reason);
            return { response: errorResponse(new ProtocolError(503, 'queue_unavailable')) };
        }
        this.log('internal_error', error && error.name);
        return { response: errorResponse(new ProtocolError(500)) };
    }

    health(body) {
        const fields = parseFlatObject(body, ['type', 'requestId']);
        if (fields === null || fields.type !== 'auth-none' || !isUuid(fields.requestId)) throw new ProtocolError(400);
        const rid = fields.requestId;
        const timestamp = formatTimestamp(this.clock.now());
        const iv = this.randomBytes(12);
        const iv64 = b64encode(iv);
        const version = this.cfg.SERVER_VERSION;
        const { tag } = gcmSeal(healthKey(this.cfg.secret, rid), iv, Buffer.alloc(0), healthAad(rid, version, timestamp, iv64));
        return { response: response(200, JSON_TYPE, compactJson({ serverVersion: version, status: 'running',
            description: 'OK', timestamp, requestId: rid, iv: iv64, tag: b64encode(tag) })) };
    }

    /** Checks timestamp format, tag encoding and skew in the reference order. */
    checkFreshness(fields) {
        const stamp = parseTimestamp(fields.timestamp);
        const provided = b64decode(fields.tag, 32);
        if (stamp === null || provided === null) throw new ProtocolError(400);
        if (Math.abs(stamp - this.clock.now()) > this.cfg.AUTH_TIMESTAMP_SKEW_SECONDS * 1000) throw new ProtocolError(401);
        return provided;
    }

    /** Unknown sources run the same derivation with a fixed dummy key, then 401. */
    authenticateFull(action, fields, hashHex, provided) {
        const access = this.cfg.accessKeys.get(fields.src);
        const key = fullAuthKey(this.cfg.secret, access || DUMMY_ACCESS_KEY, fields.src);
        const expected = hmac(key, Buffer.from(fullCanonical(action, fields.src, fields.timestamp, fields.requestId, hashHex), 'ascii'));
        const valid = constantTimeEqual(expected, provided);
        if (access === undefined || !valid) throw new ProtocolError(401);
        return key;
    }

    async info(body) {
        const cfg = this.cfg;
        const object = parseInfoObject(body, cfg.MAX_INFO_REQUEST_BODY_BYTES);
        const extended = object !== null && (Object.hasOwn(object, 'maskedCommunicationContracts')
            || Object.hasOwn(object, 'includeMaskedCommunicationContracts'));
        if (!extended && body.length > cfg.MAX_AUTH_RECORD_BYTES) throw new ProtocolError(413);
        const required = extended ? ['type', 'src', 'timestamp', 'requestId', 'tag'] : ['type', 'timestamp', 'requestId', 'tag'];
        const fields = extended ? object : parseFlatObject(body, required);
        const allowed = [...required, 'maskedCommunicationContracts', 'includeMaskedCommunicationContracts'];
        if (fields === null || required.some((key) => typeof fields[key] !== 'string')
            || Object.keys(fields).some((key) => !allowed.includes(key))
            || fields.type !== (extended ? 'auth-full' : 'auth-semi') || !isUuid(fields.requestId)) {
            throw new ProtocolError(400);
        }
        const extensions = {};
        if (extended) {
            if (!isVpnAddress(fields.src, cfg)) throw new ProtocolError(401);
            if (Object.hasOwn(fields, 'maskedCommunicationContracts')) {
                extensions.maskedCommunicationContracts = maskedContracts(fields.maskedCommunicationContracts, fields.src, cfg);
            }
            if (Object.hasOwn(fields, 'includeMaskedCommunicationContracts')) {
                if (typeof fields.includeMaskedCommunicationContracts !== 'boolean') throw new ProtocolError(400);
                extensions.includeMaskedCommunicationContracts = fields.includeMaskedCommunicationContracts;
            }
        }
        const provided = this.checkFreshness(fields);
        if (extended) {
            this.authenticateFull('info', fields, sha256hex(compactJson(extensions)), provided);
            if (extensions.maskedCommunicationContracts !== undefined) {
                await this.storage.publishContracts(fields.src, extensions.maskedCommunicationContracts, this.clock.now());
            } else {
                await this.storage.touch(fields.src, this.clock.now());
            }
        } else {
            const expected = hmac(semiKey(cfg.secret), Buffer.from(semiCanonical(fields.timestamp, fields.requestId), 'ascii'));
            if (!constantTimeEqual(expected, provided)) throw new ProtocolError(401);
        }
        const plaintext = await this.buildInfo(extensions.includeMaskedCommunicationContracts === true ? fields.src : null);
        const rid = fields.requestId;
        const salt = this.randomBytes(32);
        const iv = this.randomBytes(12);
        const salt64 = b64encode(salt);
        const iv64 = b64encode(iv);
        const sealed = gcmSeal(infoKey(cfg.secret, salt, rid), iv, plaintext, infoAad(rid, salt64, iv64));
        return { response: response(200, JSON_TYPE, compactJson({ type: 'info', requestId: rid, salt: salt64, iv: iv64,
            ciphertext: b64encode(sealed.ciphertext), tag: b64encode(sealed.tag) })) };
    }

    async buildInfo(requester) {
        const cfg = this.cfg;
        const clients = [];
        for (const address of cfg.addresses) {
            const snapshot = requester === null ? { seen: await this.storage.lastSeen(address) } : await this.storage.snapshot(address);
            const lastSeen = snapshot.seen === null ? null : formatTimestamp(snapshot.seen);
            if (lastSeen === null) continue;
            const client = { address, lastSeen };
            if (requester !== null) {
                client.maskedCommunicationContracts = address === requester
                    ? [] : snapshot.contracts.filter((contract) => contract.peer === requester).slice(0, 1);
            }
            clients.push(client);
        }
        const limits = {
            maxDataRecordsPerRequest: cfg.MAX_DATA_RECORDS_PER_REQUEST,
            maxRequestBodyBytes: cfg.MAX_REQUEST_BODY_BYTES,
            maxQueueBytesPerClient: cfg.MAX_QUEUE_BYTES_PER_CLIENT,
            queueTtlSeconds: cfg.QUEUE_TTL_SECONDS,
            minDataPollIntervalMs: cfg.MIN_DATA_POLL_INTERVAL_MS,
            maxDataPollIntervalMs: cfg.MAX_DATA_POLL_INTERVAL_MS,
        };
        const generatedAt = formatTimestamp(this.clock.now());
        for (;;) {
            const plaintext = prettyJson({ serverVersion: cfg.SERVER_VERSION, generatedAt, clients, limits });
            if (plaintext.length <= cfg.MAX_INFO_PLAINTEXT_BYTES) return plaintext;
            if (clients.length === 0) throw new ProtocolError(503, 'server_misconfigured');
            clients.pop();
        }
    }

    async data(body) {
        const cfg = this.cfg;
        const lf = body.indexOf(0x0a);
        if ((lf < 0 ? body.length + 1 : lf + 1) > cfg.MAX_AUTH_RECORD_BYTES) throw new ProtocolError(413);
        if (lf < 0) throw new ProtocolError(400);
        const auth = parseFlatObject(body.subarray(0, lf), ['type', 'src', 'timestamp', 'requestId', 'tag']);
        if (auth === null || auth.type !== 'auth-full' || !isUuid(auth.requestId) || !isVpnAddress(auth.src, cfg)) {
            throw new ProtocolError(400);
        }
        const provided = this.checkFreshness(auth);
        const rest = body.subarray(lf + 1);
        const src = auth.src;
        const rid = auth.requestId;
        const key = this.authenticateFull('data', auth, sha256hex(rest), provided);
        const records = parseRecords(rest, src, cfg);

        // Storage changes only after complete validation and authentication.
        await this.storage.ensureRoot();
        const groups = new Map();
        for (const record of records) {
            if (!groups.has(record.dst)) groups.set(record.dst, []);
            groups.get(record.dst).push(record);
        }
        const accepted = new Set();
        let acceptedBytes = 0;
        for (const dst of [...groups.keys()].sort((a, b) => addressKey(a) - addressKey(b))) {
            const group = groups.get(dst);
            if (await this.storage.enqueue(dst, src, group)) {
                for (const record of group) {
                    accepted.add(record.messageId);
                    acceptedBytes += record.line.length + 1;
                }
            }
        }
        await this.storage.touch(src, this.clock.now());
        // Drained even when every outgoing group was deferred, chapter 10.2.
        const claimed = await this.storage.drain(src, this.clock.now());
        const resultLine = compactJson({
            type: 'data-result', requestId: rid,
            acceptedMessageIds: records.filter((r) => accepted.has(r.messageId)).map((r) => r.messageId),
            deferredMessageIds: records.filter((r) => !accepted.has(r.messageId)).map((r) => r.messageId),
            acceptedRecords: accepted.size, acceptedBytes,
        });
        const pairs = claimed.flatMap((batch) => batch.pairs);
        const macLine = compactJson({ type: 'data-mac', requestId: rid, tag: b64encode(dataResponseMac(key, rid, pairs, resultLine)) });
        const chunks = claimed.map((batch) => batch.body);
        chunks.push(resultLine, LF, macLine, LF);
        return { response: response(200, NDJSON_TYPE, chunks), sweep: true };
    }
}

// ---------------------------------------------------------------------------
// 8. HTTP adapter
// ---------------------------------------------------------------------------

const PROTOCOL_HEADERS = {
    'Cache-Control': 'no-store, no-cache, must-revalidate',
    Pragma: 'no-cache',
    'X-Content-Type-Options': 'nosniff',
};

function waitWritable(res) {
    return new Promise((resolve) => {
        const done = (ok) => {
            res.off('drain', onDrain);
            res.off('close', onClose);
            resolve(ok);
        };
        const onDrain = () => done(true);
        const onClose = () => done(false);
        res.on('drain', onDrain);
        res.on('close', onClose);
    });
}

async function emit(res, result, head, extraHeaders = {}) {
    const { status, contentType, body, headers } = result;
    const length = body.reduce((sum, chunk) => sum + chunk.length, 0);
    res.writeHead(status, {
        'Content-Type': contentType, ...PROTOCOL_HEADERS, ...headers, ...extraHeaders, 'Content-Length': String(length),
    });
    if (head) {
        res.end();
        return;
    }
    for (const chunk of body) {
        if (res.destroyed) return;
        if (!res.write(chunk) && !(await waitWritable(res))) return;
    }
    res.end();
}

/** Reads at most `limit` bytes. Larger bodies are discarded up to an allowance, then 413. */
function readBody(req, limit) {
    return new Promise((resolve, reject) => {
        const declared = req.headers['content-length'];
        if (declared !== undefined && Number(declared) > limit + DISCARD_ALLOWANCE_BYTES) {
            reject(Object.assign(new ProtocolError(413), { closeConnection: true }));
            return;
        }
        const chunks = [];
        let total = 0;
        let tooLarge = declared !== undefined && Number(declared) > limit;
        req.on('data', (chunk) => {
            total += chunk.length;
            if (total > limit) tooLarge = true;
            if (tooLarge) {
                chunks.length = 0;
                if (total > limit + DISCARD_ALLOWANCE_BYTES) {
                    req.removeAllListeners('data');
                    req.pause();
                    reject(Object.assign(new ProtocolError(413), { closeConnection: true }));
                }
                return;
            }
            chunks.push(chunk);
        });
        req.on('end', () => (tooLarge ? reject(new ProtocolError(413)) : resolve(Buffer.concat(chunks, total))));
        req.on('error', () => reject(new Error('aborted')));
        req.on('close', () => {
            if (!req.complete) reject(new Error('aborted'));
        });
    });
}

function createHttpServer(app) {
    const timeout = (app.cfg ? app.cfg.REQUEST_TIMEOUT_SECONDS : DEFAULT_CONFIG.REQUEST_TIMEOUT_SECONDS) * 1000;
    const server = http.createServer({ requestTimeout: timeout, headersTimeout: Math.min(timeout, 20000),
        keepAliveTimeout: 5000, maxHeaderSize: 16384 }, (req, res) => {
        handleRequest(app, req, res).catch(() => {
            if (!res.headersSent) {
                emit(res, errorResponse(new ProtocolError(500)), req.method === 'HEAD').catch(() => res.destroy());
            } else {
                res.destroy();
            }
        });
    });
    server.on('clientError', (error, socket) => {
        if (!socket.writable || error.code === 'ECONNRESET') {
            socket.destroy();
            return;
        }
        const body = '{"type":"error","code":"bad_request"}';
        socket.end(`HTTP/1.1 400 Bad Request\r\nContent-Type: ${JSON_TYPE}\r\nCache-Control: ${PROTOCOL_HEADERS['Cache-Control']}\r\n`
            + `Pragma: no-cache\r\nX-Content-Type-Options: nosniff\r\nContent-Length: ${body.length}\r\nConnection: close\r\n\r\n${body}`);
    });
    return server;
}

async function handleRequest(app, req, res) {
    const head = req.method === 'HEAD';
    let result;
    try {
        const pre = app.preflight(req.method, req.url || '/', req.headers['content-type'], req.rawHeaders);
        if (pre.response) {
            result = { response: pre.response };
        } else {
            const body = await readBody(req, pre.limit);
            result = await app.dispatch(pre.action, body);
        }
    } catch (error) {
        if (error instanceof ProtocolError) {
            await emit(res, errorResponse(error), head, error.closeConnection ? { Connection: 'close' } : {});
            return;
        }
        if (error && error.message === 'aborted') return;
        throw error;
    }
    await emit(res, result.response, head);
    if (result.sweep) app.storage.sweep().catch(() => {});
}

// ---------------------------------------------------------------------------
// 9. Command line
// ---------------------------------------------------------------------------

function log(code, detail) {
    // Codes only: never secrets, tags, ciphertext, requests or internal paths.
    process.stderr.write(`${new Date().toISOString()} phpvpn ${code}${detail ? ' ' + detail : ''}\n`);
}

function loadOverrides(file) {
    let parsed;
    try {
        parsed = JSON.parse(fs.readFileSync(file, 'utf8'));
    } catch {
        throw new ConfigError('configuration file is not readable JSON');
    }
    checkOverrideNames(parsed);
    return parsed;
}

function parseArguments(argv) {
    const options = { config: null, mode: 'serve' };
    for (let i = 0; i < argv.length; i++) {
        const argument = argv[i];
        if (argument === '--config' && i + 1 < argv.length) options.config = argv[++i];
        else if (argument === '--check-config') options.mode = 'check';
        else if (argument === '--offline-recovery') options.mode = 'recover';
        else throw new ConfigError(`unknown argument ${argument}`);
    }
    return options;
}

async function main(argv = process.argv.slice(2)) {
    let options;
    let overrides = {};
    try {
        options = parseArguments(argv);
        if (options.config !== null) overrides = loadOverrides(options.config);
    } catch (error) {
        log('configuration_rejected', error.message);
        process.exitCode = 2;
        return;
    }
    if (options.mode !== 'serve') {
        let cfg;
        try {
            cfg = buildConfig(overrides);
        } catch (error) {
            log('configuration_invalid', error.message);
            process.exitCode = 2;
            return;
        }
        if (options.mode === 'check') {
            log('configuration_valid');
            return;
        }
        const summary = await new DiskStorage(cfg).offlineRecovery();
        log('offline_recovery_done', `locks=${summary.locks} receiving=${summary.receiving} sending=${summary.sending}`);
        return;
    }
    const app = new Application({ overrides, log });
    if (!app.cryptoReady) log('crypto_self_test_failed');
    const server = createHttpServer(app);
    const host = app.raw.LISTEN_HOST;
    const port = app.raw.LISTEN_PORT;
    server.on('error', (error) => {
        log('listen_failed', error.code);
        process.exit(1);
    });
    server.listen(port, host, () => {
        const address = server.address();
        log('listening', `http://${host.includes(':') ? `[${host}]` : host}:${address.port}${app.raw.SERVER_PATH}`);
    });
    const shutdown = () => {
        log('shutting_down');
        server.close(() => process.exit(0));
        server.closeIdleConnections();
        setTimeout(() => process.exit(0), 10000).unref();
    };
    process.once('SIGINT', shutdown);
    process.once('SIGTERM', shutdown);
}

module.exports = {
    DEFAULT_CONFIG, ProtocolError, ConfigError, StorageError, buildConfig, isVpnAddress, isInsecureSecret,
    parseTimestamp, formatTimestamp, isUuid, b64encode, b64decode, sha256hex, hmac, hkdf, semiKey, fullAuthSalt,
    fullAuthKey, healthKey, infoKey, gcmSeal, gcmOpen, gcmSelfTest, semiCanonical, fullCanonical, healthAad, infoAad,
    dataResponseMac, compactJson, prettyJson, parseFlatObject, parseInfoObject, maskedContracts, parseDataRecord,
    parseRecords, mediaTypeMatches, LocalMutex, DiskStorage, Application, createHttpServer, STATUS_HTML, main,
};

if (require.main === module) {
    main().catch((error) => {
        log('fatal', error && error.name);
        process.exit(1);
    });
}

#!/usr/bin/env node
// PHP VPN 1.0.1 (MIT License, Copyright (c) www.pilandrome.com)
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
    SERVER_VERSION: '1.0.1',
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

const STATUS_HTML = '<!doctype html><html lang=en><meta name=viewport content="width=device-width"><title>PHPVPN server is running</title><style>*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;font:16px Arial,sans-serif}main{width:48rem;max-width:100%;padding:1.5rem;background:#e8e8e8;border-radius:1rem;box-shadow:0 .4rem 1.5rem #0003}h1,h2{margin-top:0}aside{padding:1rem;background:#c00;color:#fff;border-radius:.6rem}button{padding:.6rem;border:0;border-radius:.5rem;background:#06c;color:#fff;cursor:pointer}#r:not([hidden]){display:flex;gap:.5rem}#k{min-width:0;flex:1;font-family:monospace}dialog{width:44rem;max-width:calc(100% - 2rem);border:0;border-radius:1rem;padding:1.5rem}dialog::backdrop{background:#0006}#x{float:right;padding:0 .5rem;background:none;color:#000;font-size:1.5rem}#t{width:100%;height:14rem;font-family:monospace;white-space:pre}#b{text-align:right}#f{color:#777;font-size:.9rem}#f a{color:inherit}</style><main><h1>PHPVPN server is running</h1><aside><b>Warning:</b> The default SERVER_SECRET value is still in use. Set the SERVER_SECRET variable in this script to a secure value immediately. You can use the Generate key button below to create one.</aside><p>Generate a secure hexadecimal key: <button id=g>Generate key</button></p><div id=r hidden><input id=k aria-label="Generated key" readonly><button id=c>Copy key</button></div><p>View HTTP headers for the Configuration Wizard: <button id=s>Show HTTP Headers</button></p><noscript>JavaScript must be enabled to generate and copy a key or to show HTTP headers.</noscript></main><p id=f>Developed by <a href=https://www.pilandrome.com>www.pilandrome.com</a> (MIT License)</p><dialog id=d><form method=dialog><button id=x aria-label=Close>&times;</button><h2>HTTP headers</h2><p>Headers seen by this script may be incomplete. For exact values, press F12 and copy the request headers from the Network tab.</p><textarea id=t aria-label="HTTP headers" readonly></textarea><p id=b><button id=y type=button>Copy</button> <button>OK</button></p></form></dialog><script>C=e=>{e.select();navigator.clipboard?navigator.clipboard.writeText(e.value).catch(()=>document.execCommand(\'copy\')):document.execCommand(\'copy\')};g.onclick=()=>{k.value=[...crypto.getRandomValues(new Uint8Array(32))].map(x=>x.toString(16).padStart(2,0)).join(\'\');r.hidden=false};c.onclick=()=>C(k);s.onclick=()=>{d.showModal();t.select()};y.onclick=()=>C(t)</script>';
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
            const html = isInsecureSecret(this.raw.SERVER_SECRET ?? this.raw.configuredSecret)
                ? STATUS_HTML : STATUS_HTML.replace(/<aside>.*?<\/aside>/s, '');
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

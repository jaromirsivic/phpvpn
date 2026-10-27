#!/usr/bin/env python3
# PHP VPN 1.0.1 (MIT License, Copyright (c) www.pilandrome.com)
"""PHPVPN v1 relay. Python 3.13, uvicorn and cryptography. MIT licensed.

Run this file directly, one process and one worker for either storage driver.
The wire contract is protocol.md. Operational data is not portable to PHP.
"""
from __future__ import annotations

# Configuration header. Administrators edit only this contiguous block.
SERVER_VERSION = '1.0.1'
# Empty means the public default, not an absent secret. Replace before use.
SERVER_SECRET = 'CHANGE_THIS_SERVER_SECRET'
ACCESS_KEYS = {
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
}
STORAGE_DRIVER = 'disk'  # 'disk' or 'memory', both require a single process.
BUFFER_DIRECTORY = './phpvpn_data'  # Relative to this file. Unused in memory.
STATUS_PAGE_ENABLED = True
VPN_ADDRESS_PREFIX = '10.99.99.'
VPN_HOST_MIN = 2
VPN_HOST_MAX = 254
MAX_DATA_RECORDS_PER_REQUEST = 16
MAX_AUTH_RECORD_BYTES = 4096
MAX_REQUEST_BODY_BYTES = 983040
MAX_INFO_PLAINTEXT_BYTES = 262144
MAX_INFO_REQUEST_BODY_BYTES = 65536
MAX_MASKED_COMMUNICATION_CONTRACTS = 252
INFO_TEMP_TTL_SECONDS = 300  # Disk publication temp files only.
MAX_QUEUE_BYTES_PER_CLIENT = 16777216
AUTH_TIMESTAMP_SKEW_SECONDS = 60
QUEUE_TTL_SECONDS = 60
LOCK_TIMEOUT_MS = 200
MIN_DATA_POLL_INTERVAL_MS = 50
MAX_DATA_POLL_INTERVAL_MS = 3200
CLIENT_STATS_MIN_UPDATE_SECONDS = 30
SWEEP_MIN_INTERVAL_SECONDS = 5
SWEEP_MAX_DIRECTORIES = 8
SWEEP_MAX_FILES = 256
SWEEP_MAX_MILLISECONDS = 20
PROTECT_BUFFER_DIRECTORY = True  # Disk-only best-effort webserver guards.
LISTEN_HOST = '127.0.0.1'
LISTEN_PORT = 8000
SERVER_PATH = '/phpvpn.php'
# End of administrator configuration.
_CONFIG_NAMES = tuple(name for name in globals() if name.isupper() and not name.startswith('_'))

import asyncio
import base64
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
import hashlib
import hmac
import html
import json
import os
from pathlib import Path
import re
import secrets
import threading
import time
from typing import Callable
from urllib.parse import parse_qsl
import uuid

from cryptography.hazmat.primitives.ciphers.aead import AESGCM


class ProtocolError(Exception):
    def __init__(self, status: int, code: str | None = None, **headers):
        self.status = status
        self.code = code or {400: 'bad_request', 401: 'unauthorized', 404: 'not_found',
                             405: 'method_not_allowed', 413: 'payload_too_large',
                             415: 'unsupported_media_type', 500: 'internal_error',
                             503: 'queue_unavailable'}[status]
        self.headers = headers


class ConfigError(ValueError):
    pass


class Clock:
    def now_ms(self) -> int:
        return time.time_ns() // 1_000_000

    def monotonic(self) -> float:
        return time.monotonic()


class Config:
    """Validated copy of the header. Construction never touches the filesystem."""
    def __init__(self, **overrides):
        if set(overrides) - set(_CONFIG_NAMES):
            raise ConfigError('unknown setting')
        for name in _CONFIG_NAMES:
            setattr(self, name, overrides.get(name, globals()[name]))
        for name in _CONFIG_NAMES:
            value = getattr(self, name)
            expected = (dict if name == 'ACCESS_KEYS' else bool if name in ('STATUS_PAGE_ENABLED', 'PROTECT_BUFFER_DIRECTORY')
                        else str if name in ('SERVER_VERSION', 'SERVER_SECRET', 'STORAGE_DRIVER', 'BUFFER_DIRECTORY',
                                             'VPN_ADDRESS_PREFIX', 'LISTEN_HOST', 'SERVER_PATH') else int)
            if type(value) is not expected:
                raise ConfigError(name)
            if expected is int and value <= 0:
                raise ConfigError(name)
        if self.STORAGE_DRIVER not in ('disk', 'memory'):
            raise ConfigError('storage driver')
        if (self.MIN_DATA_POLL_INTERVAL_MS > self.MAX_DATA_POLL_INTERVAL_MS
                or self.MAX_AUTH_RECORD_BYTES < 256
                or self.MAX_REQUEST_BODY_BYTES < self.MAX_AUTH_RECORD_BYTES + self.MAX_DATA_RECORDS_PER_REQUEST * 160
                or self.MAX_INFO_PLAINTEXT_BYTES < 1024
                or self.VPN_HOST_MIN > self.VPN_HOST_MAX or self.VPN_HOST_MAX > 254
                or not re.fullmatch(r'(?:[0-9]{1,3}\.){3}', self.VPN_ADDRESS_PREFIX)
                or any(int(x) > 255 or str(int(x)) != x for x in self.VPN_ADDRESS_PREFIX[:-1].split('.'))
                or not self.SERVER_PATH.startswith('/') or '?' in self.SERVER_PATH or '#' in self.SERVER_PATH
                or not self.LISTEN_HOST or self.LISTEN_PORT > 65535 or not self.BUFFER_DIRECTORY):
            raise ConfigError('limits or address')
        try:
            self.configured_secret = self.SERVER_SECRET
            self.secret = (self.SERVER_SECRET if self.SERVER_SECRET != '' else 'CHANGE_THIS_SERVER_SECRET').encode('utf-8')
            self.SERVER_VERSION.encode('utf-8')
            self.access_keys = {}
            for address, key in self.ACCESS_KEYS.items():
                if not self.is_address(address) or type(key) is not str or not re.fullmatch('[0-9a-f]{64}', key):
                    raise ConfigError('access key')
                self.access_keys[address] = bytes.fromhex(key)
        except (UnicodeError, TypeError) as exc:
            raise ConfigError('encoding') from exc
        self.addresses = sorted(self.access_keys, key=address_key)
        self.buffer = Path(os.path.abspath(os.path.join(os.path.dirname(__file__), self.BUFFER_DIRECTORY)))

    def is_address(self, value) -> bool:
        if type(value) is not str or not value.startswith(self.VPN_ADDRESS_PREFIX):
            return False
        host = value[len(self.VPN_ADDRESS_PREFIX):]
        return bool(re.fullmatch(r'[1-9][0-9]{0,2}', host)) and self.VPN_HOST_MIN <= int(host) <= self.VPN_HOST_MAX


def address_key(address: str) -> tuple[int, ...]:
    return tuple(map(int, address.split('.')))


def insecure_secret(value) -> bool:
    return isinstance(value, str) and (value in ('', 'CHANGE_THIS_SERVER_SECRET') or set(value) <= {'0'} or set(value) <= {'\0'})


def compact(value) -> bytes:
    return json.dumps(value, separators=(',', ':'), ensure_ascii=True, allow_nan=False).encode('ascii')


def pretty(value) -> bytes:
    # PHP JSON_PRETTY_PRINT: four spaces, escaped Unicode, compact empty arrays,
    # unescaped slashes, no final newline. Python matches these choices explicitly.
    return json.dumps(value, indent=4, ensure_ascii=True, allow_nan=False).encode('ascii')


def timestamp(ms: int) -> str:
    dt = datetime(1970, 1, 1, tzinfo=timezone.utc) + timedelta(milliseconds=ms)
    return f'{dt.year:04d}-{dt.month:02d}-{dt.day:02d}T{dt.hour:02d}:{dt.minute:02d}:{dt.second:02d}.{dt.microsecond // 1000:03d}Z'


def parse_timestamp(text: str) -> int:
    if not re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}Z', text):
        raise ProtocolError(400)
    y, m, d, h, minute, s, ms = map(int, (text[:4], text[5:7], text[8:10], text[11:13], text[14:16], text[17:19], text[20:23]))
    # Proleptic Gregorian year 0000 is accepted by the reference wire parser.
    leap = y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)
    if not (1 <= m <= 12 and 1 <= d <= (31, 29 if leap else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)[m-1]
            and h < 24 and minute < 60 and s < 60):
        raise ProtocolError(400)
    y -= m <= 2
    era = y // 400
    yo = y - era * 400
    doy = (153 * (m + (-3 if m > 2 else 9)) + 2) // 5 + d - 1
    days = era * 146097 + yo * 365 + yo // 4 - yo // 100 + doy - 719468
    return ((days * 86400 + h * 3600 + minute * 60 + s) * 1000) + ms


_UUID = re.compile(r'[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}')


def is_uuid(value) -> bool:
    return type(value) is str and _UUID.fullmatch(value) is not None


def b64encode(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).rstrip(b'=').decode('ascii')


def b64decode(text: str, size: int | None = None) -> bytes:
    if not re.fullmatch(r'[A-Za-z0-9_-]*', text) or len(text) % 4 == 1:
        raise ProtocolError(400)
    value = base64.urlsafe_b64decode(text + '=' * (-len(text) % 4))
    if b64encode(value) != text or (size is not None and len(value) != size):
        raise ProtocolError(400)
    return value


def _pairs(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj or '\0' in key:
            raise ValueError('duplicate key')
        obj[key] = value
    return obj


def _reject(_):
    raise ValueError('unsupported value')


def info_object(body: bytes) -> dict | None:
    # Check depth before allocating nested containers or invoking the JSON decoder.
    depth, in_string, escaped = 0, False, False
    for c in body:
        if in_string:
            if escaped:
                escaped = False
            elif c == 92:
                escaped = True
            elif c == 34:
                in_string = False
        elif c == 34:
            in_string = True
        elif c in (91, 123):
            depth += 1
            if depth > 4:
                return None
        elif c in (93, 125):
            depth -= 1
    try:
        value = json.loads(body.decode('utf-8'), object_pairs_hook=_pairs,
                           parse_constant=_reject, parse_int=_reject, parse_float=_reject)
        # Reject unpaired surrogate escapes even in unused fields.
        def validate(v, level=0):
            if level > 4:
                raise ValueError('depth')
            if isinstance(v, str):
                v.encode('utf-8')
            elif isinstance(v, dict):
                for k, item in v.items():
                    validate(k, level + 1)
                    validate(item, level + 1)
            elif isinstance(v, list):
                for item in v:
                    validate(item, level + 1)
        validate(value)
        return value if type(value) is dict else None
    except (ValueError, UnicodeError, RecursionError):
        return None


def flat_object(body: bytes, keys: set[str]) -> dict:
    if not body.startswith(b'{') or not body.endswith(b'}') or b'\\' in body:
        raise ProtocolError(400)
    try:
        text = body.decode('ascii')
        value = json.loads(text, object_pairs_hook=_pairs, parse_constant=_reject)
    except (ValueError, UnicodeError, RecursionError):
        raise ProtocolError(400) from None
    if (type(value) is not dict or set(value) != keys
            or any(type(v) is not str or any(ord(c) < 32 or ord(c) > 126 for c in v) for v in value.values())):
        raise ProtocolError(400)
    return value


def contracts(value, src: str, cfg: Config) -> list[dict]:
    if type(value) is not list:
        raise ProtocolError(400)
    if len(value) > min(cfg.MAX_MASKED_COMMUNICATION_CONTRACTS, cfg.VPN_HOST_MAX - cfg.VPN_HOST_MIN):
        raise ProtocolError(413)
    out, seen = [], set()
    for item in value:
        if (type(item) is not dict or set(item) != {'peer', 'hash'}
                or not cfg.is_address(item['peer']) or item['peer'] == src or item['peer'] in seen
                or type(item['hash']) is not str or not re.fullmatch('[0-9a-f]{64}', item['hash'])):
            raise ProtocolError(400)
        seen.add(item['peer'])
        out.append({'peer': item['peer'], 'hash': item['hash']})
    return out


@dataclass(frozen=True)
class DataRecord:
    message_id: str
    dst: str
    tag: str
    line: bytes


def parse_record(line: bytes, src: str, cfg: Config) -> DataRecord:
    # Stage the source check to preserve PHP's 400/401 precedence.
    head = re.match(rb'^\{"type":"data","messageId":"([^"\n]*)","src":"', line)
    if head is None:
        raise ProtocolError(400)
    try:
        message_id = head[1].decode('ascii')
    except UnicodeError:
        raise ProtocolError(400) from None
    if not is_uuid(message_id):
        raise ProtocolError(400)
    rest = line[head.end():]
    source = src.encode() + b'","dst":"'
    if not rest.startswith(source):
        raise ProtocolError(401)
    match = re.fullmatch(rb'([^"\n]*)","iv":"([A-Za-z0-9_-]*)","tag":"([A-Za-z0-9_-]*)","ciphertext":"([A-Za-z0-9_-]+)"\}', rest[len(source):])
    if not match:
        raise ProtocolError(400)
    try:
        dst, iv, tag, ciphertext = (x.decode('ascii') for x in match.groups())
    except UnicodeError:
        raise ProtocolError(400) from None
    if not cfg.is_address(dst):
        raise ProtocolError(400)
    b64decode(iv, 12)
    b64decode(tag, 16)
    b64decode(ciphertext)
    return DataRecord(message_id, dst, tag, line)


def parse_records(rest: bytes, src: str, cfg: Config) -> tuple[DataRecord, ...]:
    if not rest:
        return ()
    if not rest.endswith(b'\n'):
        raise ProtocolError(400)
    lines = rest[:-1].split(b'\n', cfg.MAX_DATA_RECORDS_PER_REQUEST)
    if len(lines) > cfg.MAX_DATA_RECORDS_PER_REQUEST:
        raise ProtocolError(413)
    records, seen = [], set()
    for line in lines:
        if not line or b'\r' in line:
            raise ProtocolError(400)
        record = parse_record(line, src, cfg)
        if record.message_id in seen:
            raise ProtocolError(400)
        seen.add(record.message_id)
        records.append(record)
    return tuple(records)


def mac(key: bytes, value: bytes) -> bytes:
    return hmac.digest(key, value, 'sha256')


def hkdf(ikm: bytes, salt: bytes, info: bytes) -> bytes:
    return mac(mac(salt, ikm), info + b'\x01')


DUMMY_ACCESS_KEY = bytes.fromhex('9b1d36c754a20ef86341d9257c0bae521877f34c2a906dbb05e43981ca6f1728')


def semi_key(secret: bytes) -> bytes:
    return hkdf(secret, hashlib.sha256(b'phpvpn/auth-semi/salt').digest(), b'phpvpn/auth-semi/key')


def full_key(secret: bytes, access: bytes, src: str) -> bytes:
    src_bytes = src.encode('ascii')
    return hkdf(access, mac(secret, b'phpvpn/auth-full/salt\0' + src_bytes), b'phpvpn/auth-full/key\0' + src_bytes)


def health_key(secret: bytes, request_id: str) -> bytes:
    rid = request_id.encode('ascii')
    return hkdf(secret, hashlib.sha256(b'phpvpn/health-response/salt\0' + rid).digest(), b'phpvpn/health-response/key\0' + rid)


@dataclass(frozen=True)
class Batch:
    expires: int
    size: int
    pairs: tuple[tuple[str, str], ...]
    records: tuple[DataRecord, ...] = ()
    path: Path | None = None
    offset: int = 0
    order: int = 0


class Storage:
    """Per-client synchronization shared by every request in the process."""
    def __init__(self, cfg: Config, clock: Clock):
        self.cfg, self.clock = cfg, clock
        # Fixed address space, so locks cannot disappear while a request owns one.
        self.locks = {cfg.VPN_ADDRESS_PREFIX + str(n): threading.Lock()
                      for n in range(cfg.VPN_HOST_MIN, cfg.VPN_HOST_MAX + 1)}
        self.stats_locks = {address: threading.Lock() for address in self.locks}
        self.sweep_lock = threading.Lock()
        self.last_sweep = float('-inf')
        self.sweep_cursor = 0
        self.file_cursors = {}

    @contextmanager
    def locked(self, address):
        lock = self.locks[address]
        if not lock.acquire(timeout=self.cfg.LOCK_TIMEOUT_MS / 1000):
            raise ProtocolError(503, 'queue_busy')
        try:
            yield
        finally:
            lock.release()

    def enqueue(self, dst: str, src: str, records: tuple[DataRecord, ...]) -> bool:
        size = sum(len(r.line) + 1 for r in records)
        with self.locked(dst):
            now = self.clock.now_ms()
            if self.usage(dst, now) + size > self.cfg.MAX_QUEUE_BYTES_PER_CLIENT:
                self.sweep_queue(dst, now)
                if self.usage(dst, now) + size > self.cfg.MAX_QUEUE_BYTES_PER_CLIENT:
                    return False
            self.publish(dst, src, records, size)
            return True

    def is_usable(self):
        return True

    def finish(self, batch: Batch):
        pass

    def read_batch(self, batch: Batch) -> bytes:
        return b''.join(record.line + b'\n' for record in batch.records)

    def sweep(self):
        if not self.sweep_lock.acquire(blocking=False):
            return
        try:
            started = self.clock.monotonic()
            if started - self.last_sweep < self.cfg.SWEEP_MIN_INTERVAL_SECONDS:
                return
            self.last_sweep = started
            deadline = started + self.cfg.SWEEP_MAX_MILLISECONDS / 1000
            remaining = self.cfg.SWEEP_MAX_FILES
            addresses = self.sweep_addresses()
            if not addresses:
                return
            for _ in range(min(len(addresses), self.cfg.SWEEP_MAX_DIRECTORIES)):
                if remaining <= 0 or self.clock.monotonic() > deadline:
                    break
                address = addresses[self.sweep_cursor % len(addresses)]
                self.sweep_cursor = (self.sweep_cursor + 1) % len(addresses)
                if not self.locks[address].acquire(blocking=False):
                    continue
                try:
                    remaining -= self.sweep_queue(address, self.clock.now_ms(), remaining, deadline)
                except OSError:
                    pass
                finally:
                    self.locks[address].release()
        finally:
            self.sweep_lock.release()


class MemoryStorage(Storage):
    def __init__(self, cfg, clock):
        super().__init__(cfg, clock)
        self.queues: dict[str, deque[Batch]] = {}
        self.sequences: dict[str, int] = {}
        self.stats: dict[str, tuple[int, tuple[tuple[str, str], ...]]] = {}

    def usage(self, address, now):
        return sum(b.size for b in self.queues.get(address, ()) if b.expires > now)

    def sweep_addresses(self):
        return tuple(self.queues)

    def publish(self, dst, src, records, size):
        sequence = self.sequences.get(dst, 0)
        self.sequences[dst] = sequence + 1
        batch = Batch(self.clock.now_ms() + self.cfg.QUEUE_TTL_SECONDS * 1000, size,
                      tuple((r.message_id, r.tag) for r in records), records, order=sequence)
        self.queues.setdefault(dst, deque()).append(batch)

    def drain(self, address, now):
        with self.locked(address):
            batches = self.queues.pop(address, [])
            self.sequences.pop(address, None)
        return sorted((batch for batch in batches if batch.expires > now), key=lambda b: b.order)

    def sweep_queue(self, address, now, limit=None, deadline=None):
        batches = self.queues.get(address)
        if not batches:
            return 0
        count, inspected = len(batches), 0
        # Rotate inspected live entries, so each unit of maintenance is O(1).
        # drain sorts the surviving immutable batches by publication order.
        for i in range(count if limit is None else min(count, limit)):
            if deadline is not None and self.clock.monotonic() > deadline:
                break
            batch = batches.popleft()
            inspected += 1
            if batch.expires > now:
                batches.append(batch)
        if not batches:
            self.queues.pop(address, None)
            self.sequences.pop(address, None)
        return inspected

    def snapshot(self, address):
        with self.stats_locks[address]:
            seen, values = self.stats.get(address, (None, ()))
        return seen, [{'peer': p, 'hash': h} for p, h in values]

    def last_seen(self, address):
        with self.stats_locks[address]:
            return self.stats.get(address, (None, ()))[0]

    def publish_contracts(self, address, values, now):
        with self.stats_locks[address]:
            self.stats[address] = (now // 1000 * 1000, tuple((v['peer'], v['hash']) for v in values))

    def touch(self, address, now):
        lock = self.stats_locks[address]
        if not lock.acquire(blocking=False):
            return
        try:
            seen, values = self.stats.get(address, (None, ()))
            if seen is None or now - seen > self.cfg.CLIENT_STATS_MIN_UPDATE_SECONDS * 1000:
                self.stats[address] = (now // 1000 * 1000, values)
        finally:
            lock.release()


class FileSystem:
    """Narrow injectable operations. No instance is created by MemoryStorage."""
    def mkdir(self, path):
        path.mkdir(parents=True, exist_ok=True)

    def open(self, path, mode):
        return path.open(mode)

    def replace(self, source, target):
        os.replace(source, target)

    def unlink(self, path):
        path.unlink(missing_ok=True)

    def entries(self, path):
        try:
            return sorted(path.iterdir())
        except FileNotFoundError:
            return []

    def utime(self, path, seconds):
        os.utime(path, (seconds, seconds))

    def stat(self, path):
        return path.stat()

    def fstat(self, handle):
        return os.fstat(handle.fileno())

    def write(self, handle, data):
        return handle.write(data)

    def flush(self, handle):
        handle.flush()


class DiskStorage(Storage):
    _NAME = re.compile(r'([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}-[0-9]{2}-[0-9]{2}\.[0-9]{3}Z)_([1-9][0-9]*)_([0-9.]+)_([0-9a-f-]{36})\.(receiving|data|sending)')

    def __init__(self, cfg, clock, fs=None):
        super().__init__(cfg, clock)
        self.root, self.fs = cfg.buffer, fs or FileSystem()
        self.root_lock = threading.Lock()
        self.ready = False

    def directory(self, address):
        return self.root / address.replace('.', '_')

    def sweep_addresses(self):
        try:
            return tuple(path.name.replace('_', '.') for path in self.fs.entries(self.root)
                         if self.cfg.is_address(path.name.replace('_', '.')))
        except OSError:
            return ()

    def is_usable(self):
        with self.root_lock:
            if not self.ready:
                self.fs.mkdir(self.root)
                if self.cfg.PROTECT_BUFFER_DIRECTORY:
                    for name, body in {
                        '.htaccess': b'Require all denied\n',
                        'web.config': b'<?xml version="1.0"?><configuration><system.webServer><security><requestFiltering><hiddenSegments><add segment="." /></hiddenSegments></requestFiltering></security></system.webServer></configuration>',
                        'index.html': b'',
                    }.items():
                        try:
                            with self.fs.open(self.root / name, 'xb') as handle:
                                self.write_all(handle, body)
                        except OSError:
                            pass
                self.ready = True
        return True

    def write_all(self, handle, data):
        view = memoryview(data)
        while view:
            written = self.fs.write(handle, view)
            if written is None or written <= 0:
                raise OSError('short write')
            view = view[written:]

    def safe_unlink(self, path):
        try:
            self.fs.unlink(path)
        except OSError:
            pass

    def parse_name(self, path):
        match = self._NAME.fullmatch(path.name)
        if not match or not is_uuid(match[4]) or not self.cfg.is_address(match[3]):
            return None
        stamp = match[1]
        try:
            expires = parse_timestamp(stamp[:11] + stamp[11:].replace('-', ':'))
            return expires, int(match[2]), match[3], match[5]
        except (ProtocolError, ValueError):
            return None

    def usage(self, address, now):
        total = 0
        for path in self.fs.entries(self.directory(address)):
            meta = self.parse_name(path)
            if meta and meta[0] > now and meta[3] in ('data', 'receiving'):
                total += meta[1]
        return total

    def publish(self, dst, src, records, size):
        self.is_usable()
        directory = self.directory(dst)
        self.fs.mkdir(directory)
        suffix = f'_{size}_{src}_{uuid.uuid4()}'
        expiry = self.clock.now_ms() + self.cfg.QUEUE_TTL_SECONDS * 1000
        temp = directory / (timestamp(expiry).replace(':', '-') + suffix + '.receiving')
        index = compact({'format': 'phpvpn-python-1', 'bytes': size,
                         'records': [[r.message_id, r.tag] for r in records]}) + b'\n'
        try:
            with self.fs.open(temp, 'xb') as handle:
                self.write_all(handle, index)
                for record in records:
                    self.write_all(handle, record.line + b'\n')
                self.fs.flush(handle)
            # Expiry starts at publication, after the complete write and close.
            expiry = self.clock.now_ms() + self.cfg.QUEUE_TTL_SECONDS * 1000
            final = directory / (timestamp(expiry).replace(':', '-') + suffix + '.data')
            self.fs.replace(temp, final)
        finally:
            self.safe_unlink(temp)

    def drain(self, address, now):
        claimed = []
        with self.locked(address):
            candidates = [(path, self.parse_name(path)) for path in self.fs.entries(self.directory(address))]
            try:
                for path, meta in candidates:
                    if not meta or meta[3] != 'data' or meta[0] <= now:
                        continue
                    target = path.with_suffix('.sending')
                    self.fs.replace(path, target)
                    try:
                        with self.fs.open(target, 'rb') as handle:
                            header = handle.readline(self.cfg.MAX_REQUEST_BODY_BYTES + 1)
                            offset = handle.tell()
                            index = json.loads(header)
                            if (not header.endswith(b'\n') or type(index) is not dict
                                    or index.get('format') != 'phpvpn-python-1'
                                    or type(index.get('bytes')) is not int or index['bytes'] != meta[1]
                                    or self.fs.fstat(handle).st_size != offset + meta[1]
                                    or type(index.get('records')) is not list
                                    or not 1 <= len(index['records']) <= self.cfg.MAX_DATA_RECORDS_PER_REQUEST):
                                raise ValueError('batch index')
                            pairs = []
                            for pair in index['records']:
                                if type(pair) is not list or len(pair) != 2 or not is_uuid(pair[0]) or type(pair[1]) is not str:
                                    raise ValueError('batch pair')
                                b64decode(pair[1], 16)
                                pairs.append(tuple(pair))
                        claimed.append(Batch(meta[0], meta[1], tuple(pairs), path=target, offset=offset))
                    except (OSError, ValueError, ProtocolError):
                        self.safe_unlink(target)
                        raise OSError('invalid claimed batch') from None
            except BaseException:
                for batch in claimed:
                    self.finish(batch)
                raise
        return claimed

    def read_batch(self, batch):
        with self.fs.open(batch.path, 'rb') as handle:
            handle.seek(batch.offset)
            body = handle.read(batch.size + 1)
        if len(body) != batch.size or not body.endswith(b'\n'):
            raise OSError('damaged batch')
        # Validate stored bytes as well as index before allowing the final MAC.
        meta = self.parse_name(batch.path)
        records = parse_records(body, meta[2], self.cfg)
        if tuple((r.message_id, r.tag) for r in records) != batch.pairs:
            raise OSError('damaged batch pairs')
        return body

    def finish(self, batch):
        self.safe_unlink(batch.path)

    def sweep_queue(self, address, now, limit=None, deadline=None):
        entries = self.fs.entries(self.directory(address))
        if not entries:
            self.file_cursors.pop(address, None)
            return 0
        start = self.file_cursors.get(address, 0) % len(entries)
        inspected = 0
        for i in range(len(entries) if limit is None else min(len(entries), limit)):
            if deadline is not None and self.clock.monotonic() > deadline:
                break
            path = entries[(start + i) % len(entries)]
            inspected += 1
            meta = self.parse_name(path)
            if meta and meta[0] <= now:
                self.safe_unlink(path)
            elif path.name.startswith('.info-') and path.name.endswith('.tmp') and is_uuid(path.name[6:-4]):
                try:
                    if now - int(self.fs.stat(path).st_mtime) * 1000 > self.cfg.INFO_TEMP_TTL_SECONDS * 1000:
                        self.safe_unlink(path)
                except OSError:
                    pass
        self.file_cursors[address] = (start + inspected) % len(entries)
        return inspected

    def snapshot(self, address):
        # Windows can transiently refuse opens while replace is in progress.
        # Serialize our own marker operations, while each snapshot still reads
        # content and mtime from the same opened file.
        with self.stats_locks[address]:
            return self._snapshot(address)

    def _snapshot(self, address):
        seen = None
        try:
            with self.fs.open(self.directory(address) / 'info.json', 'rb') as handle:
                seen = int(self.fs.fstat(handle).st_mtime) * 1000
                body = handle.read(self.cfg.MAX_INFO_REQUEST_BODY_BYTES + 1)
        except (OSError, ValueError, OverflowError):
            return seen, []
        obj = info_object(body) if len(body) <= self.cfg.MAX_INFO_REQUEST_BODY_BYTES else None
        if obj is not None and set(obj) == {'maskedCommunicationContracts'}:
            try:
                return seen, contracts(obj['maskedCommunicationContracts'], address, self.cfg)
            except ProtocolError:
                pass
        return seen, []

    def last_seen(self, address):
        # Standard info reads only marker metadata, never its contract content.
        try:
            return int(self.fs.stat(self.directory(address) / 'info.json').st_mtime) * 1000
        except (OSError, ValueError, OverflowError):
            return None

    def publish_contracts(self, address, values, now):
        with self.stats_locks[address]:
            self._publish_contracts(address, values, now)

    def _publish_contracts(self, address, values, now):
        body = compact({'maskedCommunicationContracts': values})
        if len(body) > self.cfg.MAX_INFO_REQUEST_BODY_BYTES:
            raise OSError('info size')
        self.is_usable()
        directory = self.directory(address)
        self.fs.mkdir(directory)
        temp = directory / f'.info-{uuid.uuid4()}.tmp'
        try:
            with self.fs.open(temp, 'xb') as handle:
                self.write_all(handle, body)
                self.fs.flush(handle)
            self.fs.utime(temp, now // 1000)
            # Never unlink the old marker. Windows sharing violations preserve it.
            self.fs.replace(temp, directory / 'info.json')
        finally:
            self.safe_unlink(temp)

    def touch(self, address, now):
        lock = self.stats_locks[address]
        if not lock.acquire(blocking=False):
            return
        try:
            self._touch(address, now)
        finally:
            lock.release()

    def _touch(self, address, now):
        try:
            self.is_usable()
            directory = self.directory(address)
            self.fs.mkdir(directory)
            marker = directory / 'info.json'
            try:
                seen = int(self.fs.stat(marker).st_mtime) * 1000
            except OSError:
                seen = None
            if seen is not None and now - seen <= self.cfg.CLIENT_STATS_MIN_UPDATE_SECONDS * 1000:
                return
            if seen is None:
                # Append mode creates an empty missing marker without truncating
                # existing content when the metadata read failed.
                with self.fs.open(marker, 'ab'):
                    pass
            self.fs.utime(marker, now // 1000)
        except OSError:
            pass


@dataclass
class Response:
    status: int = 200
    content_type: str = 'application/json; charset=utf-8'
    body: bytes = b''
    headers: dict = field(default_factory=dict)
    batches: deque[Batch] = field(default_factory=deque)


def error_response(error: ProtocolError) -> Response:
    headers = dict(error.headers)
    if error.status == 503:
        headers['Retry-After'] = '1'
    return Response(error.status, body=compact({'type': 'error', 'code': error.code}), headers=headers)


def media_type(header: str, expected: str) -> bool:
    parts = header.split(';')
    if parts[0].strip().lower() != expected:
        return False
    for part in parts[1:]:
        part = part.strip()
        if part.lower().startswith('charset=') and part[8:].strip('"').lower() not in ('utf-8', 'utf8'):
            return False
    return True


STATUS_HTML = b'''<!doctype html><html lang=en><meta name=viewport content="width=device-width"><title>PHPVPN server is running</title><style>*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;font:16px Arial,sans-serif}main{width:48rem;max-width:100%;padding:1.5rem;background:#e8e8e8;border-radius:1rem;box-shadow:0 .4rem 1.5rem #0003}h1,h2{margin-top:0}aside{padding:1rem;background:#c00;color:#fff;border-radius:.6rem}button{padding:.6rem;border:0;border-radius:.5rem;background:#06c;color:#fff;cursor:pointer}#r:not([hidden]){display:flex;gap:.5rem}#k{min-width:0;flex:1;font-family:monospace}dialog{width:44rem;max-width:calc(100% - 2rem);border:0;border-radius:1rem;padding:1.5rem}dialog::backdrop{background:#0006}#x{float:right;padding:0 .5rem;background:none;color:#000;font-size:1.5rem}#t{width:100%;height:14rem;font-family:monospace;white-space:pre}#b{text-align:right}#f{color:#777;font-size:.9rem}#f a{color:inherit}</style><main><h1>PHPVPN server is running</h1><aside><b>Warning:</b> The default SERVER_SECRET value is still in use. Set the SERVER_SECRET variable in this script to a secure value immediately. You can use the Generate key button below to create one.</aside><p>Generate a secure hexadecimal key: <button id=g>Generate key</button></p><div id=r hidden><input id=k aria-label="Generated key" readonly><button id=c>Copy key</button></div><p>View HTTP headers for the Configuration Wizard: <button id=s>Show HTTP Headers</button></p><noscript>JavaScript must be enabled to generate and copy a key or to show HTTP headers.</noscript></main><p id=f>Developed by <a href=https://www.pilandrome.com>www.pilandrome.com</a> (MIT License)</p><dialog id=d><form method=dialog><button id=x aria-label=Close>&times;</button><h2>HTTP headers</h2><p>Headers seen by this script may be incomplete. For exact values, press F12 and copy the request headers from the Network tab.</p><textarea id=t aria-label="HTTP headers" readonly></textarea><p id=b><button id=y type=button>Copy</button> <button>OK</button></p></form></dialog><script>C=e=>{e.select();navigator.clipboard?navigator.clipboard.writeText(e.value).catch(()=>document.execCommand('copy')):document.execCommand('copy')};g.onclick=()=>{k.value=[...crypto.getRandomValues(new Uint8Array(32))].map(x=>x.toString(16).padStart(2,0)).join('');r.hidden=false};c.onclick=()=>C(k);s.onclick=()=>{d.showModal();t.select()};y.onclick=()=>C(t)</script>'''
# The client owns these names and its HTTP headers dialog rejects them.
CLIENT_MANAGED_HEADERS = {'host', 'connection', 'content-type', 'content-length', 'transfer-encoding'}


def status_headers(raw) -> bytes:
    """Escaped `Name: value` lines of the request headers for the Show HTTP Headers dialog."""
    merged = {}
    for name, value in raw:
        name, value = name.decode('latin1').lower(), value.decode('utf-8', 'replace')
        if name in merged:
            value = merged[name] + ('; ' if name == 'cookie' else ', ') + value
        merged[name] = value
    lines = ['-'.join(part.capitalize() for part in name.split('-')) + ': ' + value
             for name, value in merged.items() if name not in CLIENT_MANAGED_HEADERS]
    return html.escape('\n'.join(lines)).encode('utf-8')


class Application:
    def __init__(self, cfg: Config | None = None, *, clock=None, random_bytes=None, storage=None):
        self.clock = clock or Clock()
        self.random_bytes = random_bytes or secrets.token_bytes
        self.config_error = False
        try:
            self.cfg = cfg or Config()
        except ConfigError:
            self.config_error = True
            self.cfg = None
        self.storage = storage
        if self.cfg is not None and storage is None:
            self.storage = (MemoryStorage if self.cfg.STORAGE_DRIVER == 'memory' else DiskStorage)(self.cfg, self.clock)
        self.executor = ThreadPoolExecutor(max_workers=8, thread_name_prefix='phpvpn-storage')

    def preflight(self, method, action, content_type, path, headers=()):
        cfg = self.cfg
        if path != (cfg.SERVER_PATH if cfg else SERVER_PATH):
            raise ProtocolError(404)
        if action in (None, ''):
            if method not in ('GET', 'HEAD'):
                raise ProtocolError(405, Allow='GET, HEAD')
            if not (cfg.STATUS_PAGE_ENABLED if cfg else STATUS_PAGE_ENABLED):
                raise ProtocolError(404)
            body = STATUS_HTML
            if not insecure_secret(cfg.configured_secret if cfg else SERVER_SECRET):
                body = re.sub(b'<aside>.*?</aside>', b'', body)
            body = body.replace(b'readonly></textarea>', b'readonly>' + status_headers(headers) + b'</textarea>', 1)
            return Response(content_type='text/html; charset=utf-8', body=body)
        if action not in ('health', 'info', 'data'):
            raise ProtocolError(404)
        if method != 'POST':
            raise ProtocolError(405, Allow='POST')
        if self.config_error:
            raise ProtocolError(503, 'server_misconfigured')
        if not media_type(content_type, 'application/x-ndjson' if action == 'data' else 'application/json'):
            raise ProtocolError(415)
        if action == 'info' and not cfg.MAX_AUTH_RECORD_BYTES <= cfg.MAX_INFO_REQUEST_BODY_BYTES <= cfg.MAX_REQUEST_BODY_BYTES:
            raise ProtocolError(503, 'server_misconfigured')
        return None

    def authenticate(self, fields, *, full=False, action='info', body_hash=None):
        cfg = self.cfg
        stamp = parse_timestamp(fields['timestamp'])
        provided = b64decode(fields['tag'], 32)
        if abs(stamp - self.clock.now_ms()) > cfg.AUTH_TIMESTAMP_SKEW_SECONDS * 1000:
            raise ProtocolError(401)
        if full:
            src = fields['src']
            access = cfg.access_keys.get(src)
            key = full_key(cfg.secret, access if access is not None else DUMMY_ACCESS_KEY, src)
            canonical = f"PHPVPN-AUTH-FULL\nPOST\n{action}\n{src}\n{fields['timestamp']}\n{fields['requestId']}\n{body_hash}"
            known = access is not None
        else:
            key = semi_key(cfg.secret)
            canonical = f"PHPVPN-AUTH-SEMI\nPOST\ninfo\n{fields['timestamp']}\n{fields['requestId']}"
            known = True
        valid = hmac.compare_digest(mac(key, canonical.encode('ascii')), provided)
        if not known or not valid:
            raise ProtocolError(401)
        return key

    def health(self, body):
        fields = flat_object(body, {'type', 'requestId'})
        if fields['type'] != 'auth-none' or not is_uuid(fields['requestId']):
            raise ProtocolError(400)
        rid, stamp = fields['requestId'], timestamp(self.clock.now_ms())
        iv = self.random_bytes(12)
        iv64 = b64encode(iv)
        aad = f'PHPVPN-HEALTH-RESPONSE\nhealth\n{rid}\n{self.cfg.SERVER_VERSION}\nrunning\nOK\n{stamp}\n{iv64}'.encode('utf-8')
        tag = AESGCM(health_key(self.cfg.secret, rid)).encrypt(iv, b'', aad)
        return Response(body=compact({'serverVersion': self.cfg.SERVER_VERSION, 'status': 'running', 'description': 'OK',
                                      'timestamp': stamp, 'requestId': rid, 'iv': iv64, 'tag': b64encode(tag)}))

    def build_info(self, requester=None):
        cfg, clients = self.cfg, []
        for address in cfg.addresses:
            seen, values = self.storage.snapshot(address) if requester is not None else (self.storage.last_seen(address), [])
            if seen is None:
                continue
            try:
                client = {'address': address, 'lastSeen': timestamp(seen)}
            except (ValueError, OverflowError):
                continue
            if requester is not None:
                client['maskedCommunicationContracts'] = [v for v in values if v['peer'] == requester] if address != requester else []
            clients.append(client)
        document = {'serverVersion': cfg.SERVER_VERSION, 'generatedAt': timestamp(self.clock.now_ms()), 'clients': clients,
                    'limits': {'maxDataRecordsPerRequest': cfg.MAX_DATA_RECORDS_PER_REQUEST,
                               'maxRequestBodyBytes': cfg.MAX_REQUEST_BODY_BYTES,
                               'maxQueueBytesPerClient': cfg.MAX_QUEUE_BYTES_PER_CLIENT,
                               'queueTtlSeconds': cfg.QUEUE_TTL_SECONDS,
                               'minDataPollIntervalMs': cfg.MIN_DATA_POLL_INTERVAL_MS,
                               'maxDataPollIntervalMs': cfg.MAX_DATA_POLL_INTERVAL_MS}}
        while True:
            encoded = pretty(document)
            if len(encoded) <= cfg.MAX_INFO_PLAINTEXT_BYTES:
                return encoded
            if not clients:
                raise ProtocolError(503, 'server_misconfigured')
            clients.pop()

    def info(self, body):
        cfg = self.cfg
        obj = info_object(body)
        extended = obj is not None and bool(set(obj) & {'maskedCommunicationContracts', 'includeMaskedCommunicationContracts'})
        if not extended and len(body) > cfg.MAX_AUTH_RECORD_BYTES:
            raise ProtocolError(413)
        required = {'type', 'timestamp', 'requestId', 'tag'} | ({'src'} if extended else set())
        fields = obj if extended else flat_object(body, required)
        if (not required <= set(fields) or set(fields) - required - {'maskedCommunicationContracts', 'includeMaskedCommunicationContracts'}
                or any(type(fields[k]) is not str for k in required)
                or fields['type'] != ('auth-full' if extended else 'auth-semi') or not is_uuid(fields['requestId'])):
            raise ProtocolError(400)
        extensions = {}
        if extended:
            if not cfg.is_address(fields['src']):
                raise ProtocolError(401)
            if 'maskedCommunicationContracts' in fields:
                extensions['maskedCommunicationContracts'] = contracts(fields['maskedCommunicationContracts'], fields['src'], cfg)
            if 'includeMaskedCommunicationContracts' in fields:
                if type(fields['includeMaskedCommunicationContracts']) is not bool:
                    raise ProtocolError(400)
                extensions['includeMaskedCommunicationContracts'] = fields['includeMaskedCommunicationContracts']
        self.authenticate(fields, full=extended, body_hash=hashlib.sha256(compact(extensions)).hexdigest())
        if extended:
            if 'maskedCommunicationContracts' in extensions:
                self.storage.publish_contracts(fields['src'], extensions['maskedCommunicationContracts'], self.clock.now_ms())
            else:
                self.storage.touch(fields['src'], self.clock.now_ms())
        plaintext = self.build_info(fields['src'] if extensions.get('includeMaskedCommunicationContracts') else None)
        rid = fields['requestId']
        salt, iv = self.random_bytes(32), self.random_bytes(12)
        salt64, iv64 = b64encode(salt), b64encode(iv)
        key = hkdf(cfg.secret, salt, ('phpvpn/info-response/' + rid).encode('ascii'))
        sealed = AESGCM(key).encrypt(iv, plaintext, f'PHPVPN-INFO\ninfo\n{rid}\n{salt64}\n{iv64}'.encode('ascii'))
        return Response(body=compact({'type': 'info', 'requestId': rid, 'salt': salt64, 'iv': iv64,
                                      'ciphertext': b64encode(sealed[:-16]), 'tag': b64encode(sealed[-16:])}))

    def data(self, body):
        cfg = self.cfg
        lf = body.find(b'\n')
        if (len(body) + 1 if lf < 0 else lf + 1) > cfg.MAX_AUTH_RECORD_BYTES:
            raise ProtocolError(413)
        if lf < 0:
            raise ProtocolError(400)
        fields = flat_object(body[:lf], {'type', 'src', 'timestamp', 'requestId', 'tag'})
        if fields['type'] != 'auth-full' or not is_uuid(fields['requestId']) or not cfg.is_address(fields['src']):
            raise ProtocolError(400)
        rest, src, rid = body[lf+1:], fields['src'], fields['requestId']
        key = self.authenticate(fields, full=True, action='data', body_hash=hashlib.sha256(rest).hexdigest())
        records = parse_records(rest, src, cfg)
        self.storage.is_usable()
        groups = {}
        for record in records:
            groups.setdefault(record.dst, []).append(record)
        accepted_ids, accepted_bytes = set(), 0
        for dst in sorted(groups, key=address_key):
            group = tuple(groups[dst])
            if self.storage.enqueue(dst, src, group):
                accepted_ids.update(r.message_id for r in group)
                accepted_bytes += sum(len(r.line) + 1 for r in group)
        self.storage.touch(src, self.clock.now_ms())
        claimed = self.storage.drain(src, self.clock.now_ms())
        try:
            result = compact({'type': 'data-result', 'requestId': rid,
                              'acceptedMessageIds': [r.message_id for r in records if r.message_id in accepted_ids],
                              'deferredMessageIds': [r.message_id for r in records if r.message_id not in accepted_ids],
                              'acceptedRecords': len(accepted_ids), 'acceptedBytes': accepted_bytes})
            count = sum(len(b.pairs) for b in claimed)
            context = hmac.new(key, f'PHPVPN-DATA-RESPONSE\ndata\n{rid}\n{count}\n'.encode('ascii'), 'sha256')
            for batch in claimed:
                for message_id, tag in batch.pairs:
                    context.update(f'{message_id}\n{tag}\n'.encode('ascii'))
            context.update(hashlib.sha256(result).hexdigest().encode('ascii'))
            tail = compact({'type': 'data-mac', 'requestId': rid, 'tag': b64encode(context.digest())})
            return Response(content_type='application/x-ndjson; charset=utf-8', body=result + b'\n' + tail + b'\n', batches=deque(claimed))
        except BaseException:
            for batch in claimed:
                self.storage.finish(batch)
            raise

    def handle(self, action, body):
        try:
            return getattr(self, action)(body)
        except ProtocolError as exc:
            return error_response(exc)
        except OSError:
            return error_response(ProtocolError(503, 'queue_unavailable'))
        except Exception:
            return error_response(ProtocolError(500))

    async def in_thread(self, function: Callable, *args, cancelled_result=None):
        future = asyncio.get_running_loop().run_in_executor(self.executor, function, *args)
        try:
            return await asyncio.shield(future)
        except asyncio.CancelledError:
            # A cancelled coroutine does not cancel an already running filesystem
            # operation. Wait for ownership to return before cleaning its result.
            while not future.done():
                try:
                    await asyncio.shield(future)
                except asyncio.CancelledError:
                    continue
            if not future.cancelled() and future.exception() is None and cancelled_result:
                await self.in_thread(cancelled_result, future.result())
            raise

    def cleanup(self, response):
        for batch in response.batches:
            self.storage.finish(batch)
        response.batches.clear()

    async def __call__(self, scope, receive, send):
        if scope['type'] == 'lifespan':
            while True:
                message = await receive()
                if message['type'] == 'lifespan.startup':
                    await send({'type': 'lifespan.startup.complete'})
                elif message['type'] == 'lifespan.shutdown':
                    self.executor.shutdown(wait=True)
                    await send({'type': 'lifespan.shutdown.complete'})
                    return
        if scope['type'] != 'http':
            return
        response, started, action = None, False, None
        try:
            query = dict(parse_qsl(scope.get('query_string', b'').decode('latin1'), keep_blank_values=True))
            action = query.get('action')
            headers = {k.lower(): v.decode('latin1') for k, v in scope['headers']}
            response = self.preflight(scope['method'], action, headers.get(b'content-type', ''), scope['path'],
                                      scope['headers'])
            if response is None:
                limit = {'health': self.cfg.MAX_AUTH_RECORD_BYTES, 'info': self.cfg.MAX_INFO_REQUEST_BODY_BYTES,
                         'data': self.cfg.MAX_REQUEST_BODY_BYTES}[action]
                length = headers.get(b'content-length')
                if length is not None and int(length) > limit:
                    raise ProtocolError(413)
                body = bytearray()
                while True:
                    message = await receive()
                    if message['type'] == 'http.disconnect':
                        return
                    chunk = message.get('body', b'')
                    if len(body) + len(chunk) > limit:
                        raise ProtocolError(413)
                    body.extend(chunk)
                    if not message.get('more_body', False):
                        break
                response = await self.in_thread(self.handle, action, bytes(body), cancelled_result=self.cleanup)
            await self.emit(response, scope['method'] == 'HEAD', send)
            started = True
        except ProtocolError as exc:
            response = error_response(exc)
            await self.emit(response, scope['method'] == 'HEAD', send)
        finally:
            if response is not None and response.batches:
                await self.in_thread(self.cleanup, response)
            if started and action == 'data' and self.storage is not None:
                await self.in_thread(self.storage.sweep)

    async def emit(self, response, head, send):
        headers = {'content-type': response.content_type, 'cache-control': 'no-store, no-cache, must-revalidate',
                   'pragma': 'no-cache', 'x-content-type-options': 'nosniff', **response.headers}
        if not response.batches:
            headers['content-length'] = str(len(response.body))
        await send({'type': 'http.response.start', 'status': response.status,
                    'headers': [(k.lower().encode('ascii'), v.encode('latin1')) for k, v in headers.items()]})
        if not head:
            while response.batches:
                batch = response.batches[0]
                try:
                    chunk = await self.in_thread(self.storage.read_batch, batch)
                except (OSError, ProtocolError):
                    # No authenticated trailer after a partial or damaged stream.
                    await send({'type': 'http.response.body', 'body': b'', 'more_body': False})
                    return
                await send({'type': 'http.response.body', 'body': chunk, 'more_body': True})
                await self.in_thread(self.storage.finish, batch)
                response.batches.popleft()
        await send({'type': 'http.response.body', 'body': b'' if head else response.body, 'more_body': False})


def create_server(cfg=None):
    import uvicorn
    # Instance, not import string. WEB_CONCURRENCY cannot override workers=1.
    app = Application(cfg)
    return uvicorn.Server(uvicorn.Config(
        app, host=cfg.LISTEN_HOST if cfg else LISTEN_HOST,
        port=cfg.LISTEN_PORT if cfg else LISTEN_PORT, workers=1, reload=False,
        loop='asyncio', http='h11', ws='none', interface='asgi3', lifespan='on',
        proxy_headers=False, access_log=False, server_header=False,
        timeout_graceful_shutdown=30))


def main():
    create_server().run()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# PHP VPN 1.0.2 (MIT License, Copyright (c) www.pilandrome.com)
"""PHPVPN v1 relay. Python 3.13, uvicorn and cryptography. MIT licensed.

Run this file directly, one process and one worker for either storage driver.
The wire contract is protocol.md. Operational data is not portable to PHP.
"""
from __future__ import annotations

# Configuration header. Administrators edit only this contiguous block.
SERVER_VERSION = '1.0.2'
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
# Client downloads offered in the select box of the status page, in this order. type 'file' shows
# a Download button, 'link' an Open button that opens the URL in a new tab. The optional os
# ('windows', 'macos' or 'linux') preselects the entry for the visitor's system.
CLIENT_DOWNLOADS = [
    {'name': 'Windows 10, 11+ (x64, arm64)', 'url': 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-win.zip', 'type': 'file', 'os': 'windows'},
    {'name': 'macOS', 'url': 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-macos.zip', 'type': 'file', 'os': 'macos'},
    {'name': 'Linux (DEB, RPM - x64, arm64)', 'url': 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-linux.zip', 'type': 'file', 'os': 'linux'},
    {'name': 'Source Code', 'url': 'https://github.com/pilandrome/phpvpn', 'type': 'link'},
]
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
            expected = (dict if name == 'ACCESS_KEYS' else list if name == 'CLIENT_DOWNLOADS'
                        else bool if name in ('STATUS_PAGE_ENABLED', 'PROTECT_BUFFER_DIRECTORY')
                        else str if name in ('SERVER_VERSION', 'SERVER_SECRET', 'STORAGE_DRIVER', 'BUFFER_DIRECTORY',
                                             'VPN_ADDRESS_PREFIX', 'LISTEN_HOST', 'SERVER_PATH') else int)
            if type(value) is not expected:
                raise ConfigError(name)
            if expected is int and value <= 0:
                raise ConfigError(name)
        if self.STORAGE_DRIVER not in ('disk', 'memory'):
            raise ConfigError('storage driver')
        if not all(is_download(entry) for entry in self.CLIENT_DOWNLOADS):
            raise ConfigError('CLIENT_DOWNLOADS')
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
            for entry in self.CLIENT_DOWNLOADS:
                entry['name'].encode('utf-8')
                entry['url'].encode('utf-8')
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


def is_download(entry) -> bool:
    """One CLIENT_DOWNLOADS entry: non-empty name and url, type 'file' or 'link', optional os."""
    return (type(entry) is dict and set(entry) <= {'name', 'url', 'type', 'os'}
            and all(type(entry.get(key)) is str and entry[key] != '' for key in ('name', 'url'))
            and entry.get('type') in ('file', 'link') and ('os' not in entry or entry['os'] in ('windows', 'macos', 'linux')))


def download_controls(downloads) -> bytes:
    """Select box with the CLIENT_DOWNLOADS entries and the button for the first one, so the button
    works without JavaScript. The page script preselects the visitor's system and keeps the button
    in step. A broken configuration still shows the status page, so invalid entries are left out."""
    entries = [entry for entry in downloads if is_download(entry)] if type(downloads) is list else []
    if not entries:
        return b''
    options = ''.join(f'<option value="{html.escape(entry["url"])}" data-type={entry["type"]}'
                      + (f' data-os="{entry["os"]}"' if 'os' in entry else '')
                      + f'>{html.escape(entry["name"])}</option>' for entry in entries)
    link = entries[0]['type'] == 'link'
    return (f'<div class=downloads><select id=o aria-label="Client application">{options}</select>'
            f'<a id=l href="{html.escape(entries[0]["url"])}"{" target=_blank" if link else ""} rel=noopener>'
            f'{"Open" if link else "Download"}</a></div>').encode('utf-8', 'replace')


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


STATUS_HTML = b'''<!doctype html><html lang=en><meta name=viewport content="width=device-width"><title>VPN server is running</title><style>*{box-sizing:border-box}body{margin:0;padding:2rem 1rem;min-height:100vh;display:flex;flex-direction:column;align-items:center;background:#eee;font:16px Arial,sans-serif}main{width:920px;max-width:100%;padding:1.5rem;background:#fff;border-radius:1rem;box-shadow:0 .4rem 1.5rem #0003}h1,h2{margin-top:0}aside{padding:1rem;background:#fee;color:#900;border:1px solid #e00;border-radius:.6rem}button{padding:.6rem;border:0;border-radius:.5rem;background:#06c;color:#fff;cursor:pointer}aside button{background:#e00;color:#fff}hr{border:0;border-top:1px solid #ddd;margin:1.5rem 0}.setup{display:grid;grid-template-columns:minmax(0,1fr) 240px;gap:1.5rem;align-items:start}.setup img{width:100%;max-width:240px;height:auto;border:1px solid #bbb}.setup p:first-child{margin-top:0}.setup li{margin:.75rem 0}#u{display:block;margin:.5rem 0;padding:.75rem;background:#eee;border-radius:.4rem;font-family:monospace;overflow-wrap:anywhere}@media(max-width:640px){.setup{grid-template-columns:1fr}.downloads{flex-direction:column;align-items:flex-start}}.downloads{display:flex;flex-wrap:wrap;gap:.75rem}.downloads select{min-width:0;max-width:100%;padding:.55rem;border:1px solid #bbb;border-radius:.5rem;background:#fff;font:inherit}.downloads a{padding:.6rem 1rem;border-radius:.5rem;background:#06c;color:#fff;text-decoration:none}#r:not([hidden]){display:flex;gap:.5rem}#k{min-width:0;flex:1;font-family:monospace}dialog{width:900px;max-width:calc(100% - 2rem);border:0;border-radius:1rem;padding:1.5rem}dialog::backdrop{background:#0006}#x,#z{float:right;padding:0 .5rem;background:none;color:#000;font-size:1.5rem}#t{width:100%;height:14rem;font-family:monospace;white-space:pre}#b,#a{text-align:right}#f{color:#777;font-size:.9rem}#f a{color:inherit}</style><main><h1>VPN server is running</h1><p>This server implements the Packet Handoff Protocol for virtual private networks (PHPVPN). The client application can be configured by following the instructions below.</p><aside><b>Warning:</b> The default value of SERVER_SECRET is still being used. The SERVER_SECRET variable in this script must be set to a secure value immediately. A secure value can be created using the Generate key button below.<p>Secure hexadecimal key: <button id=g>Generate key</button></p><div id=r hidden><input id=k aria-label="Generated key" readonly><button id=c>Copy key</button></div></aside><hr><section class=setup><div><p><strong>Step 1 - Download and install the client app</strong></p><p>To connect to this VPN server, the client application for your operating system must first be selected in the list below, downloaded and installed. The source code can be opened from the same list.</p>__CLIENT_DOWNLOADS__<p>After installation, the PHPVPN client app should be launched. On Windows, it can be opened from the system tray or located by searching for phpvpn.exe in the Start menu. A similar procedure can be followed on macOS and Linux.</p></div><img src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAPAAAAC0CAIAAAAl/ja/AAAACXBIWXMAAA7DAAAOwwHHb6hkAAAgAElEQVR42u1dB3xUVdZ/97XpJZMy6b2QUBJ6U5qIoiIqKmIX21p3bbtr2f10XV17wV5RUVFBUCkivUjvNY2Q3uv0mVe/8+aRYQgBAiQhwDvyi29eufV/z/2fW85FoihiXSaBwINj4Xne4/Go1Wocx+U7uF8wRRQ5WgS/wAXLsnChUqkCOEEIBV476rpLAX1irAfScbbSoEjPl2CwnhRIIGQ3gzhwDW0O9DTHcQqaFTkxoGma1mg0BEF0BOXdp6GDI/J6vXa7HboPiqJO2gQVucBZB8MwgGaTyUSS5LGYPgsaug2Thr9utxsSp9frkV+UalPkBAJGl8PhgP4cABOAchscnwXKEYA10AxIolarBQ0t+kWpM0VOIHJPDhxVBnEAyu1iGu8eHAf+Bi5kNCu1pUhHaLSM42D8HBf93ZamdofwFFHk9JTjWQa0Iop0j5Dna8YQAuJ1OHeiwLGcoFS2Auhz1owgKORrWbdyVVG1Q6U39x8yMiXajAlCJ3IdhBMkgaChKPypZ1X9eaibccLXUjDjuZdX7KwyhZo9taXFVdUCInGAOUXTNIX7TWN4DYSEWxTckW4QgRsA1SNqHp6TeOtv6TEthdJ0cO3s7353iXTrM0UUDd0lLZQkRdu3H3xWmzTq3w9M0pJI4KVxQgLHWEdjUUkFixlSMpJ1pOh22RgeczTU2jg6NSWBRpzNZkciW11ZS4fGJkaakIgwzl2QV8SpLUnx0TSBEYivLMqtc/Emk7F44/KFqz0x/bMGJkepSEKxdBVAd01+CNx+aN/mcs/UG4drcZ5hBBnktoo9s778waYyCA2NVGL23XffVrHs0zcXlQzOyao7tD9y5G1/uSz927f+cYBL6R0q7Kuw33T/02NTsPmzPtt00IXhbOaY628bm7Hst9lrdpSpTXqNPkwsOlhe4tywZV9KZESUieAVPCuA7hpbEHM21yBCF2YwHubMCEe8/devP22JuPzxuy4jnCWvvfDywo3DMhwNNkY36fb71Hnz/zV7yaHBMc3VVfrBN943fcz8d59avG6Tpar2j3z+yccfZQ78/s7ChYma+hXr86/8yz+HJeldbrZivbuS4m6//sooDc8JCpwVQHeNALI0Jgsl+jwsixMaTOARTore+pJDzuzxOQY1iamSB6clbi2uTNAbM7JTYsxqLjzOgLbY3R5VRHJ63xQVrYmNDt9SU3PoYFHpgaLPP5jB26txOraiYA9tyeyXHE7hfIhZU00g0d9aMIxXYKQAuqsEtKU5LntA4m8//7ok7b7rQzSEyDMeXhsRQRUcPCQMi8V99bll1ZHjw+lmgQcRMf8fXJ76gWspEEEUER0eFRWZob37kUcjtUDDUeWu+Wv3FlW1cOZw0ufzcZyAibiKxhSb8FwCtMgwUn3R9NlOJolJK7tP3rMDEgUy7Ia7H3zv829ffGF3eLjZ52RHXDd18j13vffBnP8r34C8DjppzK2X9i6cOx9XqzCEIVpFaNQ4Tct/Jc6togVc1XvcdYMPvv/+Jx9HGbH4rMuuG33N8IKPPnj5+choY3L/CeOyhlGLvnhv1vx7brgyxkhz2AXOOhAUPcZx2Nk2jo+/fJTn3W/NYH76GWpKdetNmr8+hJ3WsrhA+PLWA8y/EtpmsxkMhuDVgCcixaA2d+4W6+rB4usg7SAoWmTsh4oOOTwCqdLFxMWFhugctWUlFXU8ZUxJSzJQyGFrcHG0xazHOG+z3aU3GD1OG6kBi4/2OppcHGkJDRFdDfmHKniMtMYmWC1GjHWWFB6ysWJETEKkRd9YVVLrwuLjYnWqC36UQxCRSkUMHogZ9JjQyXNY0Hm63W6AjclkknesBBZpBq9VOgmgRaerZcAw0WYXeQGPCDd8N5Psn33aaA5eVgIps9vtHQU0QWAut+eOe7glS5FOdyrjdzhJSMPJIibynEQipHFnHAiCyMk/cXgqSs0MQSkhuEI44JIXoW4Cj1oDkcgJvIAQQUjjzjwwG3gI4fmvlSE70FIo0qqZ/yORlQm9ehcB2mg0SlXVKoGnHaYc0P+SJKJxobaWWbn6NADdaQJcVaPFNJpT+og7kkvKj+zWO60/AzgUWg1KDKOwox+1fkLKxcbLNqC/KQryhxSlMFepQNRqrAdsDCVPoF2xQFMTRKxHLcNXVGJPYc49ziI+LqCRiiaHDGKWr4IOGfp9PjdftNmQydQjUq1sEe8ZeqUHWsLkCfgGdeVEZvEfEvGgSO7PDUJtHXF2AQ0WgM9HDBuqfu7vojKXcda1M0WxP85lZn4D5uC5AGgQhj1isQq8UN9ApKed9kBHm00rp2tNC8hsIgYPUuB89gEN5sSGjZ0+pnFiIAWDp92NWCcCNJmZQSQl8tU1iKaFxibml9+oEcO6jja172sGbkoxooCdhkQR53llMdDZBzQYyrwQ3H/iSJo4Fc+YEApn0EhOBGg8PQ2PjeFLyyTWIQgiKOyuNAJcLhfDMG23PRIE5vEgjguMO3Ic22y3Y7wy4Xy2Ra3Cfb7W2kKCKNqdTgyq5oyH7dRqNX26c3nkSXRmVLQ0IgPqkCDFujrRZkcmY1exH2jxxzYYADQnwTlgh8A7KpoWFUCfdQ1Nq3gCb9WlIpIGMCmomzPvOoN9ynSqhjYa6CsvYxb/LjFXtYpdtY7PLyCHDOosDnSMFUq3mzmI3U0QnCjKsyQETmjUalFQtlSdfaPQR5JePx2UO09QNIRKLeLEGYbcLkiOBU+7bgxONlfH+u1C+TMcYZ09CXTSbEidg9+YbC23kzcMRc6O+KtEPNu7+k/C3/G0FDwuRrZkoZd3f/iJUnGK9GQ5CaDJ7H5EasphCwwano9RikyRcxjQ0hsR4fLMHMKR6HKJLS1KqSlyDgNadfddh9ffgGW2Zx+zdIVSaoqcw4AGviFy7BETzetTSk2Rc5lyWEKIlBRpMwIQD5ZjFi4Wmpp6fsaQ5IVD8qkB/4iObZNC/oMxpFFvgqRI4oxTcHhMiSBI8pQGViVnIFSwJ5DW9CNSCklKHH46M1zAGc/D/WKnsh5aBnRcLD3qYs/+A8hgkFa9t9gwvqePARMkLXqad+/bW+9gSFKdlNE7JtxwkqFrhAR3048fvVmAMiZfNlCnD02Ot57JRhR5A4EosJXFeW7clBQXgToQGEK4z9FwsMoWnZKoI3GBdR8qqTVYokKNakH0FhdW4WqVx+4Kj08ya6mOpw4agb1869c/bB4/7fbMaC1//i7t6tC0u+j1HF6CjOOi1ys292i7EHSzqyHvi9dnzFvyZ0Fh4fYNa/eVVIo4gU5W5SVbF66uNN/7l6mOXb9/v3gLi/DTVWiIBHNj1fzl6wtwUty+8rcFq3fyJ0tAa4UQfHPBOy/9Z11+M+hpd8WOfz760NerckFPMzV733/9nTVbdsz7ae6hBhdB4KdSJoj3tBTmFTq8/PntYL5Du77JEcPQ/N8wnw+p1XxePrdhI5Ge2lNbKEFizgVffF2gzfnXIzeaNQgTWI+PlSpfZG12p4hoo8lEYjzLc1DRPreTEUmTyYhEtq6+zhgZqhLwwVdPz+JpldR+kaO5hcFwnVZLSHt/pA3hBEATQoXPkV8Hi4LH6yVoDY04p9MtkmqTyYT5GravWXzIjA0akDz2uukX4yoSE0kC2e02LyNq9UadhpQcSEoa3OfysnBHTeG8IIg8b4jr0y9JW5BfhOVE1lcV++obSorKPdiQmuIilyZk8MixV466SK3X2luaGU6QCAlBqlQq3udyehhKA0GreI6HgH1er4DTJr2WcdtbXB4PI9C06rw/LqFjgM7ph/Rawe0GEgccU3C6ei7ZIAhHyb61h2w3PXGJWSMwDO+fklU76gq+++yLvEYv2+KNG37pfXdOObTgtbd/L8uMt5YUF/W/7q83DzT8sWL9unw+LjGpH757XWPS3++9dNWcL+ctPRASF+YqK9INuuWvl1te/WrFPQ8+3iea//ztl9GA24fh2974frklYcDEiROIik0rthY0u7kB46aOiqwGVVqEuaITLfHOXQdRyoNTL1n/63cL1u/0MThSR9z+t4czsLKXXn1VCMnkmyu4iD6PPnBfSijFCQKiTH0yYubu39PiHbRrV2nfCZdVNxyos116qOCgNXmghan4bNacUddMKVq+cF91Q8H2zZqBN73216vWzP9xf1mjB9deM+3+VCz3tfe/EMKzRk+YPCC85fuv57bwatpXf7BBRZzveyM6lD3caCQSE2XWAUqamf+r6HT20PwgzNFUjeM6i14v+F3oSqnm7b/O/LDaNPLVd2a89/ZTnt0LF2wsZjwtzV7trU/+53+PXrNt2bwGbfptN1w1aeodj958qZq1N7u9ZTuXzVlbcd/L77zy3+cGxqrrm508x7TY7KxEQKEAWlwMyzlbbIz57kceGT8gJXPkxGuvGp9lYn/5+RdNxugpEy+dcusjj0wZK3pbbG5v/pbF8/4svf3JNz74aMblvfjvv1/Y5PE01Db2Gn/L22+9EOrOXb0jV154KWB4Ut+BWGNRQVFeab1vzBWXJwktB3KLcsuciZlpKsQ2N9YzVNQdjz37wl9vTk/vP/W6q6JCw0Zddf0VY4aom4sWLN/gdLsaHei66Y/cMNT881ezTcNueevN16ZfO5LCOD54CcEFC2gUEU5fMlaaLwS7iiD4QyU9dvUmYE2tN9MiC4xCZq04QQls3cFCW/+hAzWYQJpShmemlBWVe2l9Zv+BVj2mNkfpkdPu9rAcz7Isw8vjDGJF8SGTtXd6uBbDtJnZfcIoQvI/A10AdNvwAklKTpMIVUJautVAOetKfv7yjcVbKg1hITTF+xgfx3Es45PwKe0V5ytKCs2RWWnROuhFBg8YLtTXV7vZiKSs3snhGKWxmrQuV4u8IVcURGt0ShjJrVu2pIKxZg0akG7FN6xZUeYg05PjCIyXVkESBMY0ffPpN6o+l145JLlq/7ovP/0wt040hRhF0ctgRFRSSmyo3lFX1uTUDMqU+GFkSkaK1YKE83yrT4c7ICgH/vAqJTB5RLujZ+aH54SQhOw+cfy8hctaGJKmaVzwMYI2LIw4eLBYyi/TkFtaGRYVSgkCK/kzAADx/lqWPBcc3tkl3RINRn1tzaE6D9zxFhQU1nOCWq0lvHaHgGO8q6qiieX8xFwa5sMq9q/dXKS954G7Lu6bSAi8NGbCA9shpRglhwi4JSy8pam01il1GkWH8jGD3kyRHLBdKFW/x6bA8itIgzoiIbuveem8eaaM3la1MSe717afF9Qak9LizCIvHa4KNbB7xc+leMYD06+hcX776qWukIvuv3NqrxiDKIUkSM2JEyi1AQyHg9UNEGx9aVFBQ7NwvpPojroCI/pm4RFhotONEbhgs3tnztL+++meqKGBZ1CWqbffNePzH//3n71x0RZXs2vgFddefdu0Dz6Z83L1duRuYCKHTh2dWTwPax0elpxAA1dB/hFjEfkNSx7vNWRC9uZ333jxPwMzrbv2lQq9OH1k1vheum8+eGNXDL6nvHmCNEAsfQL2YVRKPyu5+5MZ74vVG+1MFE7rUjMzly36dU6SFmEUgYjewyf22f3Rh6++GBOural1XnPbPRHqKlxaAn54jEXECRnTYM+BFZqWGNnQjFLSkiGJURm9NHiDNSYpXINsGKLV2pbiDd988b0vZtRvX30UmpQZ3Wug69e177xbsm9Lrqb/AL8nbBKKQhOePvGy7C9nf2jL7+Ot2OnlMBI/z12XdfTgTRHs80lT+LwCRNOi26O6/Wb926+d/Kug3YSgV+Rln6A8HA7HqTmaue9BbulKpNeJHg95+QTtzE9OPKgM9cm7Gg4cKLB5eYo2JKWnRVn0DeUFhSXVPG3K7J1p0RAtDdU2ThUVESL6XNUNLaERUcjb1OIlIsJDPM21zQwVExWBeZvyDhwSdPrCP2btRcP+8ejVwC32HMgXVKYQk94QEq4VXE0eMSI8TEWKjRXFhRVNprAwrVpjtVopwZ2fn89qI2KMJItU1vBwwdOYl5vf7MGiktLSYsN8LltdY4spLFJHiQ31dZjKFGrSHTZUEOLcttJqmzU2Wq8iBZ6priimzXHhJi3HuOsbbTQpVFfVeIFXcYLWEpWRFFVXXFBt58LCQ7V6g1mDN9nclrAINQ1FzBzKza20M9boKA3CTeHhWhrvlAWegATm3fe9L76CNBrJ0Yw1QvPjLKJXRhc5mmEYBjDjZ3wo2HPSKU+sBKwtPCycxwpkysFv28EXFJ7entnuIB4si6st2UNG+vMqQrVDcYTEpA2PS5d/srxgCIsxYSJ0+UilT4g38qBm6YgYE9wR9JZoM4k7KvYs/DMvJjEZFW1fWyFMvHWAWgT4hA4advHhJgr8BtPGh2D+gxdRaFx6RIJfQfgbLUbo+vQfAi9JriCliBhcbe43eATyMxqOZQmVPi7OKPid8UZExsnUJ6AISF1Ir16h8tAeIuiElD6gcSFtBK2NjTXAC5HRia2dkjRMl5jVP9k/iAg/gcXEGUIkl04CzyIquc+AFMkzlJTm897PU0cBjfQG1Q3XsmvXydMrQnX1aW8C7x4RAB4MfzS9ZoN/++HYSlPkS5HnDn/LMYA3szWM3LNz83pKZbr1vkcHpUcK/hNVmKOCPRJKm/AhWLZVVwmtyGODvw3E68fZMdyJPxKR5D+NCVxLrcXvnSz4fY49Si8GQpaSwV5Ai37JU1L9R1wWARM4v80LkcdUoZdef+ulrbBgWA5T5PwZ5YBXo6Pw0FBpwI4kJa8G837FuPO6jkXp2PTDoqD5/AM0ddEIIqsXdH6SbgbW5nIpxafIOQxoae+3VisTD6RRs1u38/kFSgkqcs4CmiA0Dz+ASEJi0sA6SkqFunqlBBU5ZwHtH98QA4uhz3u7UJHzHtDIaMCtVskulNYzIO/MWR20C4OnbxSXGoqcvqEeNFXXCYAmeqXT40aJbu9hUl1WprhqVuRcphygmDUajMQx/7oz4NDsth09M2MI4XSrkET3tTqc8K+I6hgXQzgBLxPKyXBnDdAYRl9xOR4eJu+ZFapr+J27e2CuSJLibFW/fv/pW2+99f7HX+48VI8IohtQg5NEc/ne3xatbPLyJ93HiuMkY69c/Nvi4kZXN+xeFRVAt69U9Hr/SsvDdYKRPe7oTpwgGks2v/z0m4Ut2oFDB8cayRZ7C7B+nGxV2DgKqNKACico/xVFHvcRLT0iSEreEC4pV2lvtrQrmzr8Ak0g5G6q3L0n381KS21bb9MIodZLCkITnAe/ef+jgzacZB17d+1pdEpOaYmjk0dAowwE2wbuRzof6fSjQGopaYV228RDGOXbfv3yu2UcSQcSCm8eyVVr9xVILd9S+N0nXxQ18lLMraF1Zy93Rrrs1AGtAw3NNTdLXsgJgl2xWnX9tV3nY/eUk4eTONPww2ffqUZf/9itoyXojbyY51hQgfaagh17CnzI0n/ogHAD5WqornMy7obKahuWM2wg3pC3Lbc8PD0nJ8nqbqqps/s8jVVVNiF72CCyMX/bgbLQ1OwBKRFN1YdaREOC1cK6mw7VOaIiI+xVZbzAlRwsJiJSB2UlR6QOvSW0T6gGsCCU7t20v7TRHJ2SnRlfsndTQZU9JKnPkIzIvC2rFy37wxbZ55rRfW658yaN2QDwstfkbd11UKDDsgdlRxiohpoKD8fVlxY3I1NOv94WLSlv1YaGRAi+vG0bD9a6rIlp/TKSeVvFxp177ay294AB8WE6Z3NNvc3raa6paPT1GTwiDK9bt3zx4jxDXGbioESzy+FsarHpI5MiKOfePQdcpLl//5wIgwonUPn+LftL6jXhsahi1eLla20hSforRoViDRt27PXgpqx+/aJMWkHs8Rv+n3/++VPTf2YTX1TMbduOKEpa8s9xquuvkTwcdFgEQZpSVqlUeEcMSskZCMstWCQUFYOWg+jw1BTqmknHOwiLpMjmoo2zluVeN21agplkWOmEQQBBbf6f7737bTNON+Rt+mPjgfSBw22bv37ouS9xg7kub83sH5YUVTl87rJfflsV3XsYyp3zwNOfiAZzQ8G62d//XlhpZ7wVvy5YGdV7YOmKz+bsF8YPzbQdWv/6F4sysrIWv//kp6sqIrTMol9/5qOHhNu3vPb5gr5DB+at/HHmb1uA+5TUuqM1njXbcllf86rfl5NhqVjD7uXrcy2JfVJCfF998aUuYzBds/29D2Y7KVXN3g0rth/sM2hw7sJ3n373J21IyIH1C7bX0oP6pNG4KNEUwbVw9ue//JmHMG9loxijd8x8/8sSN++pyl+09M/o7OHsvjmP/nuGmw615a1csr+5d0r0/g2/763k0/v1pytXP/PKBw5tTGpseEX+1twKe03+pk15jpyctH0rZn80d4NGzeaXNZO2sh25ZcbYXgkWbtHs2UU2X0tzHaaKSYkzBx9tI+3D2byFW/unhARBAE1H3XAdHhbW6buZoH9jWakiATPBC0exM1o+eqQJEMholEegJY/NkkNytgfZghjmdTaraINJqzu8GBOUM9c47+tZpmF3/PXGEchX/86//7Vo7d4xIh8Sm3XtHXdF1W+8/5FXk+974uZhMdS/H1q1ed9VJs4cA4+mxzVt/stDLyXe89htI+NVzz+6atOeLI71MlJ+RZ5ze7y8wHs9Qu9h426/Y1yU8PSCtRsHjMIZlmuu3L9s1Z5xtz55VU60tFFXcI8Q+Pzcg66KXat2l/7jspE5fSqvvPraPvTerzw+n7Nq3vxfIoZOeeCGi5Cz9NUX3li6MTeS5SxxWVPvnu7ZbHz227XlV12cEaaBllmTv+XPnZXXPvLcsCSjILjnvPlsc8y4Zx+YpMVc37z6rwV/bL3aymlNcZOn3ZbG5jz2wiel3qsuGja4RBd69YTBNX9s1Jh6TZs2JcNMNxgFVjiwv8q9efOf4y+OWbp02yU3PTl5UCwnYM0HV+0p5q6ePCmB3zgrr2r4XdeNGdQrVEVKq1TPPw4NAhwDDwuVGiVJ8uXlPcrbHWgQXWiUSnA2OZ04IR+jS4pcS0M9nxAfTyIRUZb0mKjG2kYGpy2hViBQBK2LiEwODdFAWzdrzT6nl4OXLNIjnNaFR6WGmrUI4SZtCOP2sjgiCekQTpz0OzASBcJkTYq34vCC2YD5gA4jiiRtNQc9yJIWGyVxUxW+/feZn89ZxmpDoyMtJC7410/z0pJRJO144b3NNgcWHx0jbYAxWFPDwuobGhm1IT45EZKgMRg0os8jbW/FIL6GikJMHZMYYfY7UvLU17hi4hO1BPzQpccnOOubXIiMSEgL01MYZTKpBDfjlpZFS/9JxNwYGW6gSXfNvs/fe3tnhSs2Ls5ioFqqi1xiSHpctCgKJEkgQeAE3ufjLPHDp1x30d4/Zv7n1U/3lDXh5DlAo08niXhCLCZvXgI97XaLjT3IMxhoEWNsv5GZmtnfzy2t94ii6HM2uRh9XLx61649dobztRTvKKpKSI1VcRzD+Ve7Q/2xjLy4XtoB4r/DcKx8wTG+1kcMx1PhZp29rqKFYWqK88qr3ZKnDo5h/QuhBfljUWA5zhAWxzG1OwuKQVt7nTVb12zSJY+/fvJIA8n7GFAEyA9qoGwQNUtprBGhaNeBfW4f46gq2FPTkJwQRUKaGFaU1++D2hTl5oos1ni7vXh/aQ3LMg47GZdgztuzo87J+ty1W3MPWpNjdCJEwfh1qbSzEDIonQYN3Es69ZkH+ibiuL1q3/5SfPzkGwemWTxOnz4sTuRrd+QVsRzf0uLipGTx0lpDhPe75MbH//a3FM+en5Zv82Ek0eMHGE9rjIIX8ZAQHnAMBJdWsZu2qOrqJa+7PUGACuC6yXc+0vz1N+/874XwcIvXwYyYMu2a6fd//N7Xr760HWM81oGTr704rXrpn9YwEpc2h9Dh1ggtRUDd60PCLEY1rTNaw3kCQETSYdYIPS09MljCzFpV/9HXrt3y7isvvaLzVoVEx6so0hxmNWlpCEatt1hDDJRKFxZiikgcfMt1dV/P/jB/hSksLXvApVf/+Pv8/9WvLWngUgdrtREZA9JD5373KT4hxxoRrjVEXHvzzZ98PPt/L+8QvN7owVdOHJq6ucwcFmJEQJtprdUaTvu1I2j16KzRUy6pXPDx6+utxtjM0VdPubdi5ldv/e+/aiSorMPvumKQY1NueBhJSl9SYRERapJMzhmlXfXtVz8sHRluiLHSuMBbUocOSt45662XdFg1bQwLjcu+c1rTx99+WrxOq44ZdMcV/bMT1//y43eucRkFK7Y6SMJtTLliVF9aPAdciKHTm4j2fvGV66lnpCE8lsX0OuP8n8g+We2gK+hswoAAwXc6nV26p1DqyEWmsrS0xcNSakNkpNWgUzOO+rLKOp4yJibG0Bjv87g8LORAC9rS6fLQWp2KRF6Xk0MqFcF7GFF6JHJOh1t6ROFel4tFlNGg9TbXltc2G8OsWppUq2if1w2Y09Ak63V5eEJLIyAmGviYwpuqKqpbnKawqKgwQ31FaQtPR4ToCRLe1rDulvK6ZrMljMYESqPVqNWMva6sog7TmOPjolRAut0uVsC1Oi3Gep1eRqPVycN5GFAkJNSVl9W72VBrFLQ/0WsrL6vyYOq4hHg9jfncLjcr6nWQeN7ldlEqvUZFNtVVtniJcIvev2JSA7yCcTZVVDVqLeF6FU6rdRo11VJVXt3kMIRFRoaFsK6mynqbKcTM2pqaPYwpPC46VNdmj8xZ2VOI+wVrPQSwM4xCOQ6dDmttrYiie5pXS2k7FCJjUjJi5V+s5COD1IWmZ0I3IkIvDz0ypTGodRJFEQnSHGKWduKJosZgRtLyK1yt9T/CSbMlpPWRUevfj6gJicwMjRL9e36hc6aNIf7/i1KA/o9D1Br4lmEFc3S8JUbyZwCduDU+NVLyUCBv+uNpXUhGqkWCCMIlJwYsQ+nDMnpH+HdYsUAx1DqDRlRfdHIAACAASURBVDoFRECUOkStPbIXUOAZDIUlpET43ZFBXhBlSM7IkvdKAsUg1foQjaTLIfFGk0UKnBdDI+PDpX2NIlQU/IYU0HqpNPw7DeX4WWNUXEg0kmgJx9F6S5opFIg3ZgmNQf7NY9y5cezYaQKaHDSA7N+P25crHePV1OSb95s2K7Nn5SxoS99hexHY7lE7+lq3A7bu0pPfOUzF23nEt9L0o/YOBrYDiq0ft34htZzAa2029gUlRmg3eYHoxKCth4G0B+8gPGr3YVAyJHcfrUkJpEQ4Tmlg/p3F/LFPz7Xj807TbsXDwpDZLOUWx0WfT6iuwRRR5NwFtDSvIa0g9VMOlYo/kCuUlimlqci5CmhkNqlunCJZA6CkaZrff0Aor1RKU5FzVkNLy21U/lMxRfngZExFK6WpyDkLaAyjLh5B9ut72GgQBN+PPx8P+tJwNUEoWwEU6bmjHBJQw8OQNAF+eKSZLyxsH80sK1RWSVrcapW0uHLovCI9U0P7v279nMCF2jo+7xivBoLgnfGh47ppzhtu8fzzX5jXq+yrVaTnAlo1/Q5MJZ3JiXBcKC7htm5r8wKXX+j75jt4xJeVs/N+EVts3QRoaf5ImlJqM40kLZcm/TNubSeYjg2gvc9P9AFOUjRFEkhpsecuoPFIa2C+UNLW8oGzwYBetYYvr0B6HVKpMPjXbURK8trcdlYfoCYwzvLSMqdPPmsHl1bikcec+effD4JL2yZFnOjQfj9APyEwh/Zt25NXAYVAkYq1cA5yaBk4yGQS7XZpeoVl2Q2b6Ksm+vdo+QUQYXdgfmjI6/SxbnBggAgS8/y5cPbSHWUmvYpWJ0yadkW8dMifiFOUq2LPe6/9dsPfHvMcWNlo7H3lRVnNzc20zqii8FYwQzvw7Vm+cMnOPCRCEw2ZdPNNqaHqEx7kIB2ltGvpr18t2jZ83MR925aFZ148dkCqqFgL55yGJuLj6InjRY9HIhKCwO/dF+zwTqiqZhYvwXBpfENkWHL4UMxolJdAdCnZwDFm17o/m3VJ48dPGDt6YKiGEv2amPAf08ZLCyUwWq3RarXe+v0zP/osr8Z3eJsg0AbkWf7TFzN/2Zdz0aUgA3vFUkhA0uJn/xGu8Ffe8Cddtt4hCMS79ubuTRx1/U2TR4SoNZR/bW3gFarDR9kqcmIJrG87Uw0t2mzsjp2Y24vCLNTQIUfAQ1HS7hU5AoIQnS6xsloazZAB7XKLjtbDsjweYgQA2oD5uuOocEplSM3q3z+nN+Zf4iwKvgPbNx+scdN8tUdykqNO7t3PgXS52xat2bSBieoTOnl0fKgOaHNd3rbFa4uueeK5celhclDSwiTWvW/HhsJqu0YXMXD44HCtcDB3n8PH1JdXiaao4UOHoLqCA4XFzqZN27PjMvv3R7oQAhcrc3fu2F9KGk0hBmN4TFJitFnxsNNTNLT3m+8dU2523HKnY9odvp9+DmYO1OjReGQk8A1pArywiF234QiBXr1WqKtDNAWgQGYj6r4F0wiJnl1rf//5558XrtvFcMzaX2d+Mne13dZStGd3YZ2NIoUVcz/7dslGHw/qHEmbqnhROtkKE0oLd/oMMb2iLNLyH78XXZF3LP3pi9kLNrlczn3r5r/79c92j3vBJ//+v/d+sjlaVvz86Td/bAXM89IJWrzP1fjzVzMWbiuszF399ieziusdzaU7X3vumXnrD+I9b3v8hcuhhaJDGC8gswlzud1P/wuFWqRT3uQGkZSIadRYk38VF3SsWs2Rr8orMKcTA5Lt9hDJGeSI4ZjQPXuGocEJjM8LcCR40d1wcOXqfRdN+8fUi5JshRH7iheIvABEgFYbsodc1Gdj3RVXTe4VrQLwIhyxHpdeE66S9qPyMrVoLNqxbF3h1Y+/ODYzhKvZ9rdnZu0sHUzqrMMGXXHTbeMTtS3f7N6muuqukUNynAkTR/SJ3jKfVpHeDX8s16eO++uDk5FQU3ewiMAU5dyTNDQy6CVHJQBHmhZsdt9XsyRDsDUApFa3qmuaXb9RGmyGt5ubub37MXgkigAUYuAA/yxMd22CR9qcUROn3TT1mjH9acHlZVXhRp0EUJVOp6Zb0SWyPg9oYY/Hd3i7ACLC41MZW3mTk8WlAThpUyLPOHlcF2qS8kgaQkNo2un1IY0hPMzgR7yKxHgfy3h9jNfjbj3YknE4fWZDiD8SQm/QIQXOPQrQqttvocaMEv30F2k0zKI/XE89I9TWSTUaE0NPvEzyZyAIwC7YVWtEjwRoeMpu2iw5HhAklyvkoP7+PQHdpKF5jnE67HZgCS4PbYwK1TsXr9vgcNl3rFmx4VClQBDS0nieJygVxrIOu42Tdw3yWGKfi/tEut/7aFZxnd3rcQJXZvVJkXrb0hXrHV73jlWr6vUhWVFmzueTffrz0iY9/8GA0iJ+QY6aEzSJCeHbdqzJq26pPbB95eY9LgJMY8Uu7DGUg0hLVd86jdu0RapzMOq1Gt/sObjVqv3Pv+AnHmLG/EdN+hsIzpeUkiFmoN1IVoSgoQ0GyXtYt1nCGBWTFLVh9Q8v7F2I0/opdz06/YHp73/807PPb0uIMV0yYpBRRUTEJvu0ZlNkr1GD4hb+8LlOf/+oDKt0+Kwu6ub7//rLd9++/d//o0hEqpPufereu+6785uvf3runwvVuqjb7rkjJUITk5hGhUq+dQxhcelJPImIiJgkQ5hBwMjYpHTSHDJqzPQq2xczXnwhLjU2KirKiOMCpmjpbumbO2p6C4L3g0/cr7wu2U6AXYbFIyN0771FjbqI373Xftd9YnWN5BbM46Fvmap/903HbdPZJcskyuFjyMED9Qvm8tKCD17eU+hyubpyTyGSRr0ZhvcffStN4FGkyDIelqdVagpHfv8BOJLXdCMRWAdBUIHZFZwAtsG73T5BFAlSBbAGJi1wPo+PJWm1isJBF8uD6yDyljZRkP2eSG5YkDRPI3pcdhajdSrKXr77g0/nZF/70OThCdz5dVBL9+8p1Ov18p5CeR4YO7NzCnH1Q/cDntyvvSVlQEXzNbWuRx7Tvf8uNSAHt1i4yio5YKTRSnPdxaWHXR2AZjLoD5+R2l1aWjqSnKSI1sxJO5EQDqaexAj4w6+IrfqcJI+a4JR3ZlE0HdD4nORJB1epVf4dWYdXFwaNjLYG1BowQZENBzd9u2C7Wq93N9aYBk4YlR0rcMo8S4+hHAFMq2672ffbQj6/EAxBaKB8RZX7hZcMX3+GUWRgNFqoqGD/3CgNcchLl+CrW26SFnuw7LldVB1ujtBoojJG3qSObXL6tMbwtLRECuN4QaEcPQ3QAM6oSP2Mt5wPPCptuKIoUNX8/gPeWd+rrr2a27FLBjS3fSfS6aUTlCW9KPkpRRbLBVWmoK0JlT41K9t/vpJ0aiyvgLlHjXIc1QIG5KjvnS7K6lY+mOLjz31z5iO5j5bmC53MwkWi1ytde33EkIF4arIoCBcYpqXzW6WZGZZTVHOPBjSIetoN2nvvkiaxpc1XhHTU9Z69wSReWr8hC8cSsTGkNQIPEmWBpSJnk3IIAigbSY6Mh5Ck8PcncLcb+/FngaIkPU3T7X6JaTTelCTe4ZA8pviXJfmHOnhlVYMiZwfQbre7qampurq6rKystrb2iDMotVqlIserVGE8zx1H4+KiyKnVS3zu+lnfENK0gzx5wWu12okTJ5rNZqX0FelWQNtstoqKii1btixatAgw7XA4JOfhrfAFHG/UUf91YXpB5NuDNI3wpRj3+uzvpbOUW1UyhBAZGTlu3Lhj32+fivhpejtvKrzlbAtqrwZlL+RnblV3PqBBNxcXF8+fPx/Q7HQ6A9w3EBnQ5DUU/pKO/K+LJeS1PMcE3YCQEyGVcGQN6wnSCqxGONZ2BI7O+ILX18O1l/FhvKBA6uxbvkG1ADUERjACy+qMx2cJv3QmoDmOq6+v37Bhw++//+5yuahj9lbJohGxDSSxhhQuZ3jP0c0SktOEUC6BkwgROAoG9PGMQv+YANsOoH0+XOADRxAIAu/x+jp44KciXTukwHKoVTlLrrgZn+jzYr4znSnUaDSdDGiPx1NeXr548WLQzSeYoMYxzIdhr2kpGhPHMIIPHfXIhmO7qVMYRQFu3W5rxSjKQ5KcKB2A4ffmTOtCQhSjsidQDp9O65W5hyhCD67V6QmjSejsqe9OALTP58vLy6uqqjrpuT7wfROOvaClYnkmjRc9h/3dSYBuRohDnbHGLBi8OC622KTt5cro7lkXmjoyH9xaUWe9VtoHNJhuNTU1AOuOHFRFi6CM0fdq8gk3pxJbDUQR+0lF1CGk6cQsgpKmaWHHTve1Nylw6gkkWqIa3biZv0Ms6HgPgNEKHZ7eo0Rsnop4V0O6kWQdQka/VRMraELdFQ1WGcPuueMcZ1/IzsoZ6OlfVMRa+vAkYSNCvN807IJiVAbsFOliQAdUfVMr2vDORTPYGV4PRigOXHqqsCzm9WE9YMVOJ29F7nzESRsZKfqpx6k7bsNIQkFOT6XTIpBpPCpKPNvDqWTPLymwo4nhQ0+1XQWaFn+06Y38/5RZma7S012mpINdzJxgWuMccRZxikObHM96PV5WmqintAYtGZRnQZreEmWfR9IBO8qMo8Khj2o30jGconAyP26iPJ/ZDRnCCUp0zvn8jbnb6hKjzCpNyq0PTksN0cinwVI0tW/JzJkbPP945m6+MJcxJMRGaAVlSFsB9GGYIqRimPGCEGOxCLIWlLxw4Mg/ei3Iy0T9XYLg8Wyz2XbpdF2fI+m04rrK5j6X3fb45AG45NWOwqWmBCkjESZG9b5oWiQeQjg+/fRNNPrvf7mqFxKUDSUKoOXuGxQiw4wZPnzivfcePmwFbrKsdBokBK1SycjGKMpVWNgwY8YOn697VvcTJKXV6fU6aS696dDWX9buv+iKG82unUs21PbrZams9+HOxnU7d7tts+MsUycMyfS73lVQfcEDWt7IzYH+I0l5eQqYuvt//LFw6VKCooY9/HBEVpYIICZJjqKE7jtjBZLiXDnnc/ZAjC5xwM3jM3wVu5evsOoqNzZEjREb98xbXPvMk1N6JScIg0b0TYnGkahM1pw30hkgk1zJCZLPDcnPp2ArKaneu7dq925vc/Ph+/C3W0coIUGqlOxhEyZOHDswQ2dJnH733UWrvtzsjrxjyjgTTZK0yhIWaQ21xCWkJkVZkKKcFQ3dFj8UhebNw/buRTSdtm9fqF95W376SVy5EkVE4LfcgojuHUJGVExyr4E5OZLXaiQ63G6CRRStEfyqGPn5PccLiOcEQRnlUADdjqLHsR070MqVAJdwDDPDDZ4nN2+W0JOSgt1wQzfnimPclWVFeYVmsE9Nav6nOXOzJt3tOrDqu8WbJxglV0kCqbJoNfsO5lc3RkeYtAoOFEAfGxIpK79iDFuKkFUUrwRrENQ3SXbKtpwOdxegclWDxo2qXrf7u6/3kjpT/769LEnDxl9+KdnPvHRXixja95rLktXIOOnO6aXfLVm9M+66MTmkYhQqgD6e6BFKwzDj2VqLJQocRg+7/Jbhlx+ts1lGTBo8LRUB3vv0RyzDYAmDn/3XEExyoMEpYFYAfYz4989KlEMUxx4ea5B+Ip7v9jEEkW13ZpHnmOD5H55lFHdzCqDbs8Ck42KxG2/ERo0CozB/wYKyDRswms6ZNi08PV3QaDCtVmxokN8UT7itUBFFTjj6cFiQPEaM2jltspM0tCCI/fqJOTlIo6nZs+fAhg04QqmDB4eNHCl4PFI3HzSYIAiCz+dTOKsipwTl4L8BQHeZhsZx5F9pBTHo9HpLZCSiKBpuApp9PriW9gLKXgxx3O12l5eXp6SkKPV0bslZ6VdlV1ssy8ogBm1IEIR0Jiogyo/vTl5tJ01xc5x8WJvkTYbnc66+uvf48fBTZTBgLhfhnxInGQZvjd5uty9atCg1NTU2NpamaYV+nBMKUvaa0v39qtyfA6ZVHdu8SJ5pRml62969LW++Kfh9x0qQJQ6fdy0EfNghxDudNQ4H4T9eiCTJ1atXe73esWPHRkZG4riyD6Wno1mtVkNNqQOnQ3VvnwAIoSgqgJMT22BnBGjJLwdFzWVZMT//xCWC/A4d8VbqA81u7dq1mzZtOg80dOcqrR5YGqAdgR8+9NBD6enpoIw6Jb8dzKYMYqJVRXbk207g0BxNt+999OgqR35PMYE0QdGwfhfKigrs4QKABrNHXiPQ/e3tVGPsBEDjJ22ygRfQ4YVAMqNvd9hFkR4IaOhINRqN7BGuU6qs44GgVgn+KWvJdn2Nk93Tytp0Ij2zb1XkeB0sQNloNIJZxnbvQTmBA6/kvwHwnMBtvnIAtSKnoFY7Sw2dhoaWRcb0CexCBdCKnIK+7Kx9oR0EdADE8Fc2DYNPKFQArciZopDo3nXt6Bg5aXtQAK1Ih3RzwAjrzrmVYKYRfHHKGhre1ul0HTq6WJELQMxmc2Bqo5sB3aY5naaGhp4lLi5OrVZ7PB5lOEJhGrGxsbJX/W6e+g5Wxm1Yx/GQ3f60M6Q+MzMzKytL2XJ3gQvP81ardcCAAaCkA/1+N8uxAyynrKHluftJkyaVl5dXVlbSJ5wIVOQ8RrNKpZo4cWKvXr30ev1ZWXUTALTMOk6K6eNyfJZlS0pK1q1bN2fOnOLiYkyZCrmQRF6oGRoaeoVfANBgU3X/arsTaOVTBjSI1+utra0tLCxcuXIl/K2rq+OUs6cuDNJssViAN48aNSonJycmJsZgMHSzOdhxoJ8CoDH/+W52u91mszU0NMCFQqkvENFqtaCeAdZGo7H7V42eEdY70uwEvyibpi4o6cR5wR4HaEUUOWfaoVIEiiiAVkSRHiqk1+vtoGGJ19Vj+w5g3TwYCYyIIMQ+WWKoBVNMUkVOitOnn366I++xBJ59oHDSirVc9x5FhQsiR5I/XzbmYFICrQwaKnJSQCckJJz8JQxzI+x6AX9NIHzdmz5oPRDjwwS3GhMVJ6GKnJxydGSuBAANLwnSGSVE8M2u5RpB1zwncPBHqS5FTgrojkxoo1b4iscATuw8ZLc5TfBoAg93lJl3RToA6NOjAW5MfAXj9mOCnqK0Wp3kcAM7/fFsAKsgCC6Xi+N5QC1YqcMx9HeMYpX6UaQbAC2fFXQAE7YQWIRGTeCiIPBnMj8DupcgCZ+a9rjdgih6MCwMwxV9rEg3AVoWCkM6glQBuDn+TAc+oDEInFrCNQGcXvC7/ldEkdOQMxlUllyJdqIz88CGXkUUOSuAVkQRBdCKKKIAWhFFFEArciHKued5Q9ru5p/dxEkCzMhzIsE8zx+72Ud2RCRvnsD8riO6YkG9fMrOhWNtn2OAFnhebTSFxifBRVNZMeNyop69q0IGU1JSksViaYNmn89XVlYG90NDQwHxlZWV9fX1p9FU2vgDP6q4BEGr1RoMhsbGxgtk+9y5BGieZS0JyWMffNIYGQ2V2VResnLGK66mBtSD1Q/HcVddddUDDzzAMAyAL3AfUFhSUrJr164rr7wSkC1j7h//+Edzc/MJtKnszVb20wyh6XQ6k8lUU1NzPG+cEDuE37dv35deeilw7s4Runl8p7QKoLteN4sCTpL9rriO9XrnPnkfqVJf+sS/siZctfXHr6GOema9gFIE0AwcOPDPP//84IMPgpEKwPJ4PHfddRdAed++fREREenp6aBNm5qajku0BCEzMxMgePDgQVk3w1cvvPDCvHnzfvnll2PRCWiGF6655hq73Q5tAH5CKwo+PMrr9cqHSimA7lBviLW33fx49zsSIq3RmuPiC1YttVVXCgJfvOXP8NRMgiB5nj0N6zZwhGMgPbz/lCO5B5cBJKMkcB9r9Ux8Su6NIZwWvwRDB8KR5kQFARQn1yrt7u+E+3ICRo8e/d///rewsPCJJ55wu92g9Xfv3v3rr7/ecccdFRUVW7ZskTV3cHMaM2bM0KFDAdCA+2+++Qa0da9eveSOAhKTn58PN8vLyzvOsAOMXKb+JEnKu6chNN5/lHAgKN4vAcMgUJ5yCIGiCHzeJhy52OX7cmqDY5QvsPYcRZNdp5wgQVDEwfHBTageSMpp93TS0fSYiJMUxiOBF6Sfp6ubQTVCRxwWFgaFsnPnzlmzZt1///2Q5tdff93lcoEuBPU5Z86cjRs3xsbG3nnnndnZ2ZBsIAbvvfce6MhTMrOgPiDXbQANolKpIPwnn3xy0KBBgLljw4R3IiMjgWdnZGTcdtttEDsEAmmura0NCQl58MEHAdypqanXXXfd/v37QeMGHxVlNBoB0HBhtVpBVUPWoDFUV1fLzQYqAp4+/PDDzz33HDSqjmQHQgC6D8UCrQIY1I8//rh69WoI+e6774Y7wKA+//xzaB4y1OD+2LFjIWooWOh2AAnQV0ycOBHi+umnn+BDwr/MAayLW265JSUlxel0QuvatGlTv379IIrw8PBt27Z99dVXECNkPDExEXL39ddfb926FQrt1ltvHTJkCHz+/fffb9iwITjxXQJoiOn666+HFIP+gJzL8UFxQBFPmzZtz549UIun39N1wAVlRzSNWq1OSEhYsGBBcXGx7EMHQNynTx9Qdb/99pterwcEL126FEr2mWeegZ9z584FsgvlCznqLOoJrAMiveiiiwAQwDfaaGiIKCsrC+AuO/+EwqyqqgJGPn78eID47Nmzn332WbheuHAhvAMhbN++vY2zrMWLFwMpB7gDJ5k6dSogLKAIAX8ABcgj5Ajs0Q62T0hGQUEBMCiAJuAsLy/vhhtuiIqKmjlz5o033ggRffjhhwBZIPcQHSAY+P0PP/wAd6CjmDJlCuAPEAwfQtOCSCGFaWlp0IF8+umnV199NdyH96F5wJ0lS5b85S9/gbggYYB1aCqXXnopqBXoVeACSuzbb7+FYKFhtzlVluwivgHpeOyxxyBj0MjkQoSygDugFfbu3XvK1E08At8jtd56cXrwgq+ABkDdgHqGawAu1P369euh3EE3MH4BBEPNgUb85z//CUUpd3+QqTN39if3zqCoALKgeKBPAMyBAg4GFkQHihkuIHZIG7Q6qEv4EKr5oYcegla3bt06IOjwFxgIaMTg9gDfQjO4+OKL4cNVq1ZBc508eTJgN/ACtB/IZlxc3KuvvvrFF1+sWbPmpDYiVBkACCg7hAa9FsQISgEa5PLly+fPnw+BQ+VCsDabDWKHzg3SCZHK1id0QdA5QNuDpgjXYDCALgdIQOLhc/gEPp80aVJOTg6UNrQKAAno4GHDhr3zzjsAbpmwTZ8+HfqrcePGQdteu3atbGe3OZCzS8YHIKHQrcyYMQNScM8990CskE/Qc8ACn3/+eQDNKXmeFgWe1upGP/BETL8BSCpWEpiG6GecQGtSLxo3/DawEVWnukwKagU6xFdeeQUqA9SezCyhthwOx+233y63N6hgUD8HDhwoKyuLj48HbXTttdeCjukc84UkoXMApgGVBxro7bffhvbTxpsb5BHUFShmQHOA04NyhZsajQYUJKBWPsvnWAUB4QPs4GVQZpdffnkwmgPUHD4f75eOuGeRFSG8DAbufffdBziDKEARQGJkz8sQoJxCSA+UGMBU/gpSCJwBtC88hXjhzYBGgCzDHShkaAwya4LP4UN4E9oDdOkQJtyEMgd9D9azTHugK4A+ASjKgAEDgseOupBDQ4KgT4QLyDmUNbRm0BZQeaAJTjBuejx1xrOMKSpWiKttKitJH3Vp0aa1YCAmDR11aMOaiJQMtdEI3PxU985AMqAvAz0HhPjQoUOy+QJF+d133/39739v4/cMagXqHrpF0IjwAtQo1hneKyFGqHuoYGhF7Q69ye6LZIMkkB5QcoBmuANtDL4FQMBrbeo1YHfCC9DngGpvt47gKfQ80KI6mB14BzTrH3/8AeUDRQHNDEKAhB27UrKNzQApAYV6rEkN34K2BmMALoCUB2xWCA2agWw0QzcFfTvAGnp70IzQGECp//7774888ghkDXAPrSIQexcCGlIjY/qpp54Cs+Dll19euXJlGzOxQ3DGccbtbMndmz5w2PrP3wu5Nf6G1z7GKaquMK9iz7ZxD//zwIpFrNdLUNSpdvoAoxUrVgBpBooMna/ct0BfBn0IqDSZKUFHCVoZrBag/gB06HZMJtOpRiRXZ7ssK/jksmOfQuygnIASAG5AIckHbg8fPhy6YAAT2EbQe8CHYOBCXo71zwngAJJ65ZVXQtTB/irgEZAcCAq6COjTc3NzO8IA5REbQP/mzZvhk9deew1sDAgHGhgEDmmQz+WGnMqdhkwJ4C8kHjoZMF5lwgbqHBSH/FQ+oxYaFfCuhoaGmJgYgCwECAwbcFxRUQEM5PHHH4cXoBODFwDxECOQJagOuAkaGtp2cNa6cBxaHqOBZgfpgAxAKZz2+AZOUvsXz8vIHpQ9+cZN331ujo4FlexqqAey4WisPbhu5WnMF0LaoKeDDgT4BvSGYD5D05fbG0Bn1KhRffv2hWoGq3HEiBHATIBtw0/oOmV91vHRHqhaUEJQPQEtGzySdYKzdCE9YOdBtwaYhooELQU9CaRw06ZNixYtAqUL94FuAgcAfQkIa6MjgVgDegAfr7/++rEWJzQAsIABCmCfQQo7QgKhWfbu3RtKBrg+pAfqtLS0dMeOHdD4Ac0jR44EGxSSBBQZVDhAExIGIIbihZ/AlaEYQRPDfQA3IBVKHnIH4UDhQI4uueQSKApgHUVFRQ8//DDgFbAODHDChAkQCNivcAGtBe4DmsFehMRDn79s2TJo28EZJ4CmnLxpYhiLYb0xfAJG8H7eDZXwG8bXkqT6hEiS+xcoa2hwHdHN8uAlh2FpGLqqNS74uxiJ+S5HU/7+hEEjsyddrw8Nj0jL7HP5ZFtN1fqZH7ma6vBTBLRMTyHvUOXAJaCqIJFQXtB/QQGBRgEKCL0KoBxKf9euXZB46N9BtUCzBIgDPBTUBgAAA5RJREFUgDrYQQNkgdiATgU1D3V24403Am8BTAAcwQYCFQtRAw2FWgRC3AZVchRQ95AAqFF4Ko/czZo1q76+HuwtgDV8fu+9986dOxfeCf4c6hgADagFuwWQAU039miBBgbvg14EUHbQQIcSg4YBEITeDIrohx9+gMYAbQzKENr5xo0bIT2gUKGggMX169cPTBQoPXgK/QwgVdav8Bf6bWgM2dnZoEcgO/BCSEgIpBD6SagFKHO4hjIBNgjXMj6hjuAmNBuIEXpUUOGQBkA2xCjPgB4pNMhbRwDtxrAbMOINjPL5tboTE+/BmD1qlYmiOtHZo9xheTBsIoZ/hNFMq1+OBzFmFSaoORasw9DEtDEPPelz2jd89VF9UQHQa5wgTzu6wMQK1G5gLk0eMgMsgo6U7RiQwIm8pF9OYcae56HC5H4ZulcIAdQqBA7d5Z133vniiy+C6QxkDLRRG4O9DXmAlIDOg/qDvkKeWQCT/4033oCf77//vvzCsRwaUAWxt2vzQauARntK3aY8kyA3VIgO2rncBckME4pLpvvyGfcyZ5NPgJUtxWDDIGDIBqdcnliR51ZkBih3a8EvyFHIKgkiasvdz6FZTSAewJXLd22tzd/vsbdU7tlJAuCI089C8OibjOlA1cKjgJXWxr45VdYE34JaggBBPYMhBfUBOhXug8pJTk6Wic2Jj0uTYQF/oU+HC7k5yQoYAL179+5j0RzIEVDt2tra43FC6hQNj+BPAkdUBYpRXj8YOP2tzaC4XKSBDwM9dpvXAqUtP22zCDEQxfHq4hxbbQfWE6VRF6xdBkYWqVaf4TjDiU856OApNR3ENNQBkJn58+cHJn4BZzt37iz1i0zJTpra4BYI74N+BfUcOHCt3U86/Wy+Y4ui3XJrt8TaLdITB3jScM5tQPszg5fv3Ar/P9VhjbMogGDgi2CnH7uGU8axPDd+Gmb38VjKBSvn5NGaBHXuncoF4AOLSgFcl5ezUgSKKIBWRBEF0IooogBaEUW6ySjkMOQVBU3nJUUURZ8gsP5pSMUVtCLdB2h5XVs8hjk40UyInbWfj+cFHy9tQfFhWAyGlNPmFOkmQPMYpsLQfzBKmoD0dKoyFSm5wZAYUpxDK9KtlEMn74XqZEWKAp0Ar1SOIt3LobtWFIfninQtoAkM03b7sAjhj5Ho9J5AkQsZ0KLfpf5uTHgWY7t5/EFeD12ECbRSV4p0QP4fww2vmJmgnYkAAAAASUVORK5CYII=" alt="Windows system tray menu with the Configuration Wizard and Edit Config options highlighted" width=240 height=180></section><hr><section><p><strong>Step 2A - Configure the client app</strong></p><p>If a configuration file has been provided by your administrator, its contents can be pasted into the configuration form accessed through <b>Edit Config</b>.</p><p><strong>Step 2B - Use the Configuration Wizard</strong></p><p>If no configuration file has been provided, the <b>Configuration Wizard</b> can be opened from the client menu. The following steps should then be completed:</p><ul><li>The VPN server URL should be set to the address of this page:<code id=u></code></li><li>The connection to the server can be checked by clicking the &quot;Verify &amp; continue&quot; button in the Configuration Wizard. If the connection fails, the HTTP request headers can be viewed using <button id=s>Show HTTP Headers</button> and then copied into the Configuration Wizard.</li><li>The correct Server Secret should be entered, or the default used if applicable.</li><li>The correct IP Address and the corresponding Access Token should be entered, or the defaults used if applicable.</li><li>The default Communication Contracts should be generated.</li><li>The Server Secret and, where required, the IP Address and Access Token should be provided by your administrator.</li></ul></section><noscript>JavaScript must be enabled so that a key can be generated and copied and HTTP headers can be viewed.</noscript></main><p id=f>Developed by <a href=https://www.pilandrome.com>www.pilandrome.com</a> (MIT License)</p><dialog id=d><form method=dialog><button id=x aria-label=Close>&times;</button><h2>HTTP headers</h2><p>The HTTP request headers received by this script may be incomplete. For exact values, your browser&apos;s developer tools can be opened with F12, and the request headers can be copied from the Network tab.</p><textarea id=t aria-label="HTTP headers" readonly></textarea><p id=b><button id=y type=button>Copy</button> <button>OK</button></p></form></dialog><dialog id=j aria-labelledby=v><form method=dialog><button id=z aria-label=Close>&times;</button><h2 id=v>SERVER_SECRET copied successfully</h2><p>The key generated by JavaScript in the web browser has been copied to the clipboard. You should now replace the value of SERVER_SECRET in the server script with this key to improve the security of the VPN server.</p><p id=a><button>OK</button></p></form></dialog><script>document.getElementById('u').textContent=location.href;C=async e=>{e.select();try{if(navigator.clipboard){await navigator.clipboard.writeText(e.value);return true}}catch{}try{e.select();return document.execCommand('copy')}catch{return false}};if(document.getElementById('g')){g.onclick=()=>{k.value=[...crypto.getRandomValues(new Uint8Array(32))].map(x=>x.toString(16).padStart(2,0)).join('');r.hidden=false};c.onclick=async()=>{if(await C(k))j.showModal()}}s.onclick=()=>{d.showModal();t.select()};y.onclick=()=>C(t);if(document.getElementById('o')){L=()=>{let e=o.selectedOptions[0],w=e.dataset.type=='link';l.href=e.value;l.textContent=w?'Open':'Download';w?l.target='_blank':l.removeAttribute('target')};let n=navigator,p=n.userAgentData?.platform||n.userAgent,m=/Android|iPhone|iPad|iPod|CrOS|Chrome OS|Chromium OS/.test(p)||/Mac/i.test(p)&&n.maxTouchPoints>1?'':/Win/.test(p)?'windows':/Mac/i.test(p)?'macos':/Linux|X11/.test(p)?'linux':'';for(let e of o.options)if(m&&e.dataset.os==m){o.selectedIndex=e.index;break}L();o.onchange=L}</script>'''
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
            body = STATUS_HTML.replace(b'__CLIENT_DOWNLOADS__', download_controls(cfg.CLIENT_DOWNLOADS if cfg else CLIENT_DOWNLOADS), 1)
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

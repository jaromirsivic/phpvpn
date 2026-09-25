"""Independent Python client for the phpvpn v1 wire protocol.

Everything in this module is derived from `protocol.md`, which is the single
source of truth for the integration suite. The cryptography is deliberately
implemented with the Python standard library (`hashlib`, `hmac`) and the
`cryptography` package, never with a port of the server code, so that the hand
written pure PHP AES-256-GCM implementation of the server is cross validated
against an independent and well tested implementation.

Chapter references in the docstrings point at `protocol.md`.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import re
import time
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

import requests
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

# ---------------------------------------------------------------------------
# Protocol constants, chapter 11.1 default limits and chapter 7 domain strings
# ---------------------------------------------------------------------------

JSON_CONTENT_TYPE = "application/json; charset=utf-8"
NDJSON_CONTENT_TYPE = "application/x-ndjson; charset=utf-8"
HTML_CONTENT_TYPE = "text/html; charset=utf-8"

DEFAULT_MAX_DATA_RECORDS_PER_REQUEST = 16
DEFAULT_MAX_AUTH_RECORD_BYTES = 4096
DEFAULT_MAX_REQUEST_BODY_BYTES = 983040
DEFAULT_MAX_INFO_PLAINTEXT_BYTES = 262144
DEFAULT_MAX_QUEUE_BYTES_PER_CLIENT = 16777216
DEFAULT_AUTH_TIMESTAMP_SKEW_SECONDS = 60
DEFAULT_QUEUE_TTL_SECONDS = 60

AUTH_SEMI_SALT_DOMAIN = b"phpvpn/auth-semi/salt"
AUTH_SEMI_KEY_DOMAIN = b"phpvpn/auth-semi/key"
AUTH_FULL_SALT_DOMAIN = b"phpvpn/auth-full/salt"
AUTH_FULL_KEY_DOMAIN = b"phpvpn/auth-full/key"
HEALTH_SALT_DOMAIN = b"phpvpn/health-response/salt"
HEALTH_KEY_DOMAIN = b"phpvpn/health-response/key"
INFO_KEY_DOMAIN_PREFIX = b"phpvpn/info-response/"

BASE64URL_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"

TIMESTAMP_PATTERN = re.compile(r"\A\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z\Z")
UUID4_PATTERN = re.compile(
    r"\A[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\Z"
)

#: Header names every response must carry, chapter 4.1.
REQUIRED_HEADERS = {
    "Cache-Control": "no-store, no-cache, must-revalidate",
    "Pragma": "no-cache",
    "X-Content-Type-Options": "nosniff",
}

#: Byte sequences that must never appear in a response, chapter 13.
#: All of them contain a character that cannot occur in canonical base64url or
#: in the fixed JSON scaffolding of a data response, so scanning whole NDJSON
#: bodies for them cannot produce a random false positive.
LEAK_PATTERNS = (
    b"D:\\",
    b"D:/",
    b"C:\\",
    b"C:/",
    b"/nginx/",
    b"\\nginx\\",
    b"phpvpn-buffer",
    b" on line ",
    b"Stack trace",
    b"Fatal error",
    b"Uncaught",
    b"X-Powered-By",
    b"PHP/",
    b"php-cgi",
    b"php.ini",
)


# ---------------------------------------------------------------------------
# base64url, chapter 6.1
# ---------------------------------------------------------------------------


def b64u_encode(raw: bytes) -> str:
    """Return the single canonical base64url text of `raw`, chapter 6.1."""
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def b64u_is_canonical(text: str) -> bool:
    """Report whether `text` is canonical base64url without padding.

    Implements the normative decode and re-encode check of chapter 6.1, which
    also rejects a non zero unused low bit in the final character.
    """
    if any(character not in BASE64URL_ALPHABET for character in text):
        return False
    if len(text) % 4 == 1:
        return False
    try:
        raw = base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))
    except Exception:  # pragma: no cover - defensive, the alphabet is checked
        return False
    return b64u_encode(raw) == text


def b64u_decode(text: str) -> bytes:
    """Decode canonical base64url text, raising `ValueError` otherwise."""
    if not b64u_is_canonical(text):
        raise ValueError(f"non canonical base64url value: {text!r}")
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


# ---------------------------------------------------------------------------
# Millisecond UTC timestamps, chapter 8.1
# ---------------------------------------------------------------------------


def format_timestamp(epoch_ms: int) -> str:
    """Format epoch milliseconds as `YYYY-MM-DDTHH:MM:SS.sssZ`."""
    moment = datetime.fromtimestamp(epoch_ms / 1000.0, tz=timezone.utc)
    return f"{moment.strftime('%Y-%m-%dT%H:%M:%S')}.{epoch_ms % 1000:03d}Z"


def parse_timestamp(text: str) -> int:
    """Parse a normative protocol timestamp into epoch milliseconds."""
    if not TIMESTAMP_PATTERN.match(text):
        raise ValueError(f"invalid protocol timestamp: {text!r}")
    moment = datetime.strptime(text, "%Y-%m-%dT%H:%M:%S.%fZ").replace(tzinfo=timezone.utc)
    return int(moment.timestamp() * 1000)


def local_epoch_ms() -> int:
    """Return the local wall clock in epoch milliseconds."""
    return int(time.time() * 1000)


def new_uuid4() -> str:
    """Return a fresh lowercase UUIDv4, chapter 6.3."""
    return str(uuid.uuid4())


# ---------------------------------------------------------------------------
# Key derivation, chapter 7
# ---------------------------------------------------------------------------


def sha256_hex(raw: bytes) -> str:
    """Return SHA256HEX(raw) as 64 lowercase hexadecimal characters."""
    return hashlib.sha256(raw).hexdigest()


def hmac_sha256(key: bytes, message: bytes) -> bytes:
    """Return the 32 byte HMAC-SHA-256 of `message` under `key`."""
    return hmac.new(key, message, hashlib.sha256).digest()


def hkdf_sha256(ikm: bytes, salt: bytes, info: bytes, length: int) -> bytes:
    """RFC 5869 HKDF-SHA-256, implemented directly on top of `hmac`."""
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    okm = b""
    block = b""
    counter = 1
    while len(okm) < length:
        block = hmac.new(prk, block + info + bytes([counter]), hashlib.sha256).digest()
        okm += block
        counter += 1
    return okm[:length]


def normalize_server_secret(configured: bytes) -> bytes:
    """Apply the chapter 3.2 normalization of an exactly empty server secret."""
    if len(configured) == 0:
        return b"CHANGE_THIS_SERVER_SECRET"
    return configured


def derive_semi_key(server_secret: bytes) -> bytes:
    """Derive the `auth-semi` HMAC key, chapter 7.1."""
    salt = hashlib.sha256(AUTH_SEMI_SALT_DOMAIN).digest()
    return hkdf_sha256(normalize_server_secret(server_secret), salt, AUTH_SEMI_KEY_DOMAIN, 32)


def derive_full_auth_key(server_secret: bytes, access_key: bytes, src: str) -> bytes:
    """Derive the `auth-full` HMAC key for one client, chapter 7.2."""
    secret = normalize_server_secret(server_secret)
    salt_material = AUTH_FULL_SALT_DOMAIN + b"\x00" + src.encode("ascii")
    salt = hmac_sha256(secret, salt_material)
    info = AUTH_FULL_KEY_DOMAIN + b"\x00" + src.encode("ascii")
    return hkdf_sha256(access_key, salt, info, 32)


def derive_health_key(server_secret: bytes, request_id: str) -> bytes:
    """Derive the per request `health` response key, chapter 7.3."""
    secret = normalize_server_secret(server_secret)
    salt_material = HEALTH_SALT_DOMAIN + b"\x00" + request_id.encode("ascii")
    salt = hashlib.sha256(salt_material).digest()
    info = HEALTH_KEY_DOMAIN + b"\x00" + request_id.encode("ascii")
    return hkdf_sha256(secret, salt, info, 32)


def derive_info_key(server_secret: bytes, salt: bytes, request_id: str) -> bytes:
    """Derive the `info` response key, chapter 8.3."""
    secret = normalize_server_secret(server_secret)
    info = INFO_KEY_DOMAIN_PREFIX + request_id.encode("ascii")
    return hkdf_sha256(secret, salt, info, 32)


# ---------------------------------------------------------------------------
# Canonical strings, chapters 7.3, 8.1, 8.3, 9.2, 9.4 and 9.5
# ---------------------------------------------------------------------------


def health_aad(
    request_id: str,
    server_version: str,
    status: str,
    description: str,
    timestamp: str,
    iv_b64: str,
) -> bytes:
    """Build the AAD of the `health` response GMAC, chapter 7.3."""
    return "\n".join(
        [
            "PHPVPN-HEALTH-RESPONSE",
            "health",
            request_id,
            server_version,
            status,
            description,
            timestamp,
            iv_b64,
        ]
    ).encode("utf-8")


def info_aad(request_id: str, salt_b64: str, iv_b64: str) -> bytes:
    """Build the AAD of the encrypted `info` response, chapter 8.3."""
    return "\n".join(["PHPVPN-INFO", "info", request_id, salt_b64, iv_b64]).encode("utf-8")


def auth_semi_canonical(timestamp: str, request_id: str) -> bytes:
    """Build the `auth-semi` canonical string, chapter 8.1."""
    return "\n".join(
        ["PHPVPN-AUTH-SEMI", "POST", "info", timestamp, request_id]
    ).encode("utf-8")


def auth_full_canonical(src: str, timestamp: str, request_id: str, body: bytes) -> bytes:
    """Build the `auth-full` canonical string, chapter 9.2."""
    return "\n".join(
        [
            "PHPVPN-AUTH-FULL",
            "POST",
            "data",
            src,
            timestamp,
            request_id,
            sha256_hex(body),
        ]
    ).encode("utf-8")


def data_aad(message_id: str, src: str, dst: str, iv_b64: str) -> bytes:
    """Build the AAD of one end to end payload record, chapter 9.4."""
    return "\n".join(["PHPVPN-DATA", "data", message_id, src, dst, iv_b64]).encode("utf-8")


def data_mac_canonical(
    request_id: str,
    pairs: Sequence[tuple[str, str]],
    data_result_line: bytes,
) -> bytes:
    """Build the `data-mac` canonical string, chapter 9.5.

    `pairs` are the ordered (messageId, base64url GCM tag) tuples of the direct
    data lines of the response. The canonical string never ends with LF.
    """
    parts = ["PHPVPN-DATA-RESPONSE", "data", request_id, str(len(pairs))]
    for message_id, gcm_tag in pairs:
        parts.append(message_id)
        parts.append(gcm_tag)
    parts.append(sha256_hex(data_result_line))
    return "\n".join(parts).encode("utf-8")


# ---------------------------------------------------------------------------
# Payload encryption, chapter 9.4
# ---------------------------------------------------------------------------


def payload_key_for(src: str, dst: str) -> bytes:
    """Return the deterministic per direction payload key used by this suite.

    The relay never sees this key, so the suite is free to derive it locally as
    long as both simulated peers agree on it for the ordered pair (src, dst).
    """
    material = b"phpvpn-integration-payload-key\x00" + src.encode() + b"\x00" + dst.encode()
    return hashlib.sha256(material).digest()


def encrypt_payload(
    payload_key: bytes,
    message_id: str,
    src: str,
    dst: str,
    plaintext: bytes,
    iv: bytes | None = None,
) -> tuple[bytes, bytes, bytes]:
    """Encrypt one payload, returning `(iv, ciphertext, tag)`, chapter 9.4."""
    if iv is None:
        iv = uuid.uuid4().bytes[:12]
    sealed = AESGCM(payload_key).encrypt(
        iv, plaintext, data_aad(message_id, src, dst, b64u_encode(iv))
    )
    return iv, sealed[:-16], sealed[-16:]


def decrypt_payload(
    payload_key: bytes,
    message_id: str,
    src: str,
    dst: str,
    iv: bytes,
    ciphertext: bytes,
    tag: bytes,
) -> bytes:
    """Authenticate and decrypt one payload record, chapter 9.4."""
    return AESGCM(payload_key).decrypt(
        iv, ciphertext + tag, data_aad(message_id, src, dst, b64u_encode(iv))
    )


# ---------------------------------------------------------------------------
# Wire serialization helpers
# ---------------------------------------------------------------------------


def compact_json(members: Sequence[tuple[str, Any]]) -> bytes:
    """Serialize an ordered member list as compact UTF-8 JSON without spaces."""
    body = ",".join(f"{json.dumps(name)}:{json.dumps(value)}" for name, value in members)
    return ("{" + body + "}").encode("utf-8")


def build_auth_none(request_id: str) -> bytes:
    """Build the `auth-none` request object of chapter 5.2."""
    return compact_json([("type", "auth-none"), ("requestId", request_id)])


def build_data_line(
    message_id: str,
    src: str,
    dst: str,
    iv_b64: str,
    tag_b64: str,
    ciphertext_b64: str,
) -> bytes:
    """Build one canonical `data` record line including the terminating LF.

    The member order is normative, chapter 9.3.
    """
    return (
        f'{{"type":"data","messageId":"{message_id}","src":"{src}","dst":"{dst}",'
        f'"iv":"{iv_b64}","tag":"{tag_b64}","ciphertext":"{ciphertext_b64}"}}\n'
    ).encode("utf-8")


@dataclass(frozen=True)
class OutgoingRecord:
    """One outgoing `data` record together with its plaintext."""

    message_id: str
    src: str
    dst: str
    iv: bytes
    ciphertext: bytes
    tag: bytes
    plaintext: bytes

    @property
    def line(self) -> bytes:
        """Return the exact serialized NDJSON line including the LF."""
        return build_data_line(
            self.message_id,
            self.src,
            self.dst,
            b64u_encode(self.iv),
            b64u_encode(self.tag),
            b64u_encode(self.ciphertext),
        )


def make_record(
    src: str,
    dst: str,
    plaintext: bytes,
    payload_key: bytes | None = None,
    message_id: str | None = None,
) -> OutgoingRecord:
    """Encrypt `plaintext` into a ready to send `data` record, chapter 9.4."""
    message_id = message_id or new_uuid4()
    key = payload_key if payload_key is not None else payload_key_for(src, dst)
    iv, ciphertext, tag = encrypt_payload(key, message_id, src, dst, plaintext)
    return OutgoingRecord(message_id, src, dst, iv, ciphertext, tag, plaintext)


def parse_json_object(raw: bytes) -> tuple[dict[str, Any], list[str]]:
    """Parse one JSON object, returning its members and their exact order.

    Duplicate member names are rejected, chapter 6.1.
    """
    seen: list[str] = []

    def hook(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        names = [name for name, _ in pairs]
        if len(set(names)) != len(names):
            raise ValueError("duplicate JSON member name")
        if not seen:
            seen.extend(names)
        return dict(pairs)

    document = json.loads(raw.decode("utf-8"), object_pairs_hook=hook)
    if not isinstance(document, dict):
        raise ValueError("expected a JSON object")
    return document, seen


# ---------------------------------------------------------------------------
# HTTP layer
# ---------------------------------------------------------------------------


@dataclass
class Response:
    """One HTTP response together with the metadata the assertions need."""

    method: str
    url: str
    status_code: int
    headers: Mapping[str, str]
    content: bytes
    elapsed_ms: float

    def __post_init__(self) -> None:
        # HTTP header names are case-insensitive, including ASGI's lowercase wire names.
        self.headers = requests.structures.CaseInsensitiveDict(self.headers)

    @property
    def text(self) -> str:
        return self.content.decode("utf-8", "replace")

    @property
    def content_type(self) -> str:
        return self.headers.get("Content-Type", "")

    def json(self) -> dict[str, Any]:
        """Parse the body as a single JSON object with member order checks."""
        document, _ = parse_json_object(self.content)
        return document

    def member_order(self) -> list[str]:
        _, order = parse_json_object(self.content)
        return order

    @property
    def from_web_server(self) -> bool:
        """Report whether the front end web server produced this response.

        nginx enforces its own body limit before the request reaches PHP, so
        such a response cannot carry the application headers of chapter 4.1.
        """
        if self.status_code < 400:
            return False
        if any(name in self.headers for name in REQUIRED_HEADERS):
            return False
        return b"nginx" in self.content.lower()


def assert_protocol_headers(response: Response, content_type: str | None = None) -> None:
    """Assert the mandatory response headers of chapter 4.1 and 13.

    Applied automatically to every response the client returns, so no test can
    forget it. Responses generated by the front end web server before PHP runs
    are exempt from the application header rules but still checked for leaks.
    """
    if not response.from_web_server:
        for name, expected in REQUIRED_HEADERS.items():
            actual = response.headers.get(name)
            assert actual is not None, (
                f"{response.method} {response.url} -> {response.status_code} "
                f"is missing the mandatory header {name}"
            )
            assert actual == expected, (
                f"{response.method} {response.url} -> {response.status_code} "
                f"has {name}: {actual!r}, expected {expected!r}"
            )
        if response.status_code == 405:
            assert "Allow" in response.headers, "a 405 must carry Allow, chapter 5"
        if response.status_code == 503:
            assert "Retry-After" in response.headers, (
                "a 503 must carry Retry-After, chapter 11.2"
            )

    assert "X-Powered-By" not in response.headers, "the server must not expose X-Powered-By"
    server_header = response.headers.get("Server", "")
    assert "php" not in server_header.lower(), (
        f"the Server header must not disclose PHP: {server_header!r}"
    )

    for pattern in LEAK_PATTERNS:
        assert pattern not in response.content, (
            f"{response.method} {response.url} -> {response.status_code} leaked "
            f"{pattern!r} in the response body"
        )

    if content_type is not None:
        assert response.content_type == content_type, (
            f"expected Content-Type {content_type!r}, got {response.content_type!r}"
        )


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Configuration:
    """The deployment description loaded from `server.json`."""

    base_url: str
    server_secret: bytes
    access_keys: dict[str, bytes]
    buffer_directory: Path
    large_body_url: str | None = None
    implementation: str = "php"
    storage_driver: str = "disk"

    @classmethod
    def load(cls, path: Path, large_body_url: str | None = None) -> "Configuration":
        document = json.loads(path.read_text(encoding="utf-8-sig"))
        return cls(
            base_url=document["baseUrl"],
            server_secret=document["serverSecret"].encode("utf-8"),
            access_keys={
                address: bytes.fromhex(key)
                for address, key in document["accessKeys"].items()
            },
            buffer_directory=Path(document["bufferDirectory"]),
            large_body_url=large_body_url,
            implementation=document.get("implementation", "php"),
            storage_driver=document.get("storageDriver", "disk"),
        )

    @property
    def addresses(self) -> list[str]:
        return sorted(self.access_keys, key=lambda item: int(item.rsplit(".", 1)[1]))


# ---------------------------------------------------------------------------
# Parsed `data` response, chapter 9.5
# ---------------------------------------------------------------------------


@dataclass
class DataLine:
    """One direct `data` line of a response."""

    fields: dict[str, Any]
    order: list[str]
    raw: bytes

    @property
    def message_id(self) -> str:
        return self.fields["messageId"]

    @property
    def gcm_tag(self) -> str:
        return self.fields["tag"]

    def decrypt(self, payload_key: bytes | None = None) -> bytes:
        """Independently authenticate and decrypt this record, chapter 9.4."""
        key = payload_key
        if key is None:
            key = payload_key_for(self.fields["src"], self.fields["dst"])
        return decrypt_payload(
            key,
            self.fields["messageId"],
            self.fields["src"],
            self.fields["dst"],
            b64u_decode(self.fields["iv"]),
            b64u_decode(self.fields["ciphertext"]),
            b64u_decode(self.fields["tag"]),
        )


@dataclass
class DataResponse:
    """A parsed successful `data` response, chapter 9.5."""

    records: list[DataLine]
    result: dict[str, Any]
    result_order: list[str]
    result_line: bytes
    mac: dict[str, Any]
    mac_order: list[str]
    raw: bytes

    @property
    def accepted_message_ids(self) -> list[str]:
        return self.result["acceptedMessageIds"]

    @property
    def deferred_message_ids(self) -> list[str]:
        return self.result["deferredMessageIds"]

    @property
    def pairs(self) -> list[tuple[str, str]]:
        return [(line.message_id, line.gcm_tag) for line in self.records]

    def expected_mac(self, full_auth_key: bytes, request_id: str) -> str:
        """Independently recompute the base64url `data-mac` tag, chapter 9.5."""
        canonical = data_mac_canonical(request_id, self.pairs, self.result_line)
        return b64u_encode(hmac_sha256(full_auth_key, canonical))

    def verify_mac(self, full_auth_key: bytes, request_id: str) -> bool:
        return hmac.compare_digest(
            self.mac.get("tag", ""), self.expected_mac(full_auth_key, request_id)
        )


def parse_data_response(raw: bytes) -> DataResponse:
    """Parse and structurally validate a `data` response body, chapter 9.5."""
    assert raw.endswith(b"\n"), "every NDJSON record must end with LF, chapter 6.2"
    lines = raw.split(b"\n")
    assert lines[-1] == b"", "the body must not have trailing bytes after the final LF"
    lines = lines[:-1]
    assert all(line != b"" for line in lines), "an empty NDJSON line is invalid"
    assert len(lines) >= 2, "a response carries at least data-result and data-mac"

    records: list[DataLine] = []
    for line in lines[:-2]:
        fields, order = parse_json_object(line)
        assert fields.get("type") == "data", f"unexpected record type in {line!r}"
        records.append(DataLine(fields, order, line))

    result, result_order = parse_json_object(lines[-2])
    assert result.get("type") == "data-result", "the penultimate line must be data-result"
    mac, mac_order = parse_json_object(lines[-1])
    assert mac.get("type") == "data-mac", "the last line must be data-mac"

    return DataResponse(
        records=records,
        result=result,
        result_order=result_order,
        result_line=lines[-2],
        mac=mac,
        mac_order=mac_order,
        raw=raw,
    )


# ---------------------------------------------------------------------------
# Client
# ---------------------------------------------------------------------------


class ServerClock:
    """Server clock estimate obtained from verified `health` responses.

    The offset is measured NTP style and the sample with the smallest round
    trip is kept, so authenticated requests can carry a timestamp that lands
    inside the chapter 11.1 skew window regardless of local clock drift.
    """

    def __init__(self, offset_ms: int = 0, rtt_ms: float = 0.0) -> None:
        self.offset_ms = offset_ms
        self.rtt_ms = rtt_ms
        #: Residual delay between `arrival_ms()` and the moment the server
        #: actually evaluates the timestamp, measured by the skew calibration.
        self.bias_ms = 0

    def now_ms(self) -> int:
        """Estimated server time now, in epoch milliseconds."""
        return local_epoch_ms() + self.offset_ms

    def arrival_ms(self) -> int:
        """Estimated server time when a request sent now is processed."""
        return self.now_ms() + int(self.rtt_ms / 2) + self.bias_ms

    def timestamp(self, offset_ms: int = 0) -> str:
        """A protocol timestamp for a request processed `offset_ms` from now."""
        return format_timestamp(self.arrival_ms() + offset_ms)


class PhpVpnClient:
    """Thin protocol client that always validates the mandatory headers."""

    def __init__(
        self,
        configuration: Configuration,
        session: requests.Session | None = None,
        base_url: str | None = None,
    ) -> None:
        self.configuration = configuration
        self.base_url = base_url or configuration.base_url
        self.session = session or requests.Session()
        self.clock = ServerClock()

    # -- low level ---------------------------------------------------------

    def request(
        self,
        method: str,
        *,
        action: str | None = None,
        raw_query: str | None = None,
        body: bytes | None = None,
        content_type: str | None = None,
        headers: Mapping[str, str] | None = None,
        timeout: float = 60.0,
        check_headers: bool = True,
    ) -> Response:
        """Send one HTTP request and wrap the response.

        The mandatory headers of chapter 4.1 are asserted on every response.
        """
        url = self.base_url
        if raw_query is not None:
            url = f"{url}?{raw_query}"
        elif action is not None:
            url = f"{url}?action={action}"

        request_headers = dict(headers or {})
        if content_type is not None:
            request_headers["Content-Type"] = content_type

        started = time.perf_counter()
        try:
            raw = self.session.request(
                method, url, data=body, headers=request_headers, timeout=timeout
            )
        except requests.ConnectionError:
            # A pooled keep alive socket can be closed by the web server while
            # a test waits for the queue time to live to pass. The request was
            # never written, so a single retry on a fresh connection is safe.
            self.session.close()
            started = time.perf_counter()
            raw = self.session.request(
                method, url, data=body, headers=request_headers, timeout=timeout
            )
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        response = Response(
            method=method,
            url=url,
            status_code=raw.status_code,
            headers=dict(raw.headers),
            content=raw.content,
            elapsed_ms=elapsed_ms,
        )
        if check_headers:
            assert_protocol_headers(response)
        return response

    # -- health ------------------------------------------------------------

    def health(
        self,
        request_id: str | None = None,
        body: bytes | None = None,
        content_type: str = JSON_CONTENT_TYPE,
    ) -> tuple[Response, str]:
        """POST an `auth-none` request, chapter 5.2."""
        request_id = request_id or new_uuid4()
        payload = build_auth_none(request_id) if body is None else body
        response = self.request(
            "POST", action="health", body=payload, content_type=content_type
        )
        return response, request_id

    def verify_health(self, payload: Mapping[str, Any], request_id: str) -> bool:
        """Independently verify the `health` GMAC tag, chapter 7.3."""
        try:
            iv = b64u_decode(payload["iv"])
            tag = b64u_decode(payload["tag"])
        except (KeyError, ValueError):
            return False
        if len(iv) != 12 or len(tag) != 16:
            return False
        if payload.get("requestId") != request_id:
            return False
        key = derive_health_key(self.configuration.server_secret, request_id)
        aad = health_aad(
            payload["requestId"],
            payload["serverVersion"],
            payload["status"],
            payload["description"],
            payload["timestamp"],
            payload["iv"],
        )
        try:
            AESGCM(key).decrypt(iv, tag, aad)
        except InvalidTag:
            return False
        return True

    def synchronize_clock(self, samples: int = 5) -> ServerClock:
        """Measure the server clock offset from verified `health` responses."""
        best: tuple[float, int] | None = None
        for _ in range(samples):
            before = local_epoch_ms()
            response, request_id = self.health()
            after = local_epoch_ms()
            assert response.status_code == 200, "health must succeed during setup"
            payload = response.json()
            assert self.verify_health(payload, request_id), "health tag must verify"
            server_ms = parse_timestamp(payload["timestamp"])
            rtt = after - before
            offset = server_ms - (before + after) // 2
            if best is None or rtt < best[0]:
                best = (rtt, offset)
        assert best is not None
        self.clock = ServerClock(offset_ms=best[1], rtt_ms=best[0])
        return self.clock

    # -- info --------------------------------------------------------------

    def build_auth_semi(
        self,
        timestamp: str | None = None,
        request_id: str | None = None,
        tag: str | None = None,
        server_secret: bytes | None = None,
    ) -> tuple[bytes, str, str]:
        """Build an `auth-semi` request body, chapter 8.1."""
        timestamp = self.clock.timestamp() if timestamp is None else timestamp
        request_id = new_uuid4() if request_id is None else request_id
        if tag is None:
            secret = (
                server_secret
                if server_secret is not None
                else self.configuration.server_secret
            )
            key = derive_semi_key(secret)
            tag = b64u_encode(hmac_sha256(key, auth_semi_canonical(timestamp, request_id)))
        body = compact_json(
            [
                ("type", "auth-semi"),
                ("timestamp", timestamp),
                ("requestId", request_id),
                ("tag", tag),
            ]
        )
        return body, timestamp, request_id

    def info(
        self,
        timestamp: str | None = None,
        request_id: str | None = None,
        tag: str | None = None,
        body: bytes | None = None,
        content_type: str = JSON_CONTENT_TYPE,
        server_secret: bytes | None = None,
    ) -> tuple[Response, str]:
        """POST an `auth-semi` request to the `info` action, chapter 8."""
        if body is None:
            body, _, request_id = self.build_auth_semi(
                timestamp, request_id, tag, server_secret
            )
        response = self.request(
            "POST", action="info", body=body, content_type=content_type
        )
        return response, request_id or ""

    def build_info_full(self, src: str, extensions: dict, *, access_key: bytes | None = None,
                        timestamp: str | None = None, request_id: str | None = None) -> tuple[bytes, str]:
        """Independent extended-info canonicalization. Presence and array order are signed."""
        extension = {}
        if "maskedCommunicationContracts" in extensions:
            extension["maskedCommunicationContracts"] = [
                {"peer": item["peer"], "hash": item["hash"]}
                for item in extensions["maskedCommunicationContracts"]]
        if "includeMaskedCommunicationContracts" in extensions:
            extension["includeMaskedCommunicationContracts"] = extensions["includeMaskedCommunicationContracts"]
        encoded = json.dumps(extension, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        timestamp = self.clock.timestamp() if timestamp is None else timestamp
        request_id = new_uuid4() if request_id is None else request_id
        canonical = f"PHPVPN-AUTH-FULL\nPOST\ninfo\n{src}\n{timestamp}\n{request_id}\n{hashlib.sha256(encoded).hexdigest()}".encode()
        tag = b64u_encode(hmac_sha256(self.full_auth_key(src, access_key), canonical))
        body = dict(type="auth-full", src=src, timestamp=timestamp, requestId=request_id, tag=tag)
        body.update(extension)
        return json.dumps(body, separators=(",", ":")).encode(), request_id

    def extended_info(self, src: str, extensions: dict) -> tuple[Response, str]:
        body, request_id = self.build_info_full(src, extensions)
        return self.info(body=body, request_id=request_id)

    def decrypt_info_bytes(self, payload: Mapping[str, Any], request_id: str) -> bytes:
        """Authenticate and decrypt an `info` response, chapter 8.3."""
        salt = b64u_decode(payload["salt"])
        iv = b64u_decode(payload["iv"])
        ciphertext = b64u_decode(payload["ciphertext"])
        tag = b64u_decode(payload["tag"])
        assert len(salt) == 32, "the info salt must be 32 bytes, chapter 8.3"
        assert len(iv) == 12, "the info IV must be 12 bytes, chapter 8.3"
        assert len(tag) == 16, "the info GCM tag must be 16 bytes, chapter 8.3"
        key = derive_info_key(self.configuration.server_secret, salt, request_id)
        aad = info_aad(request_id, payload["salt"], payload["iv"])
        return AESGCM(key).decrypt(iv, ciphertext + tag, aad)

    def decrypt_info(self, payload: Mapping[str, Any], request_id: str) -> dict[str, Any]:
        """Parse the exact authenticated bytes without changing the wire-size evidence."""
        return json.loads(self.decrypt_info_bytes(payload, request_id).decode("utf-8"))

    def info_plaintext(self) -> tuple[dict[str, Any], Response]:
        """Fetch, verify and decrypt one `info` response."""
        response, request_id = self.info()
        assert response.status_code == 200, f"info failed with {response.status_code}"
        return self.decrypt_info(response.json(), request_id), response

    # -- data --------------------------------------------------------------

    def full_auth_key(self, src: str, access_key: bytes | None = None) -> bytes:
        key = access_key if access_key is not None else self.configuration.access_keys[src]
        return derive_full_auth_key(self.configuration.server_secret, key, src)

    def build_auth_full_line(
        self,
        src: str,
        signed_body: bytes,
        timestamp: str | None = None,
        request_id: str | None = None,
        access_key: bytes | None = None,
        server_secret: bytes | None = None,
        tag: str | None = None,
    ) -> tuple[bytes, str, str]:
        """Build the first NDJSON line of a `data` request, chapter 9.2."""
        timestamp = self.clock.timestamp() if timestamp is None else timestamp
        request_id = new_uuid4() if request_id is None else request_id
        if tag is None:
            secret = (
                server_secret
                if server_secret is not None
                else self.configuration.server_secret
            )
            key_material = (
                access_key
                if access_key is not None
                else self.configuration.access_keys.get(src, bytes(32))
            )
            key = derive_full_auth_key(secret, key_material, src)
            canonical = auth_full_canonical(src, timestamp, request_id, signed_body)
            tag = b64u_encode(hmac_sha256(key, canonical))
        line = compact_json(
            [
                ("type", "auth-full"),
                ("src", src),
                ("timestamp", timestamp),
                ("requestId", request_id),
                ("tag", tag),
            ]
        ) + b"\n"
        return line, timestamp, request_id

    def build_data_body(
        self,
        src: str,
        record_lines: Iterable[bytes] = (),
        timestamp: str | None = None,
        request_id: str | None = None,
        access_key: bytes | None = None,
        server_secret: bytes | None = None,
        tag: str | None = None,
        signed_body: bytes | None = None,
        transmitted_body: bytes | None = None,
    ) -> tuple[bytes, str]:
        """Assemble a complete `data` request body, chapter 9.1.

        `signed_body` and `transmitted_body` allow a test to sign one payload
        and transmit a different one, which must fail authentication.
        """
        payload = b"".join(record_lines)
        line, _, request_id = self.build_auth_full_line(
            src,
            payload if signed_body is None else signed_body,
            timestamp=timestamp,
            request_id=request_id,
            access_key=access_key,
            server_secret=server_secret,
            tag=tag,
        )
        body = line + (payload if transmitted_body is None else transmitted_body)
        return body, request_id

    def send_data(
        self,
        src: str,
        records: Sequence[OutgoingRecord] = (),
        *,
        record_lines: Sequence[bytes] | None = None,
        timestamp: str | None = None,
        request_id: str | None = None,
        access_key: bytes | None = None,
        server_secret: bytes | None = None,
        tag: str | None = None,
        signed_body: bytes | None = None,
        transmitted_body: bytes | None = None,
        content_type: str = NDJSON_CONTENT_TYPE,
        body: bytes | None = None,
        timeout: float = 60.0,
    ) -> tuple[Response, str]:
        """Send a `data` request and return the response and its requestId."""
        if body is None:
            lines = record_lines if record_lines is not None else [r.line for r in records]
            body, request_id = self.build_data_body(
                src,
                lines,
                timestamp=timestamp,
                request_id=request_id,
                access_key=access_key,
                server_secret=server_secret,
                tag=tag,
                signed_body=signed_body,
                transmitted_body=transmitted_body,
            )
        response = self.request(
            "POST", action="data", body=body, content_type=content_type, timeout=timeout
        )
        return response, request_id or ""

    def poll(self, src: str, records: Sequence[OutgoingRecord] = ()) -> DataResponse:
        """Send an authenticated `data` request and verify its `data-mac`."""
        response, request_id = self.send_data(src, records)
        assert response.status_code == 200, (
            f"data poll for {src} failed with {response.status_code}: {response.text[:200]}"
        )
        assert_protocol_headers(response, NDJSON_CONTENT_TYPE)
        parsed = parse_data_response(response.content)
        assert parsed.verify_mac(self.full_auth_key(src), request_id), (
            "the data-mac must verify against the independently computed HMAC"
        )
        return parsed

    def drain(self, src: str) -> DataResponse:
        """Empty the incoming queue of `src` through a normal empty poll."""
        return self.poll(src)

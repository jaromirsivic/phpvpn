# phpvpn v1 wire protocol

This document is the normative specification of the phpvpn v1 protocol. The keywords **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT** and **MAY** are interpreted as defined in RFC 2119.

The protocol is publicly known. The phpvpn software implementation is open source and published under the MIT license.

## 1. Purpose and scope

phpvpn is a short-connection store-and-forward relay for transferring end-to-end encrypted IPv4 packets between clients. The server:

- authenticates requests and sender permissions;
- checks syntax, routing metadata, limits and queue capacity;
- stores opaque encrypted records in destination clients' queues;
- atomically reads and removes all pending records of the destination client when constructing a response;
- records the time of clients' last successful communication.

The server does not know payload keys and neither decrypts nor interprets IPv4 packet contents. Payload key distribution, virtual network adapter configuration, VPN address assignment and processing of decrypted IP traffic are responsibilities of the client layer outside this protocol.

The protocol is designed for PHP 8.2+, multiple concurrent PHP workers and resource-constrained storage. It requires no persistent connection, long polling, WebSocket, cron or server session.

### 1.1 Security objectives

The protocol provides:

- authenticity of the time and operational fields in the `health` response for a client that knows `SERVER_SECRET`;
- authentication of `info` requests through knowledge of the server secret;
- authentication of `data` requests through simultaneous knowledge of the single server secret and the particular client's access key;
- integrity of the authenticated request, including its exact body bytes;
- end-to-end payload confidentiality and integrity using AES-256-GCM;
- confidentiality and integrity of `info` response contents using AES-256-GCM;
- authenticity of the `data` action result and integrity of the order, count and completeness of its data lines using HMAC-SHA-256;
- separation of keys by purpose using HKDF-SHA-256.

Both PHP server and PHP client implementations must always perform every AES-256-GCM operation using the bundled pure-PHP software implementation. They MUST NOT use OpenSSL, Sodium or any other cryptographic extension for AES-256-GCM, even when available. Availability of optional extensions therefore does not change the implementation's functionality or behavior.

### 1.2 Deliberately excluded properties

The authentication timestamp rejects requests older than the allowed 60-second window. However, the protocol does not prevent replay of a valid message inside that window and does not provide exactly-once or guaranteed delivery, protection against message dropping or delay, or concealment of traffic metadata. A successful `data` response authenticates its `data-result` and the ordered list of `messageId`/GCM tag pairs with HMAC, allowing the client to verify the count, order and completeness of data lines without hashing their ciphertext. Each data line's contents remain protected by its own AES-GCM tag. This protection does not prevent dropping or replaying an entire valid response. The server deliberately uses an optimistic at-most-once model: it removes data from the buffer before sending the response and does not offer it again if the response is lost or not processed.

Over plain HTTP, an attacker can intercept a valid `data` polling request and replay it unchanged within its 60-second window. The server authenticates this replay again, atomically drains the victim's incoming queue and returns its records to the attacker. Without `PAYLOAD_KEY`, the attacker cannot read their plaintext, but permanently removes the records from the relay before the legitimate client obtains them. By repeatedly intercepting new polling requests, the attacker can continuously steal ciphertexts and destroy the victim's incoming queue. This consequence is known, explicitly accounted for in protocol v1 and consciously accepted.

Acceptance of this risk assumes that the client, transport or application layer will typically request the missing communication again or trigger a new transmission, producing new IP packets and retrying delivery. This is not a protocol guarantee: UDP or one-off traffic without its own recovery, for example, can be lost permanently, and sustained replay can prevent even repeated attempts from being delivered. V1 introduces no nonce, replay cache, acknowledged delivery or recovery of a drained queue to address this risk. Nor does the protocol guarantee availability under this attack, storage exhaustion or PHP worker exhaustion.

## 2. Normative decisions

| Area | V1 decision |
|---|---|
| Architecture | Stateless, short HTTP requests; no server session. |
| Wire format | JSON for control actions, NDJSON for the data action. |
| Delivery | Optimistic at-most-once at the server buffer level, without acknowledgement. After atomic retrieval, data is removed from the server before the response is transmitted. |
| Server time | The client sends unauthenticated `auth-none` with a fresh `requestId`; the server returns public `health` authenticated by a GCM tag derived from `SERVER_SECRET`. |
| Replay | A timestamp limits an authenticated request's validity to ±60 s relative to server time. Replay inside the window is not detected; no deduplication or timestamp/nonce persistence. Replay of intercepted HTTP polling can drain and permanently destroy the victim's incoming queue; v1 consciously accepts this risk. |
| Payload keys | Client-side per-peer or group keys distributed outside the relay; unknown to the server. |
| Multiple destinations | Partial acceptance is allowed and a result is returned for each record. |
| Transport | HTTP and HTTPS are equally supported; the server works directly and behind a proxy. HTTPS is not a protocol requirement. |
| Storage | An abstract backend; v1 must have a `disk` driver, further drivers may be added without changing the wire format. |
| Compatibility | V1 is a new protocol; old actions and wire formats are neither aliases nor fallbacks. |

## 3. Roles, identities and secrets

### 3.1 Roles

- The **relay server** receives HTTP requests and manages queues.
- A **client** has exactly one configured VPN IPv4 address and can be both sender and recipient.
- The **administrator** configures the single server secret, client tokens, limits and storage driver.

The default address space is `10.99.99.0/24`. Valid client addresses range from `10.99.99.2` to `10.99.99.254`, written without leading zeros. A different address space requires shared configuration of all participants, not a wire format change.

Different client computers MAY use the same virtual IP. phpvpn neither enforces nor reserves address uniqueness across computers. The system permits collisions, correcting them is the operator's responsibility, and they are a configuration error. An `ACCESS_KEYS` entry authorizes an address, not the identity of a particular computer.

### 3.2 Keys

The server and clients use three separate classes of keys:

1. `SERVER_SECRET`: exactly one constant in the PHP file's configuration header, known to the server and authorized clients. The default is the string `"CHANGE_THIS_SERVER_SECRET"`, which the administrator should replace with a secure random value. It is used for `auth-semi`, `info` encryption and derivation of the `auth-full` authentication key.
2. `ACCESS_KEY`: exactly one 32-byte key for a given VPN address. For a private setup it should be random and known only to the server and authorized clients of that address. It is the input secret for deriving the `auth-full` authentication key. The server looks it up in `ACCESS_KEYS` using VPN address `src`; neither the key nor an identifier for it is ever transmitted.
3. `PAYLOAD_KEY`: exactly one fixed 32-byte end-to-end key for each ordered pair of VPN addresses (`src`, `dst`). Only clients of that communication direction know it; the relay server MUST NOT possess it. Distribution is outside the protocol.

Every server and client implementation normalizes `SERVER_SECRET` at startup, before configuration validation and before the first cryptographic operation. If the configured value is exactly an empty string of zero bytes, the implementation replaces it at runtime with `"CHANGE_THIS_SERVER_SECRET"`. Throughout the remainder of this document, `SERVER_SECRET` denotes this effective normalized value. Any nonempty value is left unchanged; in particular, the string `"0"`, 32 or 64 ASCII `0` characters and a value consisting of zero bytes `0x00` are not normalized.

Cryptographic operations use the exact bytes of the effective `SERVER_SECRET`, without implicit hex or base64 decoding. An empty configured string, a value consisting solely of zero bytes `0x00` and a string consisting solely of ASCII `0` characters are valid inputs and authenticated endpoints must not reject them solely because of their contents; however, such configuration provides no practical secrecy and the public page warns about it. Each `ACCESS_KEY` is written as exactly 64 lowercase hexadecimal characters and decoded into 32 bytes for cryptography. Clients maintain a local `PAYLOAD_KEYS[src][dst]` map; each value is decoded into exactly 32 bytes for AES-256-GCM.

`SERVER_SECRET`, `ACCESS_KEY` and `PAYLOAD_KEY` have no identifier transmitted by the protocol. The client selects the single `PAYLOAD_KEY` exclusively from the exact ordered pair `src`, `dst`.

The default `ACCESS_KEYS` map contains addresses `10.99.99.2` through `10.99.99.15`. Each key is exactly the two-character lowercase hexadecimal representation of the last octet repeated 32 times, for example 32×`02` for `.2`, 32×`0a` for `.10` and 32×`0f` for `.15`. These publicly derivable defaults are not random secrets. A different address space requires an explicit server configuration change. There is no automatic migration, overwriting of nonempty secrets or probing of historical values.

## 4. HTTP, HTTPS and proxy transport

### 4.1 Common rules

The server MUST have identical application behavior over HTTP and HTTPS. HTTP/1.1 is the minimum; a web server or proxy may terminate HTTP/2 or HTTP/3 if it preserves the method, query parameter, body and response meaning.

The server:

- MUST NOT derive authentication from the TCP peer's IP address;
- MUST NOT include the scheme, host, port or proxy headers in cryptographic calculations;
- MUST NOT trust `X-Forwarded-For`, `X-Forwarded-Proto`, `Forwarded` or similar headers for authorization or routing;
- MUST accept direct connections and connections through a reverse proxy with the same result;
- MUST send `Cache-Control: no-store, no-cache, must-revalidate`, `Pragma: no-cache` and `X-Content-Type-Options: nosniff`;
- MUST NOT log secrets, authentication HMAC tags, ciphertext, GCM tags or entire requests.

The URL contains only `?action=<action>`. Client identity, tokens, authentication HMAC tags and encrypted contents belong exclusively in the body. The proxy must forward the body byte for byte; transforming line endings, decompressing to different contents or rewriting JSON invalidates the HMAC.

### 4.2 Protection boundaries over HTTP

Local diagnostics for the first two steps of the client Configuration Wizard and the initial health/info exchanges of Connect do not change this server wire protocol. They capture a specific application request and the received response before protocol parsing so that hosting-injected contents can also be shown as plain inert text. This reconstructs information available in WinHTTP; it is not a packet or TLS capture. Sent bodies, endpoints, authentication and cryptographic verification remain unchanged. Only local IPC between the service and tray is extended. The exact byte slice, reversible `http-octets-v1`, Unicode projection, lifetime and limit are defined in [HTTP details of a failed Wizard probe in client.md](client.md#http-detail-neúspěšného-wizard-probe). Correlation of Connect by epoch and attempt UUID, state snapshots and precisely correlated detail cancellation are defined in [HTTP details of a failed Connect](client.md#http-detail-neúspěšného-connect). Diagnostics do not affect data requests or recovery exchanges, and raw contents are not logged.

Plain HTTP is a valid, supported mode. Application cryptography protects secret values and authenticity to the extent described here, but does not replace secure transport.

A passive observer of an HTTP connection sees the URL, method, sizes, timing, VPN addresses `src` and `dst`, message identifiers and traffic volume. They see neither payload plaintext nor `info` response plaintext.

An active attacker on the HTTP path can copy, replay, delay, drop, reorder or modify requests and responses, or terminate the connection. Without the corresponding key, they cannot create a new valid request HMAC, `data` response HMAC or individual payload GCM tag. The final `data-mac` therefore detects removal, addition or reordering of data lines and any change to `data-result`; changing an individual data record's contents while retaining its GCM tag is detected by subsequent AES-GCM verification. The attacker can still drop the whole response, interrupt the connection or replay an entire previously valid response with the same currently expected `requestId`. Since the server removes records before sending them, failure to deliver a response permanently loses them at relay level. An authenticated request can be successfully replayed only within its 60-second window; a response has no separate age check.

The `auth-none` request itself is unauthenticated and an attacker can read, modify or replay it. However, the `health` response binds the server's UTC time and all other operational fields to the fresh client `requestId` with a GCM tag derived from `SERVER_SECRET`. An attacker without that secret cannot substitute a future timestamp or modify a legitimate response in a way that the client accepts. The client rejects an old response for a different `requestId`. An attacker can delay or drop a request or response, causing stale time or unavailability, but cannot undetectably move client signatures into the future.

To attack the polling queue, the attacker need not modify traffic in transit or know a key. Passively capturing a valid HTTP `data` request, submitting the copy to the server within the open window and repeating this with new victim requests is sufficient. Each successful copy performs the ordinary at-most-once queue drain for the attacker's connection. This can continuously take all newly pending records and leave the legitimate client an empty queue. This is the explicitly accepted plain-HTTP risk of section 1.2, not a property that v1 detects or remedies.

HTTPS additionally protects transport metadata in bodies and headers, authenticates the TLS peer and protects the connection from modification. Whether to use it is an operational decision outside the protocol's mandatory requirements.

## 5. Endpoints

| URL | Method | Request | Successful response |
|---|---|---|---|
| `/phpvpn.php` | `GET`, `HEAD` | no body | HTML operational page |
| `/phpvpn.php?action=health` | `POST` | one `auth-none` JSON object | one authenticated `health` JSON object |
| `/phpvpn.php?action=info` | `POST` | one `auth-semi` or extended `auth-full` JSON object | one `info` JSON object |
| `/phpvpn.php?action=data` | `POST` | NDJSON beginning with `auth-full` | NDJSON with data records and result |

Other methods return `405 method_not_allowed` with an `Allow` header. An unknown action returns `404 not_found`.

### 5.1 Operational page

`STATUS_PAGE_ENABLED` defaults to `true`. Root GET returns standalone HTML based on `assets/index.html` with the heading `<h1>PHPVPN server is running</h1>`, a 32-random-byte generator using Web Crypto and copying of 64 lowercase hex characters. HTML, CSS and JavaScript are embedded directly in the script. An empty, default, zero-byte or ASCII-zero secret shows the entire red warning `aside` panel. Another secret removes it. When `false`, GET returns `404 not_found` without HTML. HEAD has the corresponding status and headers without a body. The switch does not restrict the REST API. The new data default `./phpvpn_data` neither moves nor copies old data from `./phpvpn`; the administrator performs any migration manually.

The page also contains a `Show HTTP Headers` button. It opens a modal `<dialog>` titled `HTTP headers`, a brief note that headers seen by the script may be incomplete and that exact values are available in the browser's Network tab after pressing F12, a read-only field, `Copy` and `OK` buttons and a close button. The field contains current request headers, one per line as `Name: value`, separated by LF with no trailing LF and HTML-escaped. Values are decoded as UTF-8, replacing invalid sequences with U+FFFD. The source depends on the implementation:

- PHP: `getallheaders()`, otherwise `$_SERVER['HTTP_*']`, as supplied by the web server;
- Python: ASGI headers with names converted to Title-Case because Uvicorn lowercases them;
- Node.js: `req.rawHeaders` preserving the original spelling of names.

Python and Node.js combine repeated headers into one line, joining values with `, `, or `; ` for `Cookie`. All three servers use the same `assets/index.html` template. HTML entity spelling in the field may differ, but decoded text is identical. `Host`, `Connection`, `Content-Type`, `Content-Length` and `Transfer-Encoding` are omitted because the client manages them and its HTTP headers validator rejects them. The result can be pasted unchanged into Configuration Wizard → HTTP headers. The page thus returns only the client's own headers, including any `Cookie`, `Authorization` or proxy-added headers. The response is `no-store`. Below the panel is a gray `Developed by www.pilandrome.com (MIT License)` line linking to `https://www.pilandrome.com`.

This public page performs only the stated check and does not disclose configuration. The warning is informational: a default, empty or zero `SERVER_SECRET` does not block `health`, `info` or `data` and must not by itself produce `503 server_misconfigured`. However, since such a value is publicly predictable, the `health` response tag provides no practical proof of server identity.

### 5.2 `health`

`health` uses `POST` and Content-Type `application/json; charset=utf-8`. The request contains exactly one `auth-none` JSON object and no additional bytes:

```json
{"type":"auth-none","requestId":"c22cd6e8-14a9-4d83-97b7-54c26a72d1f0"}
```

- `auth-none` is deliberately an unauthenticated request type;
- it contains no `timestamp`, `src`, `tag`, secret or other field and proves knowledge of no secret;
- the client generates a new UUIDv4 `requestId` for every attempt and must not accept a response belonging to a different request;
- after syntax validation, the server answers every requester, maintains no session or persistent state for `auth-none` and processes replay as a new independent request.

A successful response has Content-Type `application/json; charset=utf-8` and exactly these fields:

```json
{"serverVersion":"1.0.0","status":"running","description":"OK","timestamp":"2026-08-09T20:15:30.123Z","requestId":"c22cd6e8-14a9-4d83-97b7-54c26a72d1f0","iv":"BASE64URL","tag":"BASE64URL"}
```

- `serverVersion` is the server application version;
- `status` is `running` on success;
- `description` is `OK` on success;
- `timestamp` is the current server UTC time with millisecond precision, exactly in `YYYY-MM-DDTHH:MM:SS.sssZ` format;
- `requestId` is the exact value from the corresponding `auth-none` request;
- `iv` is 12 fresh random bytes from a CSPRNG, encoded as unpadded base64url;
- `tag` is a 16-byte AES-256-GCM authentication tag encoded as unpadded base64url and calculated according to section 7.3.

JSON object member order is not semantic. Before using `timestamp`, a client knowing `SERVER_SECRET` MUST verify a valid 24-character UTC time in `YYYY-MM-DDTHH:MM:SS.sssZ` format, verify `requestId` and IV length, derive the key and verify the GCM tag. Until successful verification, it must not change its time offset or create an authenticated request based on the received time. It discards the entire response if the tag is missing or invalid, `requestId` mismatches or another field is invalid.

Before its first authenticated request, the client must obtain and verify server time through `health`. During operational recovery it repeats this after `401` when clock skew could be the cause. A one-shot diagnostic check ends after an error, including `401`, without automatic retry. A new separately requested authenticated check begins with a new verified `health`. The server does not verify request-side knowledge of `SERVER_SECRET`, but a valid response proves that knowledge to a client capable of independently verifying the tag.

## 6. Common wire format

### 6.1 JSON

- Encoding is UTF-8 without BOM.
- Protocol fields use ASCII only.
- A JSON object MUST NOT contain duplicate member names.
- An unknown field in an authentication, data or result record is `400 bad_request`.
- Numeric fields are nonnegative integers within the safe PHP integer range.
- Base64 values use exclusively canonical base64url under the rules below.
- Binary HMAC and tag comparisons run in constant time.

A PHP implementation must detect duplicate top-level keys before or during decoding; `json_decode()` alone silently overwrites them and is insufficient for this check.

`BASE64URL(x)` denotes the single canonical textual encoding of the exact bytes `x` using the alphabet `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_` in that order, without `=` or whitespace. Canonical length modulo 4 may only be 0, 2 or 3; remainder 1 is always invalid. The empty string is the canonical representation of an empty byte sequence, but a particular field may forbid it or require a different decoded length.

Unused low bits in the last character MUST be zero:

- for length modulo 4 equal to 2, the last character's six-bit value must have zero low 4 bits;
- for length modulo 4 equal to 3, it must have zero low 2 bits;
- for length modulo 4 equal to 0, there are no unused bits.

The recipient MUST check canonicality before using a value, checking its decoded length or placing its text in AAD or another canonical string. An equivalent mandatory check is to verify alphabet and length, add only the padding needed by the decoder, decode strictly, re-encode using `BASE64URL` and require byte equality with the original text. Successful decoding alone is insufficient because a tolerant decoder may ignore nonzero unused bits. For example, `_w` canonically encodes byte `ff`, whereas `_x` must be rejected even if a tolerant decoder produces the same byte. Similarly, `__8` canonically encodes `ffff`, whereas `__9` is noncanonical.

A noncanonical base64url request value is `400 bad_request`. On a noncanonical response value, the client rejects the whole response and must use none of its plaintext, time, send result or payload. After successful validation, the original text is identical to `BASE64URL` of the decoded bytes; the implementation may therefore use either the original text or the re-encoded value for AAD or another canonical string with identical results.

### 6.2 NDJSON

NDJSON is a sequence of compact JSON objects. Every record, including the last, MUST end with one LF byte (`0x0A`). CRLF, an empty line and a fragment lacking the final LF are invalid. Sizes are measured from received bytes, not decoded JSON.

### 6.3 Identifiers

`requestId` and `messageId` are version 4 UUIDs in lowercase hexadecimal `8-4-4-4-12` format. They are generated by a CSPRNG.

Identifiers serve correlation. The server and client MUST NOT use them as a replay cache or cross-request deduplication key. `messageId` must be unique only within one `data` body so the response is unambiguous; replaying an entire request with the same identifiers processes it again.

## 7. Cryptographic primitives and canonicalization

V1 requires SHA-256, HMAC-SHA-256, HKDF-SHA-256 per RFC 5869 and AES-256-GCM with a 16-byte tag.

All textual canonical strings are ASCII/UTF-8, values are separated by one LF and the string has no trailing LF unless stated otherwise. `SHA256HEX(x)` is SHA-256 over the exact bytes `x`, written as 64 lowercase hexadecimal characters. `HMAC-SHA-256(key, x)` produces 32 bytes and is encoded in the HMAC `tag` field as unpadded base64url. The message type unambiguously distinguishes the HMAC `tag` in `auth-semi`, `auth-full` and `data-mac` from the 16-byte AES-GCM `tag` in `health`, `info` and `data`.

### 7.1 `auth-semi` key

Both client and server derive a single 32-byte key for the `auth-semi` request HMAC tag:

```text
authSemiSaltDomain = "phpvpn/auth-semi/salt"
authSemiKeyDomain  = "phpvpn/auth-semi/key"
authSemiSalt       = SHA-256(authSemiSaltDomain)   // 32 raw bytes
semiKey            = HKDF-SHA-256(SERVER_SECRET, authSemiSalt, authSemiKeyDomain, 32)
```

`authSemiSaltDomain` with suffix `/salt` and `authSemiKeyDomain` with suffix `/key` are public constant domain strings for context separation: the first specifies the hashed HKDF salt and the second directly forms HKDF `info`. They are neither secrets nor request fields; `auth-semi` transmits no salt or IV. The only input secret is `SERVER_SECRET`. `semiKey` is not transmitted, stored in protocol state or assigned an identifier.

### 7.2 `auth-full` key

After syntax validation of `src`, client and server derive a single 32-byte key for the `auth-full` request HMAC tag:

```text
authFullSaltDomain   = "phpvpn/auth-full/salt"
authFullKeyDomain    = "phpvpn/auth-full/key"
authFullSaltMaterial = authFullSaltDomain || NUL || src
authFullSalt         = HMAC-SHA-256(SERVER_SECRET, authFullSaltMaterial)   // 32 raw bytes
authFullInfo         = authFullKeyDomain || NUL || src
fullAuthKey          = HKDF-SHA-256(ACCESS_KEY, authFullSalt, authFullInfo, 32)
```

`authFullSaltDomain` and `authFullKeyDomain` are public constant domain strings for context separation: the first identifies salt derivation input and the second HKDF `info`. `NUL` is the single byte `0x00`, and `src` is the exact ASCII of the validated VPN address without leading zeros. Domain strings, separator, argument order and output length are normative. Unlike the public constant `authSemiSalt`, `authFullSalt` is client-specific and derived from `SERVER_SECRET` and `src`; the client's single `ACCESS_KEY` is then HKDF's input secret. Without both secrets, the correct `fullAuthKey` and HMAC tag cannot be computed. `fullAuthKey` is not transmitted, stored in protocol state or assigned an identifier.

An empty configured `SERVER_SECRET` is normalized to `CHANGE_THIS_SERVER_SECRET` before this derivation per section 3.2. An empty or zero input thus remains valid configuration, but as a publicly predictable value provides no second practical secret; the operational page must therefore warn about it.

### 7.3 `health` response authentication

For each `requestId`, client and server derive a separate 32-byte `health` response key from the effective `SERVER_SECRET`:

```text
healthSaltDomain   = "phpvpn/health-response/salt"
healthKeyDomain    = "phpvpn/health-response/key"
healthSaltMaterial = healthSaltDomain || NUL || requestId
healthSalt         = SHA-256(healthSaltMaterial)   // 32 raw bytes
healthInfo         = healthKeyDomain || NUL || requestId
healthKey          = HKDF-SHA-256(SERVER_SECRET, healthSalt, healthInfo, 32)
```

Domain strings, the single `NUL` separator, exact ASCII bytes of UUID `requestId` and output length are normative. `requestId` separates each attempt's key and binds the response to the fresh client challenge. The sole secret input is the effective `SERVER_SECRET`; `auth-none` itself sends no secret.

The server constructs AAD from the exact validated response values:

```text
aad = "PHPVPN-HEALTH-RESPONSE" LF
      "health" LF
      requestId LF
      serverVersion LF
      status LF
      description LF
      timestamp LF
      BASE64URL(iv)
```

The server performs AES-256-GCM with `healthKey`, the 12-byte `iv`, the specified `aad` and empty, zero-byte plaintext. Ciphertext is therefore also empty and is not transmitted. The resulting 16-byte GCM tag authenticates the IV and all public response fields. This use of AES-GCM for authentication with empty plaintext corresponds to GMAC; `SERVER_SECRET` is not appended as public data but forms the tag's secret key material through `healthKey`.

The client constructs the same AAD from the exact received values and verifies the tag using software AES-256-GCM. Only then may it use `timestamp`. Changing `serverVersion`, `status`, `description`, `timestamp`, `requestId`, IV or tag invalidates verification. A response with a valid tag but a `requestId` other than the currently expected one is also invalid. Neither server nor client persists `requestId` or IV or creates a replay database.

### 7.4 Software AES-256-GCM implementation

AES-256-GCM is a mandatory cryptographic primitive. Every PHP implementation must use a single bundled pure-PHP software implementation for all its operations without exception. It MUST NOT delegate operations to `openssl_encrypt()`, `openssl_decrypt()`, Sodium or any other cryptographic extension. This applies to `health` response authentication, `info` response encryption and encryption, authenticated decryption and verification of end-to-end `data` payloads.

The software implementation must provide the complete AES-256-GCM operations needed for encryption, authenticated decryption and tag verification. This includes at least the AES-256 forward block transform, GCTR counter mode and GHASH authentication function. Since GCM uses only the forward AES transform in both directions, a separate inverse AES block transform is not required. The implementation must support the normative 32-byte key, 12-byte IV, any AAD specified by this protocol, empty plaintext for `health` GMAC and a 16-byte tag. It MUST NOT expose decrypted plaintext to the caller before successful verification of the entire GCM tag.

For the same key, IV, AAD and plaintext bytes, the software implementation must produce ciphertext and a tag matching normative AES-256-GCM. Presence or absence of OpenSSL, Sodium or another extension MUST NOT change the wire format, cryptographic output, key derivation, error codes or any other protocol behavior.

## 8. `info` action

### 8.1 `auth-semi` request

Content-Type is `application/json; charset=utf-8`. The body contains exactly one JSON object and no additional bytes:

```json
{"type":"auth-semi","timestamp":"2026-08-09T20:15:30.123Z","requestId":"4e94de3a-b9ad-4bc3-9297-82b98a19a143","tag":"BASE64URL"}
```

The HMAC tag is calculated as follows:

```text
canonical = "PHPVPN-AUTH-SEMI" LF
            "POST" LF
            "info" LF
            timestamp LF
            requestId

tag = HMAC-SHA-256(semiKey, canonical)
```

`timestamp` is a JSON string of exactly 24 ASCII characters in `YYYY-MM-DDTHH:MM:SS.sssZ` format, identical to `health.timestamp`, `info.generatedAt` and `lastSeen`. It must represent a valid Gregorian date and UTC time with millisecond precision; the year has four digits, other numeric components have exactly two except for the three-digit milliseconds, `T` and `Z` are uppercase, seconds are `00` through `59` and milliseconds `000` through `999`. A missing or differently sized fraction, an offset time zone and leap second `60` are forbidden. Invalid format or calendar value returns `400 bad_request`. After parsing, the server compares both UTC instants with millisecond precision and accepts the request only if the absolute difference is at most 60 seconds; a greater difference returns generic `401 unauthorized`.

The exact ASCII bytes of the validated `timestamp` enter `canonical` unchanged and without normalization. `tag` must be unpadded base64url decoding to exactly 32 bytes; invalid encoding or length returns `400 bad_request`. The server compares it with the calculated HMAC in constant time and a mismatch returns generic `401 unauthorized`.

`requestId` is a correlation value. The server persists neither `timestamp` nor `requestId`. The timestamp limits replay of old requests, but an identical valid request can be repeated inside the open 60-second window and will be accepted again.

### 8.1a Optional full-auth extension

Presence of `maskedCommunicationContracts` or `includeMaskedCommunicationContracts` enables full authentication, including `[]` and `false`. The request is one JSON object containing `type:"auth-full"`, `src`, `timestamp`, `requestId`, `tag` and at least one extension field. A standalone auth-full without an extension is invalid. The original request lacking both fields continues to use exactly auth-semi and the original 4096-byte limit.

`maskedCommunicationContracts` is an array of at most 252 objects of the exact form `{"peer":"10.99.99.3","hash":"<64 lowercase hex>"}`. The effective ceiling is also bounded by the number of valid VPN addresses minus the client's own address. A peer is a canonical VPN IPv4 address in the server's range, different from src and unique. No ACCESS_KEYS entry is required for a peer. Each hash must match `[0-9a-f]{64}`. Unknown members, duplicate members including escaped names, duplicate peers, null and object/array substitutions are rejected. `includeMaskedCommunicationContracts` is strictly boolean. The maximum entire body is `MAX_INFO_REQUEST_BODY_BYTES = 65536`, also bounded by MAX_REQUEST_BODY_BYTES.

A contract identifier is the pair of IPv4 addresses sorted numerically in ascending order and joined by `_`. The masked hash is lowercase SHA-256 of the UTF-8 bytes of `contract + "_" + communicationKeyLowerHex + "_" + configuredServerSecret`, with no trailing LF or NUL. The communication key is exactly 32 bytes represented by 64 lowercase hex characters. Here, the server secret is used exactly as configured, including case, whitespace, Unicode and an empty value. An empty secret is not replaced by the default for this hash. Neither plaintext key nor secret is sent.

Canonical object E contains only present extension fields, in order `maskedCommunicationContracts`, `includeMaskedCommunicationContracts`. Pair member order is peer, hash. E is compact JSON without whitespace, BOM or trailing LF. Arrays are not sorted during verification. The client builder sorts them numerically by peer before signing. An absent field differs from false or []. The extension hash is lowercase SHA-256 of the exact UTF-8 bytes of E.

```text
PHPVPN-AUTH-FULL
POST
info
<src>
<timestamp>
<requestId>
<extensionHash>
```

The signed input has no trailing LF. The tag is canonical unpadded Base64URL HMAC-SHA-256 with `fullAuthKey` derived exactly as for data. An empty server secret is replaced by the default only in authentication derivation. Data canonicalization and tags do not change. The server checks src against ACCESS_KEYS, performs dummy-key derivation and constant-time comparison for an unknown identity. Invalid identity, signature or time outside the 60-second window returns 401. Malformed input returns 400, size or count 413, Content-Type 415 and failed confirmed publication 503 queue_unavailable. Nothing is written before validation and authentication. Valid replay inside the time window remains accepted; no request-ID history is created.

### 8.2 Response plaintext

The server constructs one JSON object using `JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT`. Indentation and LF belong to the authenticated plaintext. Fields, their order and values remain the same. The following compact representation shows minimum contents, not exact serialized whitespace:

```json
{"serverVersion":"1.0.0","generatedAt":"2026-08-09T20:15:30.123Z","clients":[{"address":"10.99.99.2","lastSeen":"2026-08-09T20:14:52.000Z"}],"limits":{"maxDataRecordsPerRequest":16,"maxRequestBodyBytes":983040,"maxQueueBytesPerClient":16777216,"queueTtlSeconds":60,"minDataPollIntervalMs":50,"maxDataPollIntervalMs":3200}}
```

`generatedAt` is current server time exactly in `YYYY-MM-DDTHH:MM:SS.sssZ` format. `clients` contains only VPN addresses and approximate times of last successfully processed communication. It must contain no token, secret, physical IP, disk path or queue contents. A client without a valid statistics record may be omitted. `lastSeen` is UTC RFC 3339 exactly in `YYYY-MM-DDTHH:MM:SS.sssZ` format. Because of the 30-second update throttle and best-effort statistics, it is only approximate operational information, not an audit trail or authoritative proof of client availability.

`limits` describes the server's actual active values. The client must not rely solely on this specification's defaults. `minDataPollIntervalMs` and `maxDataPollIntervalMs` recommend lower and upper bounds for intervals between `data` calls; the client may follow them but they are not binding. The `info` response must disclose no name, configuration or other information about the server's internal storage.

With `includeMaskedCommunicationContracts:true`, every returned client also has `maskedCommunicationContracts`. For another client it contains at most one published pair whose peer is exactly the requester's authenticated src. Without a match, the array is empty. The client's own record always has []. No hashes, peers, original list length or hidden-item counts are returned for relationships between two other clients. A standard response without include true has no new field, even after publication.

Clients without valid lastSeen are always omitted, with neither null nor Never. Addresses are traversed numerically. MAX_INFO_PLAINTEXT_BYTES = 262144 is measured with `strlen` on the actual resulting UTF-8 bytes, including pretty whitespace. When exceeded, whole entries are removed starting from the highest IP and the object is serialized again. Exact equality is permitted. If even the base object does not fit, the response is server_misconfigured. GeneratedAt is created after publication or touch. Reading each stored list and its mtime uses a single file handle.

The extended client parser requires the extension field for every client, unique addresses, valid times, self with [] and at most one peer equal to expected src for each other client. A malformed last entry rejects the whole snapshot including limits. Empty clients is conservatively rejected in the extended flow because no record means an old server cannot be distinguished. Only the client's own marker represents a valid overview with zero other clients. The standard parser accepts empty clients.

The overview uses the signed difference generatedAt minus lastSeen from a single response. At most 60000 ms, including future lastSeen, means online; 60001 ms means offline. Hash equality with the current local configuration does not prove the state of the actually active tunnel.

### 8.3 Encrypted response

AES-GCM does not automatically create a salt. For every response the server therefore explicitly generates:

- `salt`: 32 random bytes;
- `iv`: 12 random bytes;
- `tag`: 16 bytes.

```text
info = "phpvpn/info-response/" || requestId
key  = HKDF-SHA-256(SERVER_SECRET, salt, info, 32)

aad = "PHPVPN-INFO" LF
      "info" LF
      requestId LF
      BASE64URL(salt) LF
      BASE64URL(iv)
```

The server encrypts the exact UTF-8 bytes of the indented plaintext JSON using AES-256-GCM with the specified `key`, `iv` and `aad`. It returns one JSON object:

```json
{"type":"info","requestId":"4e94de3a-b9ad-4bc3-9297-82b98a19a143","salt":"BASE64URL","iv":"BASE64URL","ciphertext":"BASE64URL","tag":"BASE64URL"}
```

The client MUST first check lengths, derive the key and verify the GCM tag over the entire ciphertext and AAD. It may parse plaintext only after successful tag verification.

Neither salt nor IV is an anti-replay mechanism. The protocol deliberately does not detect a repeated previously valid response. `requestId` binds the response to its request and prevents integrity substitution between transactions, but neither server nor client keeps a history of used values.

## 9. `data` action

### 9.1 Request order

Content-Type is `application/x-ndjson; charset=utf-8`. The request has exactly this order:

1. exactly one `auth-full` record;
2. zero to `MAX_DATA_RECORDS_PER_REQUEST` `data` records.

An `auth-full` alone is valid polling without outbound data.

### 9.2 `auth-full` record

```json
{"type":"auth-full","src":"10.99.99.2","timestamp":"2026-08-09T20:15:30.123Z","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","tag":"BASE64URL"}
```

The HMAC `tag` covers all bytes after the LF terminating the first line:

```text
body = exact bytes after the LF terminating auth-full

canonical = "PHPVPN-AUTH-FULL" LF
            "POST" LF
            "data" LF
            src LF
            timestamp LF
            requestId LF
            SHA256HEX(body)

tag = HMAC-SHA-256(fullAuthKey, canonical)
```

The server first checks `src` syntax, looks up exactly one `ACCESS_KEY` in the `ACCESS_KEYS` constant, combines it with the single `SERVER_SECRET` into `fullAuthKey` per section 7.2 and uses that key to verify the HMAC tag. It must read and hash the entire body. Before successful HMAC verification it MUST NOT modify the buffer or publish data. For an unknown client, it substitutes a fixed random-looking 32-byte dummy key for `ACCESS_KEY`, performs the same derivation with `SERVER_SECRET` and `src` and returns generic `401 unauthorized` so response timing does not reveal client existence.

`timestamp` follows the same exact UTC format, calendar validation and at-most-60-second absolute skew check as `auth-semi`. Its exact ASCII bytes belong to `canonical` in the stated order, so changing any character invalidates the HMAC `tag`. This `tag` also decodes to exactly 32 bytes and is compared in constant time. The timestamp provides bounded freshness against old replay; the same authenticated request can be replayed within the valid 60-second window and is processed again. The server does not persist the timestamp.

### 9.3 `data` record

```json
{"type":"data","messageId":"d5b543a9-9fc3-4c98-916c-ea0198f67a79","src":"10.99.99.2","dst":"10.99.99.4","iv":"BASE64URL","tag":"BASE64URL","ciphertext":"BASE64URL"}
```

A `data` record MUST be a compact UTF-8 JSON object without whitespace outside string values, in exactly this member order: `type`, `messageId`, `src`, `dst`, `iv`, `tag`, `ciphertext`. Another member MUST NOT follow `ciphertext`. Its value is the object's final string value; its closing `"` is immediately followed by `}` and LF. All `data` values are ASCII and `ciphertext` is canonical unpadded base64url, so it MUST NOT contain JSON escape sequences.

An implementation may first read and validate the short header ending with the literal prefix `,"ciphertext":"`, then stream the `ciphertext` value. It must still verify canonical base64url, closing `"}` and LF; it MUST reject any other representation, whitespace, changed member order or additional member after `ciphertext`.

Value rules:

- `src` must exactly match `src` in `auth-full`; otherwise `401 unauthorized`;
- `dst` must be a valid VPN address;
- `messageId` must be unique within exactly one request, but is not compared with history;
- decoded `iv` must be 12 bytes;
- decoded `tag` must be 16 bytes;
- `ciphertext` must not be empty;
- `data` record size is bounded only by the overall `MAX_REQUEST_BODY_BYTES` body limit.

### 9.4 End-to-end payload encryption

The sender looks up the single 32-byte `PAYLOAD_KEY` in its local `PAYLOAD_KEYS[src][dst]` map. The protocol performs no further key derivation and transmits no key identifier. AAD is:

```text
aad = "PHPVPN-DATA" LF
      "data" LF
      messageId LF
      src LF
      dst LF
      BASE64URL(iv)
```

Client `maxQueuedPacketAgeMs`, default 5000 ms, uses a local monotonic clock and bounds waiting before local selection. It is independent of server `QUEUE_TTL_SECONDS` and protocol UTC timestamp. It introduces no server reservation, server TTL requirement or limit on a transmission already underway.

Local flow fair queuing does not change the wire format, AES-GCM, HMAC authentication, server API or data record meaning. The authoritative description of peer queues, sixteen sub-buckets, DRR and logical reservations is in [client.md](client.md#symetrické-flow-fronty-a-logické-rezervace). Selection before batch construction and scheduling after complete batch verification are internal client operations. Packet order in an already constructed plaintext and authenticated NDJSON line order are preserved during encoding, decoding and verification. Bucket, owner, deficit and monotonic-time metadata is not serialized.

Plaintext is one binary batch of IPv4 packets for the exact ordered pair `src`, `dst`. All multibyte values use network byte order (big-endian):

```text
packetCount                     : unsigned 32-bit integer
packetLength[0..packetCount-1]  : unsigned 16-bit integer
packetBytes[0..packetCount-1]   : exactly packetLength[i] bytes of an IPv4 packet
```

`packetCount` MUST be at least 1. Every `packetLength` MUST be 20 through 65535 and every `packetBytes` entry MUST be a complete syntactically valid IPv4 packet whose IPv4 header length and Total Length field exactly match `packetLength`. The sum of the four-byte header, all two-byte lengths and all `packetBytes` MUST exactly equal the plaintext length; trailing data and partial entries are forbidden.

The sender constructs a batch only from packets whose inner source and destination IPv4 addresses exactly match outer `src` and `dst`. It encrypts the entire batch plaintext with AES-256-GCM and transmits ciphertext and the 16-byte tag. Before passing packets to the virtual adapter, the recipient must:

1. discard the line if `dst` is not its single configured VPN address;
2. look up the single key by exact routing addresses `src`, `dst`;
3. construct exact AAD;
4. verify the GCM tag and only then obtain plaintext;
5. validate `packetCount`, the length table, exact plaintext length and every IPv4 packet;
6. verify each packet's inner source and destination against `src`, `dst`;
7. only after validating the whole batch, pass valid packets in plaintext order to local client scheduling. An invalid batch must not be passed partially. Subsequent virtual-adapter write order follows the client flow policy in [client.md](client.md#symetrické-flow-fronty-a-logické-rezervace), with FIFO inside sub-buckets.

The relay does not verify the tag because it has no `PAYLOAD_KEY`. Changing `src`, `dst`, `messageId`, IV or ciphertext invalidates the recipient's tag verification.

#### IV uniqueness

For each specific `PAYLOAD_KEY`, the sender MUST prevent reuse of the same 12-byte IV to encrypt two different plaintexts. The sender uses 12 random bytes from a CSPRNG.

The client keeps no received-IV database and does not persist IVs for replay checks. IV uniqueness is the sender's responsibility for correct AES-GCM, not a replay detection mechanism. Repeated delivery of the same IV/ciphertext/tag triple is accepted as another valid message.

### 9.5 `data` response

A successful response has Content-Type `application/x-ndjson; charset=utf-8` and this order:

1. zero or more directly stored `data` records;
2. exactly one `data-result` record;
3. exactly one final `data-mac` record.

After successful request verification and processing outbound records, an atomic operation retrieves all unexpired data records currently in the client's buffer and simultaneously removes them from persistent storage. Draining the caller's incoming buffer MUST NOT depend on successful publication of any or all outbound records. The server therefore drains it even if capacity prevents accepting every outbound record. A newly published record with `dst` equal to authenticated `src` thus already belongs in this response. Only then does the server build a response from the resulting local list. No separate protocol or configuration count limit applies to returned records, and the response is not paginated. Buffer capacity is the only upper volume bound. Each record is returned directly in the same `data` schema without another envelope:

```json
{"type":"data","messageId":"e8edc241-96ec-4b51-9336-1fb0836f4db5","src":"10.99.99.4","dst":"10.99.99.2","iv":"BASE64URL","tag":"BASE64URL","ciphertext":"BASE64URL"}
```

The `data-result` record:

```json
{"type":"data-result","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","acceptedMessageIds":["d5b543a9-9fc3-4c98-916c-ea0198f67a79"],"deferredMessageIds":[],"acceptedRecords":1,"acceptedBytes":2048}
```

- `acceptedMessageIds` are the only entries the sender may remove from its outbound queue;
  Unacknowledged local selections return to their original client buckets with the same first-enqueue time under [rollback and expiry rules](client.md#monotónní-expirace-a-rozpracované-pakety). Client expiry before the next selection is neither a server acknowledgement nor a new delivery guarantee.

- `deferredMessageIds` remain with the client for a later attempt;
- the two disjoint arrays exactly cover every request `messageId`;
- `acceptedBytes` sums accepted serialized data lines including LF.

Example polling result with neither sent nor returned data:

```ndjson
{"type":"data-result","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","acceptedMessageIds":[],"deferredMessageIds":[],"acceptedRecords":0,"acceptedBytes":0}
{"type":"data-mac","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","tag":"BASE64URL"}
```

The server authenticates the count, order and completeness of data lines and the exact `data-result` using `fullAuthKey` derived for the current request. Let `recordCount` be the number of response `data` lines, `dataResultLine` the exact UTF-8 bytes of the `data-result` JSON object without its terminating LF, and `messageId_i`, `gcmTag_i` pairs taken from the i-th data line in response order. `gcmTag_i` is the exact base64url string of that line's `tag` field. The server constructs:

```text
canonical = "PHPVPN-DATA-RESPONSE" LF
            "data" LF
            requestId LF
            DECIMAL(recordCount) LF
            messageId_1 LF
            gcmTag_1 LF
            ...
            messageId_recordCount LF
            gcmTag_recordCount LF
            SHA256HEX(dataResultLine)

responseTag = HMAC-SHA-256(fullAuthKey, canonical)
```

`DECIMAL(recordCount)` is unsigned nonnegative decimal without leading zeros; zero is `0`. For `recordCount = 0`, the pair block is omitted entirely. `canonical` ends at the last hexadecimal character of `SHA256HEX(dataResultLine)`, without LF. Only the small `data-result` line is hashed, not data-line ciphertext. `data-mac` has exactly three fields:

```json
{"type":"data-mac","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","tag":"BASE64URL"}
```

Its `requestId` must match both the current request and `data-result`. `tag` is unpadded base64url decoding to exactly 32 bytes of `responseTag`.

The client MUST receive the complete NDJSON body in the specified order with exactly one final `data-mac`. It constructs `canonical` from received data lines in the same order, uses the exact received `dataResultLine` bytes, computes HMAC and compares in constant time. Missing or invalid `data-mac`, mismatching `requestId`, a different pair count or order, or modified `data-result` invalidates the whole response. The client then MUST NOT remove any outbound queue entry or deliver any received payload, and discards the response.

During normal bootstrap, recovery and operational polling, only after successful `data-mac` verification does the client use `acceptedMessageIds` and independently verify each data line under section 9.4 AES-GCM rules. It discards a line with an invalid GCM tag and may process other valid payloads. GCM verification protects each record's fields and ciphertext; HMAC over ordered pairs protects its presence and position. The protocol sends no subsequent acknowledgement.

The client may perform a one-shot diagnostic access check with at most one empty `data` request whose body contains only `auth-full` and LF. Before reporting success it must completely verify HTTP success, NDJSON structure, `requestId` correlation in both `data-result` and `data-mac`, all size and count limits, canonicality and routing rules including `dst` matching the checked address, the empty request's zero result (empty accepted/deferred arrays and zero acceptedRecords/acceptedBytes), and a valid final `data-mac`. It may then discard all received data records without payload GCM verification or decryption. It must not store, deliver to the network layer, enqueue operationally, pass to another client component or otherwise use any received payload. It must not remove an operational outbound queue entry or change client operational state.

This check verifies the Access Key and authenticated server-response structure. It does not prove authenticity, integrity or readability of individual payload plaintexts. This exception does not apply to normal connection bootstrap, recovery or regular operational `data` polling. Those paths must still perform full GCM verification of every processed data record after `data-mac` verification and process valid payloads under the usual rules.

A diagnostic request has the same at-most-once consequences as any polling. It can permanently remove pending messages even if its result never reaches the client. The requester MUST be informed of this effect in advance. Automatically repeating any part of one-shot diagnostics after an error, including `401`, is forbidden. Another separately requested authenticated check begins with fresh verified time and may remove further messages again.

Once a worker atomically claims a particular batch file for sending, no other worker may deliver it. If the worker fails before sending, the response is lost or incomplete, has an invalid `data-mac` or is not processed by the client, already claimed batches are not restored. This is deliberately accepted optimistic at-most-once behavior per batch. `data-mac` provides result authenticity and detects changed data-line count or order, but does not guarantee delivery.

## 10. Queues and delivery semantics

### 10.1 Model

Each destination client has a logical persistent buffer. Its size includes all records not yet delivered. The default limit is 16 MiB of logical serialized bytes per client, excluding internal backend overhead.

The server may create separate batches by destination from a single request. Publication of a batch must be atomic: after a crash, either the whole batch is visible or none of it. Success for one destination is independent of failure for another.

When constructing a response, the server finds all unexpired published batches in the authenticated client's buffer. It atomically claims each before sending; it skips a batch already claimed by another worker. The returned list can contain multiple previously published batches. The server must neither paginate it nor limit its record count.

### 10.2 Partial acceptance

The server groups outbound records by `dst` and processes destination groups deterministically. Publication of one group is atomic: it publishes all its records or none. Accepted entries appear in `acceptedMessageIds`, unaccepted entries in `deferredMessageIds`.

For each destination queue written by the current request, the server:

1. attempts to atomically publish the entire destination group;
2. if insufficient capacity prevents the group from fitting, performs exactly one targeted sweep of that queue, removing all its expired records;
3. after the sweep, retries atomic publication of the entire group exactly once;
4. if the group still does not fit, writes none of it to that queue and lists all its `messageId` values in `deferredMessageIds`.

Within one `data` request the server may perform at most one capacity-triggered sweep and at most two publication attempts per destination queue. Insufficient capacity is not an error for the entire request. An authenticated, syntactically valid request always continues by draining the caller's incoming buffer and returns `200`, `data-result` and final `data-mac`, even when all outbound entries are deferred. The server MUST NOT return HTTP `429` for insufficient destination queue capacity. Backend unavailability returns `503 queue_unavailable`.

An error response never authorizes the client to remove an outbound entry. On success, the client evaluates the single syntactically valid `data-result` with matching `requestId` only after successfully verifying the immediately following final `data-mac` under section 9.5.

### 10.3 Delivery, loss and replay

Draining an entire queue is not atomic. The server claims, delivers and deletes batch files sequentially. However, claiming each individual batch must be atomic: under concurrent processing, at most one worker may deliver it. A concurrently published batch not found during the worker's initial queue scan remains for the next poll. The server does not wait for a client response and does not restore claimed or removed batches. Worker failure, connection interruption, response dropping or client error after claiming can therefore lose a batch. Higher layers of the transported IP traffic may retransmit under their own rules.

Neither client nor server compares `messageId`, IV, request body or authentication HMAC tag with history. Replaying an authenticated sending request within its valid 60-second window creates another data copy; once the window closes, the server rejects it. Replaying a data response may deliver the payload again even though the server issued the buffered record only once. Duplication in transit and loss after draining are explicitly accepted limitations.

The same rules apply to replaying a polling request without outbound data. Successful verification authorizes each replayed copy to perform `drainAll`; the first copy to encounter a nonempty queue removes the records and receives them in its response. The server must not reserve them for the original connection, return them to the queue or distinguish legitimate and replaying copies. Communication recovery can be expected only when the client or a higher layer creates new packets; this operational compensation does not change acceptance of the possibility of a sustained attack.

### 10.4 Expiry

Default `QUEUE_TTL_SECONDS` is 60 s, starting at first successful publication. After expiry, the backend may delete the batch without delivery. Maintenance may be opportunistic, so physical deletion can occur later; nevertheless, an expired batch must neither appear in a response nor count toward used queue capacity.

Alongside opportunistic maintenance, the backend must support a targeted sweep of one queue under section 10.2. A capacity-triggered sweep removes every batch file in that queue expired at request-processing time, regardless of `.receiving`, `.data` or `.sending` state. Its purpose is to free actually available capacity before the single publication retry; it must not traverse other clients' queues.

### 10.5 Storage driver

The wire protocol must not depend on filenames, directories or database technology. The implementation uses an interface with operations corresponding to at least:

- `enqueue(destination, records, expiresAt)`;
- `drainAll(client, now)`;
- `usage(client)`;
- `readClientStats()` and `touchLastSeenBestEffort(client, now)`;
- `sweepQueue(destination, now)` for targeted expiry removal in one queue;
- `sweep(now)` for bounded opportunistic maintenance.

The mandatory `disk` driver uses per-client directories and atomic `rename()` within one filesystem. `BUFFER_DIRECTORY` sets its root. Its default is `./phpvpn_data`, resolved relative to the `phpvpn.php` directory; the administrator may select another absolute or relative path. A batch is written with suffix `.receiving`, renamed to `.data` after the write completes, then atomically renamed by a worker to `.sending` before transmission. Only the worker that successfully renamed `.data` to `.sending` may deliver and delete it. Before the state suffix, the batch name is `YYYY-MM-DDTHH-MM-SS.sssZ_<bytes>_<src>_<uuid>`, whose first field is the only time value: UTC expiry time with exactly three millisecond digits. Hyphens between hours, minutes and seconds replace colons for filename safety; `Z` denotes UTC. Example: `2026-08-12T14-37-05.123Z_2048_10.99.99.2_d5b543a9-9fc3-4c98-916c-ea0198f67a79.data`. `<bytes>` is the serialized byte count, `<src>` the validated source VPN address and `<uuid>` a random version 4 UUID. Each client directory may also contain an `info.json` statistics marker per section 10.6; it is not a batch and queue operations must ignore it. `STORAGE_DRIVER` defaults to `disk`; future `memcached`, `mariadb`, `mysql` or `postgresql` drivers must preserve the same per-batch atomic claiming and capacity behavior.

The directory for VPN address `a.b.c.d` is `BUFFER_DIRECTORY/a_b_c_d`. Its name is formed solely by replacing the address's three dots with underscores; for example, destination `10.99.99.2` uses `BUFFER_DIRECTORY/10_99_99_2`. The directory always belongs to destination `dst`, not the sender named in a batch filename. In the default range, the root can contain at most 253 such subdirectories for `10.99.99.2` through `10.99.99.254`.

Root and per-client directories are created lazily and only from already validated VPN addresses. The server must create a missing `dst` directory before writing or draining its queue. After successful authentication and normal `data` processing, it must similarly make a best-effort attempt to create authenticated `src`'s directory for creating or updating its `info.json` marker; failures follow section 10.6. An `info` request or merely reading configuration does not create directories for other clients.

### 10.6 `disk` driver client statistics

The `disk` driver stores statistics separately for each client in `info.json` in its per-client directory. There is no central statistics file. `info.json` is a marker: its modification time (`mtime`) represents that client's `lastSeen` at filesystem-provided precision.

A fresh ordinary-activity marker is empty. Confirmed full-auth publication atomically replaces it with JSON `{"maskedCommunicationContracts":[{"peer":"IP","hash":"HEX"}]}`. Explicit [] clears the previous list. Publication bypasses the thirty-second throttle. It writes to a unique `.info-<uuid>.tmp` in the same directory, checks every byte, flushes, sets mtime and renames without first unlinking the target. Failure preserves the old file and returns 503 queue_unavailable. Extended reading obtains contents and mtime from one handle. A malformed, oversized or historically empty file supplies only [] while preserving readable lastSeen. Temp files are not queues; bounded sweeping removes only exact matching names older than INFO_TEMP_TTL_SECONDS = 300.

After successful authentication and normal processing of every `data` request, the server:

1. reads metadata once to obtain `mtime` of authenticated `src`'s `info.json`;
2. only if the file is missing, `mtime` cannot be obtained or valid time is more than 30 seconds older than current server time, makes a single `touch()` attempt with current time;
3. creates a missing marker as an empty file on this attempt; changes only `mtime` of an existing marker;
4. performs no update at exactly 30 seconds old or for a future `mtime`.

The `info` endpoint traverses configured clients and attempts to read only each marker's `mtime`. It converts a successfully read, representable time to UTC RFC 3339 exactly in `YYYY-MM-DDTHH:MM:SS.sssZ` format for `lastSeen`, expressing unavailable filesystem precision as zeros in the relevant milliseconds. A client with a missing or unreadable marker, invalid `mtime` or missing directory is omitted from `clients`. Marker contents are not read even during `info`.

Ordinary touch is strictly best effort and preserves stored JSON. Confirmed publication has the separate error contract above. Failure to read metadata, create a marker or change its `mtime` MUST NOT change HTTP status, `data-result`, corresponding `data-mac`, outbound publication, incoming buffer draining or payload delivery. For statistics the implementation MUST NOT retry, wait, acquire a file lock or return an error. Concurrent updates to one marker are allowed and the last completed metadata operation wins; different clients use different markers and do not overwrite each other's statistics.

`info.json` MUST NOT be directly accessible through HTTP or HTTPS. `BUFFER_DIRECTORY` should be outside the document root; if the administrator uses default `./phpvpn_data` inside it, the web server MUST explicitly deny direct access to that entire directory. The `info` endpoint reveals neither marker name, location nor errors. The marker MUST NOT count toward used queue capacity, be delivered in `data`, be renamed between batch states or be deleted by TTL or opportunistic sweeping.

## 11. Limits and timeouts

### 11.1 Default limits

| Constant | Default | Meaning |
|---|---:|---|
| `MAX_DATA_RECORDS_PER_REQUEST` | 16 | Number of `data` records in one request. |
| `MAX_AUTH_RECORD_BYTES` | 4096 B | First line including LF. |
| `MAX_REQUEST_BODY_BYTES` | 983040 B | Entire NDJSON request body including all LF. `983040 B` means 960 kB. Deliberately below Nginx and Tengine's default 1 MiB limit. |
| `MAX_INFO_REQUEST_BODY_BYTES` | 65536 B | Entire extended info body, at most MAX_REQUEST_BODY_BYTES. |
| `MAX_MASKED_COMMUNICATION_CONTRACTS` | 252 | Also bounded by the number of valid client addresses minus one. |
| `INFO_TEMP_TTL_SECONDS` | 300 s | Age of temporary info publications for bounded sweeping. |
| `MAX_INFO_PLAINTEXT_BYTES` | 262144 B | Plaintext of encrypted `info` response. |
| `MAX_QUEUE_BYTES_PER_CLIENT` | 16777216 B | 16 MiB logical persistent queue. |
| `AUTH_TIMESTAMP_SKEW_SECONDS` | 60 s | Maximum absolute authentication timestamp skew from server time. |
| `QUEUE_TTL_SECONDS` | 60 s | Maximum pending batch age. |
| `LOCK_TIMEOUT_MS` | 200 ms | Maximum PHP worker wait for a storage lock. |
| `MIN_DATA_POLL_INTERVAL_MS` | 50 ms | Recommended minimum interval between `data` calls. |
| `MAX_DATA_POLL_INTERVAL_MS` | 3200 ms | Recommended maximum interval between `data` calls. |

Configuration invariants must ensure that `MAX_REQUEST_BODY_BYTES` accommodates the authentication line and maximum count of maximum-sized records, and that `MIN_DATA_POLL_INTERVAL_MS` is positive and no greater than `MAX_DATA_POLL_INTERVAL_MS`. PHP `post_max_size`, proxy and web server request limits must be strictly greater than `MAX_REQUEST_BODY_BYTES`. The web server and proxy must transfer a response containing the whole `MAX_QUEUE_BYTES_PER_CLIENT` queue plus `data-result` and final `data-mac` without pagination; otherwise diagnostics must report misconfiguration.

Exceeding a syntax limit returns `413 payload_too_large`. Insufficient persistent destination queue capacity is neither a syntax nor protocol error. After one targeted sweep and one retry, the server returns unaccepted items in `deferredMessageIds` of a successful `200` response.

### 11.2 Timing behavior

The server responds by immediately draining the current buffer. It never waits for new data, uses `sleep`, long polling or indefinite-length streaming.

Recommended client defaults:

- connect timeout 5 s;
- total request timeout 15 s;
- exponential backoff after a network error or `503`, starting at 250 ms, capped at 30 s, always with jitter;
- the client may bound `data` intervals using active `minDataPollIntervalMs` and `maxDataPollIntervalMs` from `info`; the recommended default range is 50 to 3,200 ms.

The total timeout is a single absolute monotonic deadline from request start, covering DNS, connection, TLS, sending, headers and the entire response body. Partial transfer or a phase change does not renew it. The stated 15 s is a general recommendation. The native client under `client.md` has its own normative configurable default `responseTimeoutSeconds=30`. Time-unit conversions and addition to monotonic time must be checked. Unrepresentable values and failure to set a timeout must not result in a negative, infinite or silently changed timeout. Expiry triggers safe cancellation with a short finite cleanup period and confirmed terminal state before replacing the request or releasing resources. The result remains timeout, not late success.

Backoff and automatic recovery apply to operational communication. One-shot diagnostics end after an error without automatic retry, including `401`. A new separately requested authenticated check begins with fresh verified `health`. Initial health/info of ordinary Connect also ends without retry if the client rejects an HTTP response because of status, contents, parsing or cryptographic verification, for example HTML instead of JSON or an unverifiable tag. Transient communication failures without a usable HTTP response retain backoff and show no HTTP details. Later operational phases and recovery exchanges are unchanged.

The server may send `Retry-After` with `503`. On operational retry the client respects it as a minimum wait. It keeps `deferredMessageIds` in its outbound queue for a later attempt but continues polling so an unavailable destination does not block incoming traffic.

## 12. States and transitions

### 12.1 Client communication

The protocol has no cryptographic session or login handshake. Unauthenticated `auth-none` creates no session and authenticated `health` only binds one response to `requestId`. Logical bootstrap is:

1. mandatory `POST health` with fresh `auth-none` before the first authenticated request; the client verifies the response GCM tag and only then uses version and time to correct its offset;
2. optional authenticated `info` for current limits and overview;
3. any number of independently authenticated `data` cycles;
4. termination simply by stopping requests.

Windows Connect uses health → ordinary info → empty full-auth data → full-auth info with complete publication and include true → tunnel. The fourth Communication contracts checkpoint succeeds only after full response verification. Failure here is terminal, including timeout and unknown publication outcome. Successful publication is not repeated during recovery within the same epoch. Early recovery before the fourth step must complete it. A new user reconnect loads saved configuration and publishes a new list.

There is no login, session ID, keepalive or logout. Each request can be processed by another PHP worker or node with shared storage and identical key configuration.

### 12.2 Stored batch state

```text
absent --write--> receiving --publish--> data --claim--> sending --send--> deleted
                 \________________________ TTL ______________________________/
```

A `.receiving` file is not a published batch. Atomic rename to `.data` publishes it, and atomic rename to `.sending` assigns it to exactly one sending worker. Failure during `enqueue` must not publish a partial batch. Failure after claiming into `.sending` may lose the batch, but never automatically reissue it. Sweeping may delete an expired file in any of these states.

## 13. Errors

An error body is a small generic JSON object:

```json
{"type":"error","code":"unauthorized"}
```

| HTTP | `code` | Meaning | Client action |
|---:|---|---|---|
| 400 | `bad_request` | Invalid JSON/NDJSON, order, field, value, UTC timestamp format, HMAC tag encoding or length, or duplicate key. | Correct the request; do not retry unchanged. |
| 401 | `unauthorized` | Invalid authentication HMAC tag, `src`, `SERVER_SECRET` or `ACCESS_KEY` configuration, forged `src` in a data record or timestamp outside the window. | During operational recovery refresh time through verified `health` and check secrets. End one-shot diagnostics without retry; start another requested check with fresh verified time. Do not delete data. |
| 404 | `not_found` | Unknown action. | Correct URL. |
| 405 | `method_not_allowed` | Forbidden method. | Follow `Allow`. |
| 413 | `payload_too_large` | Body or record-count limit exceeded. | Reduce request size. |
| 415 | `unsupported_media_type` | Wrong Content-Type. | Correct Content-Type. |
| 503 | `queue_busy` | Storage lock wait limit exhausted. | Short backoff. |
| 503 | `queue_unavailable` | Backend cannot be used safely. | Backoff; administrator inspection. |
| 503 | `server_misconfigured` | Inconsistent limits or other invalid server configuration. | Do not retry rapidly; fix server. |
| 500 | `internal_error` | Unexpected internal error. | Keep data, back off. |

Backoff actions in the table apply to operational recovery. One-shot diagnostics end on every error without automatic retry under 5.2 and 11.2.

An error response is unauthenticated. The client may use it for diagnostics or to trigger backoff, never as proof of acceptance. Errors must not disclose paths, tokens, secrets, another client's existence, queue contents or exception dumps.

Ordinary best-effort touch failure is not a protocol error. Explicit contract publication failure returns 503 queue_unavailable. After successful rename, losing the HTTP response does not imply rollback.

## 14. Compatibility and extension

`type` determines the message schema. `serverVersion` in `health` and `info` is an informational application SemVer version, not wire negotiation.

Normative message types have a closed field set. New optional information is added primarily to encrypted `info` plaintext, where clients must ignore unknown fields. An incompatible change to required fields, canonicalization or cryptographic algorithms requires a new message type name and new cryptographic domain strings.

There is no automatic downgrade. After a supported type fails, the client must not try a historical action or weaker authentication. `data` and `info` offer no aliases for old names.

## 15. PHP implementation implications

Recommended responsibility separation within one deployable PHP file:

- `Config` and `ConfigValidator`: keys, limits, storage driver and invariants;
- `Router` and `Http`: method, action, headers and generic errors;
- `JsonWire` and `NdjsonReader`: bounded reading, duplicate keys, strict fields and canonical base64url checked by re-encoding;
- `KeyDerivation` and `Authenticator`: HKDF, HMAC, canonicalization and constant-time comparison;
- `AesGcm`: a single pure-PHP software AES-256-GCM implementation for `health` GMAC, `info` encryption and end-to-end payload;
- `Storage` interface and `DiskStorage`;
- `ClientStats`: best-effort `mtime` reading and at most one update attempt on per-client `info.json`;
- `HealthHandler`, `InfoHandler` and `DataHandler`;
- `Maintenance`: targeted expiry sweep of one queue on capacity shortage and bounded opportunistic sweeping.

The server implementation needs `hash_hmac`, SHA-256, `random_bytes`, strict JSON and its own pure-PHP software AES-256-GCM for `health` and `info`. It may use `hash_hkdf()` for HKDF. PHP server and client must perform every AES-256-GCM operation exclusively through this implementation per section 7.4. They must contain neither cryptographic backend selection nor an AES-256-GCM branch using OpenSSL, Sodium or another extension.

The software implementation should implement only the scope needed by this protocol: forward AES-256, GCTR, GHASH, 96-bit IV, 128-bit tag and empty plaintext with nonempty AAD for GMAC. At initialization it must pass an AES-256-GCM known-answer test with fixed inputs and expected outputs. The result is remembered within the process so the test is not repeated for each request. The software must not handle implementation failure by disabling encryption, changing algorithms or sending plaintext.

Section 3.2 normalization must occur at startup before configuration is passed to `KeyDerivation`. PHP tests exactly zero string length, not general falsiness:

```php
$serverSecret = SERVER_SECRET;
if ($serverSecret === '') {
    $serverSecret = 'CHANGE_THIS_SERVER_SECRET';
}
```

Every subsequent cryptographic operation uses only `$serverSecret`. `empty()` MUST NOT be used because it would wrongly treat valid nonempty `"0"` as empty. After normalization, the input key of every prescribed `hash_hkdf()` call is nonempty: an originally empty `SERVER_SECRET` becomes `CHANGE_THIS_SERVER_SECRET`, any other value is by definition nonempty, and `auth-full` receives a validated 32-byte `ACCESS_KEY`. An empty configured `SERVER_SECRET` must therefore not cause `ValueError`, `500 internal_error` or `503 server_misconfigured`.

The default `data` body ceiling is `MAX_REQUEST_BODY_BYTES = 983040` bytes. An implementation may stream and incrementally hash it, but must defer every storage modification until `auth-full` verification. A response may contain the entire client buffer, approximately `MAX_QUEUE_BYTES_PER_CLIENT` plus `data-result` and `data-mac`. Response HMAC processes only ordered `messageId`/GCM tag pairs and the small `data-result` hash, not all ciphertext; it can be computed incrementally with constant extra memory. `memory_limit` must leave sufficient headroom for the request, entire response list and JSON structures.

The disk driver must use paths derived only from validated VPN addresses, locking with timeout, atomic publication and protection against web access to buffers and statistics markers. It should not use an unbounded global lock. The prohibition on locks for `info.json` does not change data-buffer locking rules. Post-response maintenance must have time and count budgets.

`lastSeen` statistics are considered only after a validly authenticated and normally processed `data` request. Their 30-second throttle, single metadata-update attempt and all failures are separate from the data-processing result under section 10.6.

## 16. Required test scenarios

### 16.1 Transport and routing

1. The same request over HTTP, HTTPS, direct connection and proxy produces the same application result.
2. Forged `X-Forwarded-*` headers do not affect authentication, `src`, queue or response.
3. Unknown action URLs and forbidden methods return the correct generic errors.
4. The root page shows H1; default `CHANGE_THIS_SERVER_SECRET`, empty or otherwise all-zero `SERVER_SECRET` additionally shows a red H2, while `health`, `info` and `data` remain functional.
5. `GET` and `HEAD` on `health` return `405` with `Allow: POST`. A valid `POST` contains exactly `auth-none` with fresh UUIDv4 `requestId`, without timestamp, tag or other proof of secret knowledge.
6. Successful `health` has exactly seven required fields: `serverVersion`, `status`, `description`, `timestamp`, original `requestId`, 12-byte `iv` and 16-byte `tag`; status is `running`, description `OK` and timestamp has exact millisecond UTC format `YYYY-MM-DDTHH:MM:SS.sssZ`.
7. The client does not change its time offset before GCM verification. After successful verification with effective `SERVER_SECRET` it derives the server-relative offset; a modified or forged response does not affect it.

### 16.2 JSON, NDJSON and limits

1. A valid empty poll contains only `auth-full` with LF.
2. Missing final LF, CRLF, empty line, duplicate key and unknown field are rejected. A `data` record with whitespace, different member order, `ciphertext` not last or another member after it is rejected.
3. One `data` line may use the remaining capacity of the whole request body; only a request whose total size exceeds `MAX_REQUEST_BODY_BYTES` is rejected.
4. Exactly 16 data records pass; a seventeenth returns `413` without storage changes.
5. Whole-body and buffer boundaries are tested at `limit-1`, `limit` and `limit+1`.
6. Invalid base64url, IV length, tag length, UUID, VPN address and unknown or extra fields are rejected.
7. Canonical `_w` for `ff` and `__8` for `ffff` pass general base64url validation. Noncanonical `_x`, `__9`, length remainder 1 modulo 4, `=`, whitespace and `+` or `/` are rejected even if the decoder can turn them into bytes. Decode–reencode must return exactly the original text for every accepted value.
8. A successful response contains zero or more direct `data` lines, exactly one `data-result` and exactly one final `data-mac`; a line after `data-mac` is invalid.
9. `auth-none` has exactly `type` and `requestId`; missing, unknown or extra fields including `timestamp`, `src` or `tag` return `400 bad_request`.
10. A two-IPv4-packet payload batch passes only with exact big-endian `packetCount`, length table and matching IPv4 Total Length values. Zero `packetCount`, length below 20, trailing byte, incomplete length table, incomplete packet or inner `src`/`dst` mismatch discards the entire batch without delivering any packet.

### 16.3 Cryptography

1. Known-answer tests for HKDF-SHA-256, HMAC-SHA-256 and AES-256-GCM.
2. Both `auth-semi` and `auth-full` carry `timestamp` as a JSON string exactly in `YYYY-MM-DDTHH:MM:SS.sssZ` format; numeric values, invalid calendar dates, lowercase letters, missing or differently sized fractions, offsets, missing `Z` and second `60` are rejected.
3. An `auth-semi` known-answer test constructs `canonical` in order domain string, `POST`, `info`, exact timestamp, `requestId`; changing one timestamp character changes the 32-byte HMAC `tag`. Changing `ACCESS_KEY` must change neither `semiKey` nor tag, because `auth-semi` depends only on `SERVER_SECRET`.
4. An `auth-full` derivation known-answer test with fixed `SERVER_SECRET`, `ACCESS_KEY` and `src` verifies exact `authFullSaltMaterial`, 32-byte `authFullSalt`, `authFullInfo` and 32-byte `fullAuthKey` under section 7.2.
5. Changing one byte of `SERVER_SECRET` or `ACCESS_KEY` must change `fullAuthKey` and the resulting tag for an otherwise identical request. A tag computed knowing only one secret must not verify.
6. An `auth-full` known-answer test constructs `canonical` in order domain string, `POST`, `data`, `src`, exact timestamp, `requestId`, `SHA256HEX(body)` and computes the tag exclusively as `HMAC-SHA-256(fullAuthKey, canonical)`; changing timestamp or a single body byte invalidates it.
7. The server accepts UTC skews exactly `-60.000`, `0.000` and `+60.000` s and rejects `-60.001` and `+60.001` s; it compares at millisecond precision and creates no timestamp or nonce record after processing.
8. Authentication `tag` in `auth-semi` and `auth-full` must be unpadded base64url decoding to exactly 32 bytes; comparison is constant-time. `auth-none` MUST NOT contain `tag`.
9. `auth-full.src` selects exactly one `ACCESS_KEY` and enters both derivation and canonicalization; changing it changes `fullAuthKey` and tag. Unknown `src` produces generic `401` after dummy-key derivation, and every data record must have the same `src`.
10. The recipient discards a line whose `dst` is not its configured VPN address before selecting `PAYLOAD_KEY`; during operational processing it unambiguously selects exactly one fixed `PAYLOAD_KEY` for every remaining ordered `src`, `dst` pair. The relay does not have this map.
11. Changing `messageId`, routing `src` or `dst`, IV, ciphertext or tag invalidates GCM verification because routing fields remain in AAD.
12. `health` IV is 12 bytes and GCM tag 16 bytes. `info` salt is 32 bytes, IV 12 and GCM tag 16; two responses have fresh random values.
13. The client neither parses `info` plaintext nor delivers payload before successful GCM verification.
14. During normal bootstrap, recovery and operational polling, each processed data line is independently verified with AES-GCM. Invalid-tag lines are discarded without affecting other valid lines.
15. A zero-byte configured `SERVER_SECRET` is normalized at server and client startup to exact `CHANGE_THIS_SERVER_SECRET`. Known-answer tests for empty and explicitly configured `"CHANGE_THIS_SERVER_SECRET"` must produce identical `semiKey`, `info` response key, `authFullSalt`, `fullAuthKey` and tags without `ValueError`. Nonempty `"0"`, 32 and 64 ASCII `0` characters and 32 zero bytes `0x00` are not normalized and must produce results for their own exact bytes.
16. PHP server and client use pure-PHP software AES-256-GCM regardless of OpenSSL, Sodium or other extension availability. Tests with available and unavailable extensions must take the same software path and produce exactly identical outputs for `health`, `info` and end-to-end payload.
17. Fixed key, IV, AAD and plaintext produce exactly the expected ciphertext and 16-byte tag from an AES-256-GCM known-answer vector. The software correctly decrypts a valid vector, rejects a one-bit change to key, IV, AAD, ciphertext or tag, and exposes no plaintext before successful tag verification.
18. A `data-mac` known-answer test constructs `canonical` from domain string, action, `requestId`, decimal record count, ordered `messageId`/exact base64url GCM-tag pairs and `SHA256HEX` of exact `data-result` bytes without LF. The result is exactly 32-byte `HMAC-SHA-256(fullAuthKey, canonical)` whose `tag` is unpadded base64url decoding to exactly 32 bytes.
19. Adding, removing or reordering a data line, changing its `messageId` or GCM tag, record count or a single `data-result` byte invalidates `data-mac`. During operational processing, subsequent AES-GCM detects a changed other data-line field with preserved GCM tag. Section 9.5 diagnostics do not prove payload integrity. Missing, changed or invalid `data-mac` must cause neither outbound removal nor payload delivery.
20. A `health` known-answer test with fixed effective `SERVER_SECRET`, `requestId`, IV and public fields verifies exact `healthSaltMaterial`, `healthSalt`, `healthInfo`, `healthKey`, AAD and the 16-byte GMAC tag over empty plaintext under section 7.3.
21. Changing one character of `serverVersion`, `status`, `description`, `timestamp`, `requestId` or base64url IV invalidates the `health` tag. Empty, shortened or extended tags and tags calculated with another `SERVER_SECRET` are rejected and must not change client time offset.
22. A valid-tag response for an old or different `requestId` is rejected. Replay of the same `auth-none` may create multiple valid server responses with the same `requestId`, but the client accepts at most a response for its current attempt, and the server creates no persistent state.

### 16.4 Queues, concurrency and crashes

1. Atomic publication survives failure before and after `.receiving` → `.data` rename without a partial batch.
2. Two workers sending to the same destination neither lose nor merge a batch.
3. A response sequentially claims found `.data` files, returns them as direct `data` lines without pagination or envelope and deletes them after delivery; their count may exceed 16.
4. Data lines are followed by exactly one `data-result` with the exact closed result-field set and exactly one final `data-mac`; an empty buffer returns these two lines.
5. Worker failure or connection interruption after `.data` → `.sending` claiming may lose the batch; the next poll does not reissue it.
6. Batches written concurrently after the initial `.data` scan remain queued for the next poll and must not be lost.
7. One full destination queue does not cancel already accepted entries for another destination, and the result is exact.
8. At the first capacity shortage for a destination, the server runs `sweepQueue(dst, now)` exactly once and retries entire-group publication exactly once.
9. If the destination group still does not fit after sweeping, none of its entries is published, every `messageId` is deferred and the response status is `200`.
10. A request with all outbound groups deferred still sequentially delivers found batches from the caller's incoming buffer with `data-result` and `data-mac`; it does not return `429`.
11. Each request performs at most one capacity-triggered sweep and at most two publication attempts per unique `dst`.
12. The 16 MiB limit counts only unexpired, not-yet-delivered buffered records; physically present expired records do not reduce capacity.
13. An expired batch is not delivered; sweeping deletes it in `.receiving`, `.data` and `.sending` states.
14. Storage locking ends after 200 ms with a controlled error, not a blocked worker.
15. The same contract-test suite runs against every storage driver.
16. A disk batch name contains exactly one UTC time: expiry in filename-safe `YYYY-MM-DDTHH-MM-SS.sssZ` format with exactly three millisecond digits.
17. Default `BUFFER_DIRECTORY` resolves to `./phpvpn_data` relative to `phpvpn.php`; validated `10.99.99.2` maps to `10_99_99_2`. The destination directory is created on first queue read or write, and an authenticated source's directory is created best effort for `info.json`.

### 16.5 Client statistics

1. The `disk` driver has no central statistics file; each client directory may contain exactly one `info.json` marker.
2. An implementation-created marker is a zero-byte empty file. Its contents are neither read nor parsed as JSON, and `lastSeen` derives exclusively from `mtime`.
3. An eligible update creates a missing marker as an empty file in one attempt. An existing marker receives only an `mtime` change in one attempt.
4. An `mtime` exactly 30 seconds old or in the future triggers no update; one 31 seconds old is updated to current time.
5. Failure to read `mtime`, create or update a marker changes neither HTTP status, `data-result`, corresponding `data-mac`, publication nor `drainAll`; in particular, it causes neither `500` nor `503`.
6. Two concurrent workers use neither a lock nor retry; concurrent updates of one marker are allowed and different clients' markers do not affect each other.
7. `info` reads only configured clients' marker `mtime`, converts valid values to UTC RFC 3339 and omits clients with missing/unreadable markers without internal paths or diagnostics.
8. A marker does not count toward queue capacity, is not delivered as `data` and survives targeted and opportunistic sweeps. Direct HTTP and HTTPS requests for `info.json` and other storage-root files are prevented.

### 16.6 Limited replay protection

1. Sending the same valid request twice inside the 60-second window produces two processing operations.
2. At age 61 s the server rejects that request with generic `401`; the timestamp is not persisted and no nonce or replay database is created.
3. During operational processing, delivering the same valid response twice re-verifies `data-mac` and each GCM tag, and delivers payload twice.
4. Repeated `messageId`, IV and authentication HMAC tags are not compared with history.
5. Replay within the window can fill a queue but, without a key, cannot change protected contents or create a new valid tag.
6. After testing, the source tree and runtime storage contain no directory, table or file intended for nonce/replay caching.
7. An attacker's copy of valid empty HTTP polling sent within the 60-second window successfully performs `drainAll` on the victim queue and obtains encrypted records; subsequent legitimate polling no longer obtains them. Repeating with newly captured polls can continuously empty the queue. The server neither detects the attack, restores records nor creates replay state.
8. A higher layer may create new packets and retry after loss, but tests must not assume retransmission for every traffic type or guarantee delivery of another attempt during a continuing attack.
9. Replaying an earlier `health` against a new `auth-none` fails `requestId` matching. Delaying the current valid response can cause only older verified time or unavailability; without `SERVER_SECRET`, a valid tag with future timestamp cannot be created.

### 16.7 One-shot diagnostics and timeout

1. A valid diagnostic response with multiple data records and valid `data-mac` may succeed even with unverifiable payload GCM. No payload is decrypted, delivered, stored or reflected in operational state.
2. Ciphertext modification preserving canonical format and the original GCM tag is not proof of content integrity for this diagnostic. The same damaged payload is rejected by GCM during normal bootstrap, recovery and operation.
3. Wrong HTTP status, NDJSON, correlation, routing, limit, nonzero empty-request result or final `data-mac` invalidates diagnostics. A valid MAC alone does not excuse other errors.
4. One requested check sends at most one empty `data` request. `401`, `503`, transport failure and lost response end the attempt without retry. A new requested authenticated check begins with newly verified `health`.
5. Possible irreversible removal of messages is disclosed in advance. The server does not restore them after a lost response, and another requested attempt may remove further messages.
6. Slow DNS, TLS, headers, incrementally arriving blocks and an unterminated body respect a single total deadline. Boundary conversions, timeout setup failure and timeout/cancel/success races must not return late success or release a context still in use.
7. The default server authenticates all fourteen addresses `.2` through `.15` with their exact default keys. Two computers with the same address and matching secrets are not rejected for duplicate IP.

## 17. Conformance checklist

An implementation is compatible with phpvpn v1 only if it:

- supports all four endpoints and exact message types;
- accepts `health` only through `POST` with unauthenticated `auth-none`, returns the original `requestId`, IV and GCM tag over all operational fields, and the client uses server time only after verifying the tag with effective `SERVER_SECRET`;
- at server and client startup normalizes only exactly empty configured `SERVER_SECRET` to `CHANGE_THIS_SERVER_SECRET` and uses this effective value for every cryptographic operation;
- if written in PHP, always performs every AES-256-GCM operation through a single bundled pure-PHP software implementation and uses neither OpenSSL, Sodium nor another cryptographic extension for it;
- accepts only canonical unpadded base64url, checks zero unused bits by re-encoding or equivalent validation and never uses noncanonical text in AAD or another canonical string;
- uses normative canonicalization, separated keys and AES-GCM lengths;
- authenticates before any storage modification;
- requires exact UTC string `YYYY-MM-DDTHH:MM:SS.sssZ` in both `auth-semi` and `auth-full`, includes it in normative order in the HMAC `tag` and rejects absolute skew above 60 s without timestamp persistence;
- derives the `auth-full` HMAC key by normatively combining the single `SERVER_SECRET`, the single `ACCESS_KEY` selected by `src` and `src` itself, so a correct tag requires both secrets;
- returns found published batches directly as `data` without envelope or pagination, followed by one `data-result` and one final `data-mac` authenticating the ordered `messageId`/GCM-tag list and exact `data-result`;
- uses atomic at-most-once per-batch claiming without acknowledgement, preserves TTL and exact partial send results;
- on capacity shortage performs at most one targeted sweep per `dst`, retries entire-group publication once and represents continuing shortage solely through `deferredMessageIds` in `200`, never `429`;
- maintains best-effort `lastSeen` as the `mtime` of an empty per-client `info.json` marker, limits updates to values older than 30 seconds and never changes the data result because of statistics;
- prevents direct web access to buffers, statistics and other storage-root files;
- behaves identically over HTTP, HTTPS and proxies;
- implements no server or client replay deduplication beyond timestamp age checking and persists neither timestamp nor nonce;
- explicitly preserves the accepted risk that replaying intercepted HTTP polling within the valid window can repeatedly drain and permanently destroy the victim's incoming queue;
- maintains one fixed payload key for each ordered `src`, `dst` pair outside the relay server;
- preserves full payload GCM and ordinary valid-data processing during normal bootstrap, recovery and operational polling. Only one-shot diagnostics under 9.5 may discard all payloads without GCM and without using them after completely verifying the response.
- respects published active limits regardless of storage driver;
- reveals neither secrets nor internal paths in responses or logs.

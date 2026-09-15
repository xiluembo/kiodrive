<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0003: Placeholder identity

## Status

Accepted

## Context

Every Cloud Files placeholder can carry a `FileIdentity` blob (max 4 KB) that the
platform returns on callbacks. Remote paths change on rename; URLs are long;
credentials must never appear in identity blobs. KIO does not provide a stable
cross-session cloud primary key for all protocols (`UDS_INODE` is optional and
not an ETag).

KIODrive needs a compact, versioned identity that survives remote renames and
maps quickly to SQLite rows during `FETCH_DATA` / notify callbacks.

## Decision

`FileIdentity` is a **compact binary structure**, not a remote path string:

```text
v1: version:u8 | flags:u8 | mountUuid:16 | itemUuid:16 | revisionSeq:u64
```

Approximate size: ~42 bytes. Rich metadata (remote path, URL, MIME, ETag-like
tokens, Windows display name) lives in SQLite. Remote rename preserves
`itemUuid`. `revisionSeq` increments when the authoritative revision token /
mtime / size changes; the full token is stored in `revision_token`.

## Consequences

### Positive

- Fits far under the 4 KB limit with room for future flags.
- Callbacks resolve `itemUuid` → mount → remote URL without parsing paths.
- Renames do not invalidate identity.

### Negative

- Requires a durable metadata store (see ADR 0004).
- Corrupt or missing DB rows cannot be recovered from `FileIdentity` alone.
- Version bumps need careful reader compatibility.

### Follow-ups

- Define endianness and UUID encoding in the implementation header.
- Never place secrets or full URLs in `FileIdentity`.

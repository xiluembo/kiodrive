<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0009: Remote refresh

## Status

Accepted

## Context

Most KIO protocols do not offer reliable push notifications or server-side
watches on Windows. kio-fuse refreshes attributes/children with TTLs via
`stat` / `listDir`. KIODrive must keep placeholders coherent enough for Explorer
without assuming SMB-style change journals on every backend.

## Decision

Remote coherence is **pull-based**:

- Directory listings and attributes use **TTL caches** (`directory_ttls` /
  item metadata), refreshed by `KIO::listDir` / `KIO::stat` when TTLs expire or
  when the user/provider explicitly refreshes.
- `RemoteReconciler` compares remote listings to SQLite items, then creates /
  updates / removes placeholders and records conflicts.
- Do **not** assume native remote watches for SFTP/FTP/WebDAV in the core design.
- Protocol-specific push (if ever available) may augment, not replace, TTL
  reconciliation.
- Offline mounts leave dehydrated opens failing with mapped errors; already
  hydrated files remain usable; reconnect runs reconciliation.

## Consequences

### Positive

- Matches real KIO protocol capabilities on Windows.
- Reuses proven kio-fuse refresh ideas without FUSE.
- Predictable load control via TTLs and explicit refresh.

### Negative

- Remote changes appear only after TTL/reconcile (eventual consistency).
- Aggressive TTLs increase worker load; lax TTLs show stale trees.
- Delete/rename races need journal + conflict policy.

### Follow-ups

- Tune default TTLs per protocol after latency spikes.
- Surface manual “Refresh” in GUI/tray.

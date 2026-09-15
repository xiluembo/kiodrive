<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0004: SQLite metadata store

## Status

Accepted

## Context

KIODrive must persist mounts, placeholder item maps, name virtualization,
operation journals, conflicts, and transfer state across process restarts.
Credentials must not live in this database. The store must support crash
recovery: an incomplete journal entry is not remote success.

CFAPI identity blobs are intentionally tiny (ADR 0003), so nearly all metadata
needs an application database.

## Decision

Use **SQLite in WAL mode** with versioned migrations as the sole metadata store
for SyncEngine. Initial tables:

- `schema_migrations`
- `mounts` — uuid, name, local_relpath, remote_url_canonical, protocol, mode,
  state, caps_json, ttl, timestamps
- `items` — uuid, mount_id, parent_id, remote_path, remote_name, windows_name,
  kind, mime, size, mtimes, remote_inode, revision_token, revision_seq,
  sync_state, pin, hydrate, metadata_version
- `name_mappings` — item_id, remote_name, windows_name, reason
- `operations` — journal (type, state, payload, idempotency_key)
- `conflicts`
- `transfers`
- `directory_ttls`

Access is owned by `KIODrive.SyncEngine`. Mount create/activate uses
`BEGIN IMMEDIATE` → validate → reserve → auth probe → persist → placeholders →
activate, with rollback on failure.

## Consequences

### Positive

- Durable, queryable mapping from local paths / UUIDs to remote resources.
- Idempotent journals support rename/delete/upload recovery.
- No credential leakage into the DB schema by design.

### Negative

- Schema evolution requires migration discipline.
- DB corruption is a high-severity failure mode (backup/repair strategy later).
- Concurrent writers must be serialized through the engine.

### Follow-ups

- Startup recovery walks incomplete `operations` without assuming remote commit.
- Keep credentials exclusively in the credential store (ADR 0005).

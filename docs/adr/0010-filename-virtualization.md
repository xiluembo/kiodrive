<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0010: Filename virtualization

## Status

Accepted

## Context

Remote KIO names may be illegal or ambiguous on Win32: reserved device names
(`CON`, `PRN`, …), characters (`:*?"<>|`), trailing dots/spaces, case-only
collisions on case-insensitive volumes, NFC/NFD differences, and long paths.
Two remote entries must never collapse to one local name. Users still need a
reversible mapping back to the remote name for sync and UI.

## Decision

Introduce a persistent **`NameMapper`**:

- Compute a Windows-safe `windows_name` per item
- Store mappings in `name_mappings` with a machine-readable `reason`
  (reserved / illegal / case / nfc / long / …)
- Ensure uniqueness within each directory of a mount
- Keep `remote_name` authoritative for KIO URLs
- Apply mapping consistently for placeholder creation and PathRouter lookups

## Consequences

### Positive

- Prevents create/hydrate failures on illegal names.
- Preserves remote fidelity in metadata and uploads/renames.
- Auditable reasons help support and UI affordances.

### Negative

- Local names may differ from remote names (must be visible in UI).
- Case-folding policies interact with volume case sensitivity.
- Long-path strategies must stay coherent with CFAPI/Explorer limits.

### Follow-ups

- Full mapper matrix tests (reserved, illegal, case, Unicode, long).
- Optional “Show remote name” in details/status UI later.

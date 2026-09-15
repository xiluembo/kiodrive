<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0011: Symlink semantics

## Status

Accepted

## Context

Some KIO protocols (notably SFTP) can report symbolic links. Windows Cloud Files
placeholders already use reparse points. Creating nested Windows symlinks /
reparse points for remote links risks incorrect traversal, security surprises,
and apps that reject reparse points. POSIX permission bits also do not map
cleanly to NTFS ACLs.

## Decision

For the first product slices:

- Project remote symlinks as **ordinary files or directories** with **link
  metadata** stored in SQLite / item flags (target string, link kind).
- **Do not** create nested Windows symlink reparse points for remote links until
  a dedicated spike proves a safe model.
- Surface POSIX mode/owner/executable bits as metadata/UI when available — **not**
  as fabricated NTFS ACLs.
- Reject special remote nodes (FIFO, device nodes, etc.) with a mapped error.

Document the behavior for users so “symlink” does not imply Win32 symlink
semantics.

## Consequences

### Positive

- Avoids dangerous reparse nesting with Cloud Files.
- Predictable Explorer behavior in slice 1.
- Leaves room for a future explicit “materialize as Windows symlink” mode.

### Negative

- Remote symlink-aware tools may not see real Win32 links.
- Opening a link may require KIO-side follow policies that differ from shell
  expectations.
- Spike still required before any reparse-based enhancement.

### Follow-ups

- Spike: follow vs no-follow on hydrate; interaction with PathRouter.
- UX copy explaining projected link metadata.

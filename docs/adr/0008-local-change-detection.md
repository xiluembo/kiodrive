<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0008: Local change detection

## Status

Accepted

## Context

CFAPI does not notify providers when users create or edit files under the sync
root. Without a watcher, local edits never upload and new local files never
become synced items. Provider-originated placeholder updates must not be
misinterpreted as user changes (feedback loops).

## Decision

Implement a SyncEngine **`LocalChangeMonitor`**:

- **Slice 1:** `ReadDirectoryChangesW` on the sync root / mount subtrees
- **Later:** optionally USN Journal for scale and reliability
- Suppress or correlate events caused by `CfCreatePlaceholders`,
  `CfUpdatePlaceholder`, hydration, and dehydration
- Feed create/edit/rename/delete into the operation journal and upload pipeline
- Never dehydrate items in `OutOfSync`, `Uploading`, or `Conflict` states

## Consequences

### Positive

- Closes a confirmed CFAPI gap.
- Incremental path: directory changes first, USN later.
- Journal integration supports crash-safe uploads.

### Negative

- `ReadDirectoryChangesW` buffer overflows and missed events need hardening.
- Feedback-loop bugs cause sync storms or data loss risk.
- USN adds complexity and privilege/volume considerations later.

### Follow-ups

- Integration tests for edit → upload and provider-suppressed events.
- Explicit handling of Explorer unpin/dehydrate attribute changes (related to
  Free up space behavior).

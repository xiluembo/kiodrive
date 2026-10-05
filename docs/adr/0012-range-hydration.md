<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0012: Range hydration

## Status

Accepted — with a documented residual on synchronous local-read cancellation
(see Follow-ups).

## Context

CFAPI `FETCH_DATA` supplies byte ranges (typically 4 KB aligned). The hydration
policy `CF_HYDRATION_POLICY_PARTIAL` is **not supported**; progressive/full
policies still allow ranged `TRANSFER_DATA`.

On the KIO side, random-access I/O exists only when a protocol supports
`KIO::open` / `FileJob` (SFTP does; FTP metadata shows no `opening`). kio-fuse
uses FileJob for `file`/`sftp`/`smb` and full-file cache otherwise. Pretending
progressive ranged hydration on a sequential-only protocol can force full
downloads while still reporting misleading progress.

Cancellation is correlated by CFAPI `TransferKey`, not by path: one file may
have concurrent ranged `FETCH_DATA` requests. A global
`cancelTransfers()` / `HydrationManager::cancel(QUuid())` would abort unrelated
ranges and list jobs.

## Decision

- Prefer **progressive/full** hydration policies compatible with CFAPI (not
  PARTIAL).
- For protocols with **`supportsOpening` / FileJob` (SFTP first):** map CFAPI
  ranges to `FileJob` seek/read and `TRANSFER_DATA`.
- For **sequential protocols (FTP, and WebDAV/HTTP until proven):** hydrate via
  sequential `KIO::get` (full or resume metadata if applicable). Do **not** claim
  efficient random ranged progressive hydration if the implementation must
  download the entire object anyway.
- Report progress with `CfReportProviderProgress`.
- Honor `CANCEL_FETCH_DATA` by:
  1. Marking the pending fetch cancelled under its `TransferKey`-derived
     `requestKey` in `PendingFetchRegistry`.
  2. Resolving that entry's `operationId` and calling
     `HydrationManager::cancel(operationId)`, which kills **only** the active
     KIO get job when it belongs to that operation — never a global cancel of
     list jobs or unrelated gets.
  3. Completing the CFAPI request with `STATUS_CLOUD_FILE_REQUEST_CANCELLED`
     and the full requested range length.
- Keep network work off the callback thread; respect the ~60 s callback budget
  via asynchronous completion patterns required by CFAPI.
- Treat short reads (`ok` with fewer bytes than `requiredLength`) as hydration
  failure; do **not** mark `ItemSyncState::InSync` until the exact range is
  obtained.

## Consequences

### Positive

- Matches confirmed CFAPI and KIO capabilities.
- SFTP can hydrate large files without always reading from offset 0.
- Avoids lying about partial efficiency on sequential backends.
- Concurrent ranged fetches for one path are not cancelled by each other.

### Negative

- Protocol-specific hydration paths increase complexity.
- Sequential protocols may hydrate more data than the immediate range needs.
- Cancellation and worker-kill behavior need careful testing.
- Synchronous `file://` `QFile::read` paths still finish the local read before
  the cancel response; only in-flight KIO get jobs for the matching
  `operationId` are aborted mid-transfer (via `getSync`'s nested event loop).

### Follow-ups

- Spike: SFTP FileJob ranged hydrate under Explorer open.
- Revisit WebDAV after Range/`UDS_XML_PROPERTIES` prototypes.
- Move hydration fully off blocking `getSync` / `QFile::read` so
  `CANCEL_FETCH_DATA` always aborts I/O mid-transfer for every backend path,
  still keyed only by `operationId` (no global cancel).
- Windows integration test: overlapping ranges, cancel one TransferKey only,
  short-read failure reporting the full required length.

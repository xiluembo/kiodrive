<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0012: Range hydration

## Status

Accepted

## Context

CFAPI `FETCH_DATA` supplies byte ranges (typically 4 KB aligned). The hydration
policy `CF_HYDRATION_POLICY_PARTIAL` is **not supported**; progressive/full
policies still allow ranged `TRANSFER_DATA`.

On the KIO side, random-access I/O exists only when a protocol supports
`KIO::open` / `FileJob` (SFTP does; FTP metadata shows no `opening`). kio-fuse
uses FileJob for `file`/`sftp`/`smb` and full-file cache otherwise. Pretending
progressive ranged hydration on a sequential-only protocol can force full
downloads while still reporting misleading progress.

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
- Honor `CANCEL_FETCH_DATA` by aborting/killing the corresponding KIO job.
- Keep network work off the callback thread; respect the ~60 s callback budget
  via asynchronous completion patterns required by CFAPI.

## Consequences

### Positive

- Matches confirmed CFAPI and KIO capabilities.
- SFTP can hydrate large files without always reading from offset 0.
- Avoids lying about partial efficiency on sequential backends.

### Negative

- Protocol-specific hydration paths increase complexity.
- Sequential protocols may hydrate more data than the immediate range needs.
- Cancellation and worker-kill behavior need careful testing.

### Follow-ups

- Spike: SFTP FileJob ranged hydrate under Explorer open.
- Revisit WebDAV after Range/`UDS_XML_PROPERTIES` prototypes.

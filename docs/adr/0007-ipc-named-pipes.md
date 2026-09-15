<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0007: IPC via Windows named pipes

## Status

Accepted

## Context

`KIODrive.Gui` and `KIODrive.Shell` must command and query `KIODrive.SyncEngine`
without D-Bus (unavailable/unwanted on our Windows KF6 stack). Requirements:

- Local-only, versioned messages
- Works unpackaged and under MSIX
- Suitable for tray status, mount CRUD, conflict prompts, and Shell proxies

COM remains necessary for Explorer-facing Shell contracts, but is a poor fit as
the primary Gui↔Engine control channel.

## Decision

Use **Windows named pipes** with **versioned messages** (JSON or CBOR) between:

- Gui ↔ SyncEngine
- Shell ↔ SyncEngine (thin proxy for status/verbs that need engine state)

Do **not** use D-Bus for KIODrive IPC. Reserve COM for Shell handler interfaces
required by the platform.

## Consequences

### Positive

- Native Windows IPC without Plasma session dependencies.
- Explicit protocol versioning eases upgrades.
- Shell stays thin: COM in, pipe calls out.

### Negative

- Custom protocol and authz (pipe ACL / user session binding) to design.
- Must handle engine-not-running and reconnect UX.
- CBOR vs JSON choice affects debuggability vs size (implementation detail).

### Follow-ups

- Document message schema under `src/common` once implemented.
- Enforce same-user ACLs on pipe endpoints.

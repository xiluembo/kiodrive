<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0002: WinRT sync-root registration

## Status

Accepted

## Context

Cloud Files providers must register a sync root for Shell integration (navigation
pane, display name, icons, policies) and connect the Cloud Filter API for
callbacks. Two APIs exist:

- Win32 `CfRegisterSyncRoot`
- WinRT `StorageProviderSyncRootManager.Register`

Microsoft documentation and FAQ wording disagree (“use one, not both” vs
examples that still mention `CfRegisterSyncRoot`). The official CloudMirror
sample registers with **WinRT only**, then calls `CfConnectSyncRoot`, and does
**not** call `CfRegisterSyncRoot`. Using only `CfRegisterSyncRoot` often yields
broken Shell UX (missing nav pane / grey-X symptoms reported in community Q&A).

Packaged apps also need MSIX `desktop3:CloudFiles` for correct lifecycle cleanup.

## Decision

KIODrive’s primary registration path is:

1. `StorageProviderSyncRootManager.Register(StorageProviderSyncRootInfo)`
2. `CfConnectSyncRoot` for callbacks
3. Explicit `CfRegisterSyncRoot` **only if** a spike on the target OS shows that
   WinRT registration does not cover required CFAPI policies

Sync root Id follows `[StorageProviderID]![SID]![AccountID]`. Development may
start unpackaged; production packaging must declare `windows.cloudFiles`.

## Consequences

### Positive

- Aligns with the current official sample and Shell-facing APIs.
- Keeps navigation pane and provider identity correct.
- Leaves an escape hatch for policy gaps discovered in testing.

### Negative

- Dual-API ambiguity remains until the spike closes.
- Unpackaged vs MSIX behavior differs; tests must cover both eventually.
- Missing MSIX cloudFiles declaration risks orphan sync roots on uninstall.

### Follow-ups

- Spike: WinRT-only vs WinRT + `CfRegisterSyncRoot` (ADR-linked in research.md).

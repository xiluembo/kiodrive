<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0006: Process architecture

## Status

Accepted

## Context

A Cloud Files provider must keep `CfConnectSyncRoot` callbacks alive while
offering a GUI and optional Shell handlers. Loading Qt/KF6 inside Explorer is
unsafe. CloudMirror’s single-EXE COM hosting is a sample shortcut; Microsoft
guidance prefers separating Shell COM servers from the sync engine.

KIO workers already spawn as separate `kioworker.exe` processes.

## Decision

KIODrive ships **three first-party processes**:

| Process | Role |
|---------|------|
| `KIODrive.SyncEngine` | Long-lived; CFAPI connect/callbacks; KIO backend; SQLite; credentials; sync |
| `KIODrive.Gui` | Qt Widgets tray/settings/mount wizard; talks to the engine over IPC |
| `KIODrive.Shell` | Lightweight COM local server for Shell handlers; **no Qt/KF6** |

GUI crashes must not tear down CFAPI callbacks. Shell code must not load
Qt/KDE libraries. KIO worker processes remain children/peers of the engine via
the KF6 scheduler.

## Consequences

### Positive

- Isolates Explorer from KF6/Qt.
- Improves reliability of hydration callbacks.
- Clear security boundary for Shell surfaces.

### Negative

- More packaging, auto-start, and IPC complexity.
- Shell features (thumbnails, custom state) arrive after the SFTP core slice.
- Debugging spans multiple processes.

### Follow-ups

- Engine auto-start strategy for MSIX (milestone packaging).
- Keep COM out-of-proc only for Shell; prefer named pipes for Gui↔Engine (ADR 0007).

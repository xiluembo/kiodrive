<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0001: Single sync root with logical mounts

## Status

Accepted

## Context

Windows Cloud Files forbids overlapping sync-root trees (ancestor/descendant).
KIODrive must expose multiple remote KIO locations (initially SFTP mounts) in
Explorer without colliding with OneDrive or with itself. Registering one CFAPI
sync root per mount would either force sibling roots under carefully disjoint
paths or risk overlap errors as users rearrange folders.

Cloud Files treats every directory under a connected sync root as a normal
placeholder subtree. “Mount” is therefore an application concept, not a platform
object.

## Decision

KIODrive registers **one** Cloud Files sync root, conventionally under:

```text
%USERPROFILE%\KIO
```

Each user mount is a **logical mount**: a first-level (or registry-defined)
subdirectory of that root, with its own remote URL, credentials handle,
capabilities, and metadata rows. Path routing (`PathRouter`), conflict detection,
and `FileIdentity.mountUuid` bind local paths to mounts.

Nested CFAPI sync roots are not used. Volume roots are not registered.

## Consequences

### Positive

- Avoids platform overlap failures between KIODrive mounts.
- Simplifies connect/disconnect and Shell navigation to a single provider root.
- Matches the confirmed model of placeholders-as-subtrees.

### Negative

- Pin / “Free up space” / hydration policies apply at root level unless the app
  carefully scopes behavior per subtree.
- A single root failure or disconnect affects all mounts until recovered.
- Users may expect drive-letter or per-mount roots; UX must explain the layout.

### Follow-ups

- Validate Explorer UX with many logical mounts (see research.md).
- Keep mount conflict checks for local path ancestry and remote URL overlap.

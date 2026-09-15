<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# ADR 0005: Credential storage

## Status

Accepted

## Context

On Windows Craft KF6, KIO cannot rely on D-Bus `kpasswdserver` or KWallet.
`openPasswordDialog()` fails with `ERR_PASSWD_SERVER`. KIODrive must own
authentication for SFTP (and later WebDAV/FTP): passwords, SSH keys/passphrases,
and host-key trust.

Secrets must not appear in SQLite, config files, logs, persisted URLs, or
`FileIdentity`.

## Decision

Implement a SyncEngine **`CredentialBroker`** that:

- Stores secrets in **Windows Credential Manager** and/or **DPAPI**-protected
  blobs keyed by mount UUID / principal.
- Supports anonymous, user/password, SSH private key + passphrase, and host-key
  records.
- Pre-authenticates on mount create/reconnect from the GUI.
- Injects `KIO::AuthInfo` / ephemeral URL userinfo and job metadata
  (`no-auth-prompt`) into KIO jobs.
- On unexpected auth challenges during CFAPI work: fail fast, set mount state
  `AuthenticationRequired`, notify GUI/tray — do not block Explorer for the
  full callback timeout waiting for a missing dialog server.
- Handles SFTP host keys via KIODrive UI + persisted trust, rather than opaque
  worker-only prompts whenever possible.

## Consequences

### Positive

- Matches confirmed Windows KIO limitations.
- Uses OS-native secret storage familiar to Windows users and IT policy.
- Keeps CFAPI callbacks non-interactive.

### Negative

- Must spike AuthInfo injection and host-key `messageBox` interception.
- Credential Manager ACLs / package identity differ unpackaged vs MSIX.
- Key file paths still need careful permission handling on disk.

### Follow-ups

- Mandatory Craft Windows SFTP auth spike before declaring auth “done”.
- Never log secret material; redact URLs in diagnostics.

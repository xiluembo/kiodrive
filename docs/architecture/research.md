<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# KIODrive architecture research

This document records research findings for **KIODrive**, a Windows Cloud Files
provider that projects KDE Frameworks 6 (KF6) KIO protocols as native Explorer
placeholders. Findings are classified as:

| Tag | Meaning |
|-----|---------|
| **Confirmed** | Documented officially, observed in Craft at `D:\Craft`, or demonstrated in an official sample |
| **Assumption** | Reasonable inference; not fully proven on the target OS/build |
| **Prototype needed** | Must be validated with a short spike before treating as settled |
| **Unsupported** | Documented limitation, missing component, or out of scope for the core product |

Product license: **LGPL-3.0-or-later**. Development docs use **CC0-1.0** SPDX
headers where noted.

---

## 1. Windows Cloud Files API (CFAPI)

### Confirmed

- Canonical docs live under the Win32 Cloud Files portal and Cloud Filter API
  reference (`cfapi.h` / `cldflt.sys`). Introduced in Windows 10 1709; sync engines
  are desktop apps, not pure UWP.
- Two complementary layers:
  - **Win32 Cloud Filter API** — placeholders, connect, callbacks, transfer/ack ops
  - **WinRT `Windows.Storage.Provider`** — Shell-facing sync-root registration
- Sync roots require **local NTFS**. Network/SMB paths as sync roots fail.
- **Volume roots** (e.g. `D:\`) cannot be sync roots.
- **Ancestor/descendant overlap** of sync-root trees is rejected by the platform.
- Callback surface includes `FETCH_PLACEHOLDERS`, `FETCH_DATA`,
  `CANCEL_FETCH_*`, and `NOTIFY_*` for dehydrate/delete/rename
  (open/close notifications are optional and non-blocking for open/close).
  `VALIDATE_DATA` is part of the platform surface but is **not registered**
  under the current `CF_HYDRATION_POLICY_FULL` (no validation-required
  modifier); see [cfapi-callback-audit.md](cfapi-callback-audit.md).
- There is **no** `CF_CALLBACK_TYPE_ACKNOWLEDGE`. Acknowledgements are
  `CfExecute` operations (`ACK_DATA`, `ACK_DEHYDRATE`, `ACK_DELETE`, `ACK_RENAME`).
- Callback work has a practical **~60 second** timeout (not configurable).
  Network I/O must be asynchronous relative to the callback thread.
- **`CF_HYDRATION_POLICY_PARTIAL` is not supported.** Byte-range transfer via
  `TRANSFER_DATA` / `FETCH_DATA` (typically **4 KB** aligned except EOF) is
  supported under progressive/full hydration policies.
- `FileIdentity` is an **opaque blob**, max **4 KB**
  (`CF_PLACEHOLDER_MAX_FILE_IDENTITY_LENGTH`). Required for files; optional for
  directories. Returned on subsequent callbacks.
- **No CFAPI callback** for local file/folder creation or content edits.
  Providers must use `ReadDirectoryChangesW`, USN Journal, or equivalent.
- Explorer **“Free up space”** sets **UNPINNED**; it does **not** reliably fire
  `NOTIFY_DEHYDRATE`. Providers observe pin attributes and dehydrate via
  `CfUpdatePlaceholder` (preferred over the poorly documented
  `CfDehydratePlaceholder` path).
- Pin state (`CfSetPinState`) is asynchronous intent, not an immediate
  hydrate/dehydrate guarantee.
- Feature gating: `CfGetPlatformInfo()` / integration numbers for newer APIs.

### Assumption

- WinRT `StorageProviderSyncRootManager.Register` plus `CfConnectSyncRoot` is
  sufficient for unpackaged development (CloudMirror pattern). MSIX
  `desktop3:CloudFiles` is still required for correct packaged lifecycle and
  Shell handler registration.
- Subfolders under a single sync root can act as app-defined “logical mounts”
  without additional platform mount APIs.

### Prototype needed

- Whether WinRT `Register` alone covers all hydration/population/in-sync
  policies KIODrive needs, or whether an explicit `CfRegisterSyncRoot` is still
  required on the target Windows build.
- End-to-end dehydrate after Explorer unpin; cancel/hydrate under slow KIO.
- MSIX install/uninstall/reinstall cleanup of sync roots when
  `windows.cloudFiles` is declared correctly.

### Unsupported

- Nested sync roots (ancestor/descendant).
- Using FUSE or Dokan as the primary filesystem integration layer for KIODrive.
- Treating CFAPI as a complete sync engine (conflicts, credentials, remote
  revisioning remain application concerns).

### References

- https://learn.microsoft.com/en-us/windows/win32/cfapi/cloud-files-api-portal
- https://learn.microsoft.com/en-us/windows/win32/cfapi/build-a-cloud-file-sync-engine
- https://learn.microsoft.com/en-us/windows/win32/api/_cloudapi/

---

## 2. WinRT `StorageProviderSyncRootManager` and CloudMirror discrepancies

### Confirmed

- Modern Shell registration uses
  `Windows.Storage.Provider.StorageProviderSyncRootManager.Register` /
  `Unregister` with `StorageProviderSyncRootInfo`.
- Sync root `Id` format: `[StorageProviderID]![Windows SID]![AccountID]`
  (max ~174 characters). Example style: `OneDrive!S-1-…!Personal`.
- Relevant info fields: hydration/population/in-sync/hardlink policies,
  pinning, `ShowSiblingsAsGroup`, recycle URI, context blob, item property
  definitions, provider GUID.
- Official sample **CloudMirror** lives in
  [Windows-classic-samples/Samples/CloudMirror](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/CloudMirror).
  It is educational, not a production architecture.
- CloudMirror demonstrates WinRT `Register`, `CfConnectSyncRoot`,
  `FETCH_DATA` / cancel, placeholder creation, progress reporting, custom
  states, thumbnails, context menus, status UI, and MSIX packaging with
  `desktop3:CloudFiles` + `com:ExeServer`.
- CloudMirror does **not** demonstrate real bidirectional sync, robust
  conflict handling, or a production process split.

### CloudMirror / docs discrepancies (Confirmed)

| Topic | Observation |
|-------|-------------|
| Registration | CloudMirror does **not** call `CfRegisterSyncRoot`; it uses WinRT `Register` + `CfConnectSyncRoot`. |
| FAQ wording | Microsoft FAQ states “use one, not both” and recommends WinRT `Register`, yet also describes `CfRegisterSyncRoot` + connect flows. |
| Free up space | Sample watches `UNPINNED` via directory changes; `NOTIFY_DEHYDRATE` is not what Explorer uses for that UI. |
| Dehydrate API | Prefer `CfUpdatePlaceholder` dehydrate flags; dedicated dehydrate Learn pages have been inconsistent. |
| COM hosting | CloudMirror hosts Shell COM in the same EXE; Microsoft Q&A recommends a **separate** process for Shell extensions. |
| Build docs | Sample README/tooling notes can lag current VS/SDK; use current `main` + recent Windows SDK. |

### Assumption

- For KIODrive production: always WinRT-register for Shell UX; connect CFAPI for
  callbacks; only add explicit `CfRegisterSyncRoot` if a spike shows missing
  policy coverage.

### Prototype needed

- Side-by-side comparison on the development machine: WinRT-only vs
  WinRT + `CfRegisterSyncRoot` for navigation pane, grey-X failures, and
  `GetSyncRootInformationForFolder`.

### Unsupported

- Copying CloudMirror’s single-process COM+engine layout as the production model.

### References

- https://learn.microsoft.com/en-us/uwp/api/windows.storage.provider.storageprovidersyncrootmanager
- https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-desktop3-cloudfiles
- https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/cloudmirror-sample/

---

## 3. Single sync root with logical mounts

### Confirmed

- The platform forbids overlapping sync-root trees.
- Multiple sync roots for one provider are allowed when paths do **not** overlap
  and `AccountID` (in the root Id) differs.
- Placeholders under one connected sync root are ordinary placeholder files and
  directories; the platform does not define “mount” objects inside a root.

### Assumption

- A single user-visible root such as `%USERPROFILE%\KIO` with per-mount
  subfolders (`…\KIO\<mountName>\…`) is accepted by Explorer and CFAPI when each
  mount is a normal placeholder subtree keyed by app metadata.
- This model avoids ancestor/descendant sync-root conflicts with OneDrive and
  with other KIODrive mounts.

### Prototype needed

- UX of many logical mounts under one root (navigation pane, pin/hydrate
  inheritance, Free up space scope).
- Whether sibling sync roots + `ShowSiblingsAsGroup` would be preferable for
  some account models (docs for grouping are thin / historically confusing).

### Unsupported

- Nested CFAPI sync roots per mount.
- Registering the volume root as the KIODrive sync root.

---

## 4. KF6 KIO on Windows Craft (`D:\Craft`)

Craft target observed: **`windows-cl-msvc2022-x86_64`**.

### Confirmed (local install)

| Package | Version / evidence |
|---------|--------------------|
| KIO | **6.30.0** |
| kio-extras | **26.08.1** |
| ECM / breeze-icons | **6.30.0** |
| Qt | **6.9.0** |
| Worker launcher | `D:\Craft\bin\kioworker.exe` |
| Plugins | `D:\Craft\plugins\kf6\kio\` (`kio_file`, `kio_ftp`, `kio_http`, `sftp`, `archive`, `filter`, …) |

Also confirmed:

- **No** `kpasswdserver` / `kiod` session daemon in this Craft prefix.
- **No** KWallet integration in the Craft KIO blueprint
  (`CMAKE_DISABLE_FIND_PACKAGE_KF6Wallet=ON` / D-Bus off).
- **`HAVE_QTDBUS` / session D-Bus** is not available as Plasma-style session
  infrastructure on this Windows Craft build.
- Protocol discovery is **not** primarily via loose `*.protocol` files.
  KF6 loads `KPluginMetaData::findPlugins("kf6/kio")` and reads embedded
  **`KDE-KIO-Protocols`** JSON from each plugin DLL.
- Remote workers run **out-of-process** via `kioworker.exe` + `QLocalSocket`.
  `file` / `admin` may run in-process (threaded) in recent KF6.
- Scheduler reuses idle workers for about **3 minutes**; per-protocol limits
  apply (SFTP declares `maxInstances=20`, `maxInstancesPerHost=5`).
- Filesystem-like filter for mounts:
  `known && inputType == T_FILESYSTEM && supportsListing && supportsReading
  && !isFilterProtocol && !isHelperProtocol`.
- **SFTP** (`sftp.json`): reading, writing, deleting, moving, makedir, linking,
  **opening**, **truncating**. Seek via `sftp_seek64` / `KIO::FileJob`.
- **FTP**: reading/writing/deleting/moving/makedir present; **no** `opening`
  visible in plugin metadata → sequential `get`/`put` model.
- **WebDAV / dav / davs**: hosted in `kio_http.dll`; listing + recursive delete
  capabilities present.
- **No `UDS_ETAG`**. Candidates: `UDS_XML_PROPERTIES` (WebDAV), plus
  `UDS_INODE`, `UDS_MIME_TYPE`, `UDS_MODIFICATION_TIME` (+ ns offset in recent KF).
- `WorkerBase::openPasswordDialog()` talks to `org.kde.kpasswdserver6` and fails
  with **`ERR_PASSWD_SERVER`** without the daemon.

### Assumption

- Credentials can be injected into jobs via `KIO::AuthInfo`, URL userinfo
  (memory-only), and job metadata such as `no-auth-prompt`, without
  kpasswdserver.
- HTTP Range / `range-start` metadata maps usefully for some HTTP/WebDAV GETs,
  but behavior and latency must be measured.

### Prototype needed

- SFTP auth on Craft Windows: password, key+passphrase, host-key prompts
  (`messageBox` in the worker process).
- JobUiDelegate / auth injection without D-Bus.
- Extracting ETag/revision from `UDS_XML_PROPERTIES`.
- Spawn/reuse latency of `kioworker.exe` under CFAPI’s ~60 s callback budget.

### Unsupported (this Craft Windows build)

- **SMB** worker (`SAMBA_FOUND=false` / not built for WIN32).
- **fish**, **nfs**, **man**, **mtp**, **afc**, activities / recentlyused-style
  helpers that need D-Bus or non-Windows deps.
- Relying on KWallet or kpasswdserver for credential persistence.
- Using **kio-fuse** itself on Windows (FUSE3 + Linux/BSD + D-Bus).

### References

- https://develop.kde.org/docs/getting-started/building/craft/
- https://invent.kde.org/frameworks/kio
- https://invent.kde.org/network/kio-extras
- https://api.kde.org/kio.html

---

## 5. Authentication must be application-owned on Windows

### Confirmed

- Plasma session services (D-Bus kpasswdserver, KWallet) are absent from the
  Craft Windows KIO stack used by KIODrive.
- Interactive password dialogs initiated by workers fail without those services.
- SFTP host-key verification in `kio_sftp` uses libssh known-hosts checks and
  `WorkerBase::messageBox` (runs in the **worker** process).

### Assumption

- Pre-authenticating in the GUI / engine and injecting credentials before
  CFAPI-driven jobs is sufficient for first-party protocols (SFTP first).
- Host keys should be stored and confirmed by KIODrive UI, then reflected into
  the environment the worker expects (`known_hosts` and/or interception).

### Prototype needed

- End-to-end SFTP login without ERR_PASSWD_SERVER.
- Intercepting or replacing host-key `messageBox` prompts from `kioworker.exe`.
- Mapping auth failures during CFAPI callbacks to mount state
  `AuthenticationRequired` without stalling Explorer for ~60 s.

### Unsupported

- Assuming KDE Wallet / kpasswdserver will appear “for free” in a packaged
  Windows app.
- Persisting passwords in URLs, SQLite, logs, or `FileIdentity`.

---

## 6. Lessons from kio-fuse (patterns only)

Source: [kio-fuse DESIGN.md](https://invent.kde.org/system/kio-fuse/-/blob/master/DESIGN.md).
kio-fuse itself remains **Unsupported** on Windows; only architectural lessons apply.

### Confirmed lessons

1. **Minimum protocol surface:** `KIO::stat` + `KIO::get` (or `open` when available).
2. **Component lookup:** prefer `stat` per path segment over listing every parent
   when opening a deep path.
3. **Lazy listing:** call `listDir` only when directory contents are required;
   cache children/attributes with a TTL (kio-fuse uses ~30 s).
4. **Two file I/O strategies:**
   - **`KIO::FileJob`** (random access) when `opening` is supported — historically
     `file` / `sftp` / `smb`.
   - **Temp cache + full `get`/`put`** for sequential protocols.
5. Stable **local identity** (inode-like IDs, trees) is the **app’s** job; KIO
   does not provide a global stable cloud identity.
6. Permissions: forward worker errors; do not invent a parallel ACL model.
7. Write path pattern: validate locally → start KIO job → update local model only
   after success (with compensation/journal for CFAPI).

### Assumption

- Mapping these patterns to CFAPI placeholders (instead of FUSE inodes) preserves
  the same KIO job discipline.

### Prototype needed

- Whether SFTP `FileJob` range I/O under CFAPI `FETCH_DATA` meets latency and
  cancellation requirements better than sequential `get`.

### Unsupported

- Shipping or depending on kio-fuse / FUSE / Dokan as the primary projection layer.

---

## 7. Packaging: `kioworker.exe` + `plugins/kf6/kio` DLLs

### Confirmed

- Runtime needs the KIO worker launcher and protocol plugins discoverable on the
  Qt/KF plugin path (`plugins/kf6/kio/*.dll`), not a scattered set of `.protocol`
  files.
- Workers load via paths returned from protocol metadata / plugin lookup.
- Craft already provides a reference layout under `D:\Craft`.

### Assumption

- A redistributable prefix next to KIODrive (or inside MSIX) plus
  `QCoreApplication::addLibraryPath` / `QStandardPaths` overrides is enough for
  discovery without a system-wide KDE install.

### Prototype needed

- Minimal redistributable set: `kioworker.exe`, KF6/Qt DLLs, `plugins/kf6/kio`,
  Qt plugins, translations, icon theme pieces as needed.
- MSIX path virtualization vs Craft absolute paths.
- Worker spawn under AppContainer / packaged identity constraints.

### Unsupported

- Expecting end users to install a full Craft/KDE stack separately for KIODrive
  to function.

---

## 8. Cross-cutting risks for KIODrive

| Risk | Classification |
|------|----------------|
| Auth gap on Windows KIO | Confirmed + prototype needed |
| CFAPI 60 s callback vs KIO latency | Confirmed constraint |
| Watcher feedback loops with `CfCreatePlaceholders` / updates | Prototype needed |
| Duplicate rename/delete notify paths | Assumption / journal must be idempotent |
| Illegal Win32 names / case collisions from remote names | Confirmed problem space |
| Remote symlinks vs Cloud Files reparse points | Prototype needed (do not nest blindly) |
| Dirty file dehydration | Confirmed hazard — never dehydrate OutOfSync/Uploading/Conflict |
| Orphan sync roots after bad MSIX uninstall | Confirmed when `windows.cloudFiles` missing |
| SMB via KIO on current Craft Windows | Unsupported |

---

## 9. Suggested spikes (worktree-isolated)

1. SFTP authentication on Craft Windows without D-Bus.
2. WinRT `Register` vs `CfRegisterSyncRoot` on the target OS.
3. Range hydration with SFTP `FileJob` vs sequential `KIO::get`.
4. Host-key / `messageBox` interception from the engine process.
5. Packaging smoke test: `kioworker.exe` + `plugins/kf6/kio` from an app-local prefix.
6. WebDAV ETag extraction from `UDS_XML_PROPERTIES`.
7. Worker spawn/reuse latency under concurrent CFAPI callbacks.

---

## 10. Decision index

Architectural decisions derived from this research are recorded as ADRs:

| ADR | Topic |
|-----|-------|
| [0001](../adr/0001-single-sync-root.md) | Single sync root + logical mounts |
| [0002](../adr/0002-winrt-registration.md) | WinRT sync-root registration |
| [0003](../adr/0003-placeholder-identity.md) | Placeholder `FileIdentity` |
| [0004](../adr/0004-sqlite-metadata.md) | SQLite metadata store |
| [0005](../adr/0005-credential-storage.md) | Credential storage |
| [0006](../adr/0006-process-architecture.md) | Process architecture |
| [0007](../adr/0007-ipc-named-pipes.md) | Named-pipe IPC |
| [0008](../adr/0008-local-change-detection.md) | Local change detection |
| [0009](../adr/0009-remote-refresh.md) | Remote refresh |
| [0010](../adr/0010-filename-virtualization.md) | Filename virtualization |
| [0011](../adr/0011-symlink-semantics.md) | Symlink semantics |
| [0012](../adr/0012-range-hydration.md) | Range hydration |

---

## 11. Spike and build observations (2026-09-13)

| Observation | Classification |
|-------------|----------------|
| Project builds with Craft Qt 6.9 / KF6 6.30 against MSVC 2022 | **Confirmed** |
| Unit tests (identity, path router, mount conflicts, name mapper, mock KIO, metadata) pass | **Confirmed** |
| `CfRegisterSyncRoot` on unpackaged Public folder returns `S_OK` (link against **`cldapi.lib`**, not `cfapi.lib`) | **Confirmed** |
| `cfapi.h` requires `<winternl.h>` (or NTSTATUS) before include under MSVC | **Confirmed** |
| `KIODrive.SyncEngine` starts and creates `%USERPROFILE%\KIO` sync-root directory | **Confirmed** |
| SFTP live auth with real host (`spikes/auth-sftp`) | **Prototype needed** (tooling ready; needs credentials + server) |
| Full Explorer open → hydrate end-to-end on a live SFTP mount | **Prototype needed** (code path present; needs live server) |

---

*Last updated: 2026-09-13. Revisit after live SFTP hydration validation.*

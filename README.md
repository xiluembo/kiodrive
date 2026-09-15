<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# KIODrive

KIODrive exposes KDE Frameworks 6 KIO protocols as native Windows Cloud Files
placeholders under a single sync root (`%USERPROFILE%\KIO`), with logical
mounts for URLs such as `sftp://`, `webdav://`, and `ftp://`.

## Architecture

- **KIODrive.SyncEngine** — long-lived Cloud Files provider and KIO backend
- **KIODrive.Gui** — Qt 6 management UI and system tray
- **KIODrive.Shell** — lightweight COM shell handlers (no Qt/KF6)

See [docs/architecture/research.md](docs/architecture/research.md) and
[docs/adr/](docs/adr/).

## Run (development)

**Important:** only one process may own the Cloud Files connection.

### Recommended for Explorer

```powershell
. D:\Craft\craft\craftenv.ps1
$env:CRAFT_ROOT="D:\Craft"
# Option A — GUI embeds the sync engine (simplest):
.\build\bin\KIODrive.Gui.exe

# Option B — headless provider only:
.\build\bin\KIODrive.SyncEngine.exe
```

Do **not** start Gui and SyncEngine together. `KIODrive.Shell.exe` is optional
(COM handlers) and is not required to open `%USERPROFILE%\KIO`.

If Explorer says the cloud provider is not running, nothing has successfully
called `CfConnectSyncRoot` with a callback table.

## Build (Craft)

```powershell
. D:\Craft\craft\craftenv.ps1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

GNU Lesser General Public License v3.0 or later. See [LICENSE](LICENSE).

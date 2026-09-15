# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: CC0-1.0

# KIODrive SFTP auth spike (Windows / Craft)

## Goal

Prove SFTP `KIO::stat` works on Windows Craft without D-Bus/`kpasswdserver`
by injecting username/password into the URL and setting `no-auth-prompt`.

## Classification

| Finding | Status |
|---------|--------|
| Craft KIO has no kpasswdserver | confirmed |
| `openPasswordDialog` fails without daemon | confirmed |
| URL/userinfo injection + `no-auth-prompt` | prototype needed (run this spike) |
| Host-key `messageBox` in worker | prototype needed |

## Run

```powershell
. D:\Craft\craft\craftenv.ps1
cmake -S spikes/auth-sftp -B build-spike-auth -G Ninja
cmake --build build-spike-auth
$env:KIODRIVE_SPIKE_USER="me"
$env:KIODRIVE_SPIKE_PASSWORD="secret"
.\build-spike-auth\kiodrive_spike_auth_sftp.exe sftp://host/home/me/
```

Document observed `error=` codes in research.md.

# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: CC0-1.0

# CFAPI registration spike

Build with MSVC + Windows SDK:

```text
cl /EHsc /DUNICODE /D_UNICODE main.cpp /link cfapi.lib ole32.lib
```

Record HRESULT of `CfRegisterSyncRoot` on unpackaged vs MSIX builds in
`docs/architecture/research.md`. Production path remains WinRT
`StorageProviderSyncRootManager::Register` + MSIX `desktop3:CloudFiles`.

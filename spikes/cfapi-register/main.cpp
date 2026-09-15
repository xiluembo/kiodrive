/*
 * SPDX-FileCopyrightText: 2026 KIODrive authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Spike: compare CfRegisterSyncRoot vs documenting WinRT Register requirement.
 */

#include <windows.h>
#include <winternl.h>
#include <cfapi.h>

#include <stdio.h>

int wmain()
{
    const wchar_t *path = L"C:\\Users\\Public\\KIODriveSpikeRoot";
    CreateDirectoryW(path, nullptr);

    CF_SYNC_REGISTRATION registration{};
    registration.StructSize = sizeof(registration);
    registration.ProviderName = L"KIODriveSpike";
    registration.ProviderVersion = L"0.1";
    GUID id = {0x11111111, 0x2222, 0x3333, {0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb}};
    registration.ProviderId = id;

    CF_SYNC_POLICIES policies{};
    policies.StructSize = sizeof(policies);
    policies.Hydration.Primary = CF_HYDRATION_POLICY_FULL;
    policies.Population.Primary = CF_POPULATION_POLICY_FULL;

    const HRESULT hr = CfRegisterSyncRoot(path, &registration, &policies, CF_REGISTER_FLAG_NONE);
    printf("CfRegisterSyncRoot hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        CfUnregisterSyncRoot(path);
    }
    return 0;
}

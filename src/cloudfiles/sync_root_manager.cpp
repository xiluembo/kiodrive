/*
 * SPDX-FileCopyrightText: 2026 KIODrive authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of KIODrive.
 *
 * KIODrive is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 */

#include "sync_root_manager.h"

#include "common/logging.h"

#include <QDir>
#include <QStandardPaths>

namespace KIODrive
{

SyncRootManager::SyncRootManager()
    : m_providerName(QStringLiteral("KIODrive"))
    , m_providerGuid(QUuid(QStringLiteral("a3b7c1d2-4e5f-6789-abcd-ef0123456789")))
{
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    m_syncRootPath = QDir(home).filePath(QStringLiteral("KIO"));
}

QString SyncRootManager::syncRootPath() const
{
    return m_syncRootPath;
}

QString SyncRootManager::providerId() const
{
    return m_providerGuid.toString(QUuid::WithoutBraces);
}

QString SyncRootManager::syncRootId() const
{
    return QStringLiteral("KIODrive!Local!Default");
}

bool SyncRootManager::ensureSyncRootDirectory()
{
    QDir dir;
    if (!dir.mkpath(m_syncRootPath)) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "Failed to create sync root" << m_syncRootPath;
        return false;
    }
    return true;
}

bool SyncRootManager::registerSyncRoot()
{
    if (!ensureSyncRootDirectory()) {
        return false;
    }

#ifdef Q_OS_WIN
    CF_SYNC_REGISTRATION registration{};
    registration.StructSize = sizeof(registration);
    registration.ProviderName = L"KIODrive";
    registration.ProviderVersion = L"0.1.0";
    const GUID providerGuid = {0xa3b7c1d2, 0x4e5f, 0x6789, {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89}};
    registration.ProviderId = providerGuid;

    CF_SYNC_POLICIES policies{};
    policies.StructSize = sizeof(policies);
    policies.Hydration.Primary = CF_HYDRATION_POLICY_FULL;
    policies.Population.Primary = CF_POPULATION_POLICY_FULL;
    policies.InSync = CF_INSYNC_POLICY_TRACK_FILE_CREATION_TIME | CF_INSYNC_POLICY_TRACK_DIRECTORY_CREATION_TIME;
    policies.HardLink = CF_HARDLINK_POLICY_NONE;

    const HRESULT hr = CfRegisterSyncRoot(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(m_syncRootPath).utf16()),
                                          &registration,
                                          &policies,
                                          CF_REGISTER_FLAG_NONE);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "CfRegisterSyncRoot failed" << Qt::hex << static_cast<quint32>(hr)
                                      << "- continuing with local namespace only";
        m_registered = false;
        return true;
    }
    m_registered = true;
    qCInfo(KIODRIVE_CFAPI_LOG) << "Registered sync root" << m_syncRootPath;
    return true;
#else
    m_registered = true;
    return true;
#endif
}

bool SyncRootManager::unregisterSyncRoot()
{
#ifdef Q_OS_WIN
    const HRESULT hr =
        CfUnregisterSyncRoot(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(m_syncRootPath).utf16()));
    if (FAILED(hr)) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "CfUnregisterSyncRoot failed" << Qt::hex << static_cast<quint32>(hr);
        return false;
    }
    m_registered = false;
    return true;
#else
    m_registered = false;
    return true;
#endif
}

bool SyncRootManager::connectCallbacks(const void *callbackTable, void *callbackContext)
{
#ifdef Q_OS_WIN
    if (!callbackTable) {
        qCCritical(KIODRIVE_CFAPI_LOG) << "CfConnectSyncRoot requires a non-null callback table";
        m_connected = false;
        return false;
    }

    CF_CONNECTION_KEY key{};
    const HRESULT hr = CfConnectSyncRoot(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(m_syncRootPath).utf16()),
                                         static_cast<const CF_CALLBACK_REGISTRATION *>(callbackTable),
                                         callbackContext,
                                         CF_CONNECT_FLAG_REQUIRE_PROCESS_INFO | CF_CONNECT_FLAG_REQUIRE_FULL_FILE_PATH,
                                         &key);
    if (FAILED(hr)) {
        qCCritical(KIODRIVE_CFAPI_LOG) << "CfConnectSyncRoot failed" << Qt::hex << static_cast<quint32>(hr)
                                       << "- Explorer will report that the cloud provider is not running";
        m_connected = false;
        return false;
    }
    m_connectionKey = key;
    m_connected = true;
    qCInfo(KIODRIVE_CFAPI_LOG) << "CfConnectSyncRoot succeeded for" << m_syncRootPath;
    return true;
#else
    Q_UNUSED(callbackTable);
    Q_UNUSED(callbackContext);
    m_connected = true;
    return true;
#endif
}

bool SyncRootManager::isRegistered() const
{
    return m_registered;
}

void SyncRootManager::disconnectCallbacks()
{
#ifdef Q_OS_WIN
    if (m_connected) {
        CfDisconnectSyncRoot(m_connectionKey);
        m_connectionKey = {};
        m_connected = false;
    }
#else
    m_connected = false;
#endif
}

bool SyncRootManager::isConnected() const
{
    return m_connected;
}

} // namespace KIODrive

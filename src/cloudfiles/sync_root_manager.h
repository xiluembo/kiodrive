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

#ifndef KIODRIVE_CLOUDFILES_SYNC_ROOT_MANAGER_H
#define KIODRIVE_CLOUDFILES_SYNC_ROOT_MANAGER_H

#include <QString>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winternl.h>
#include <cfapi.h>
#endif

namespace KIODrive
{

/**
 * Owns the single provider sync root at %USERPROFILE%\KIO.
 * Logical mounts are subdirectories, not additional CFAPI roots.
 */
class SyncRootManager
{
public:
    SyncRootManager();

    QString syncRootPath() const;
    QString providerId() const;
    QString syncRootId() const;

    bool ensureSyncRootDirectory();
    bool registerSyncRoot();
    bool unregisterSyncRoot();
    /**
     * Connect CFAPI callbacks. @p callbackTable must be a CF_CALLBACK_REGISTRATION
     * array ending with CF_CALLBACK_REGISTRATION_END (Windows). Passing null fails.
     */
    bool connectCallbacks(const void *callbackTable, void *callbackContext);
    void disconnectCallbacks();

    bool isConnected() const;
    bool isRegistered() const;

private:
    QString m_syncRootPath;
    QString m_providerName;
    QUuid m_providerGuid;
    bool m_registered = false;
    bool m_connected = false;
#ifdef Q_OS_WIN
    CF_CONNECTION_KEY m_connectionKey{};
#endif
};

} // namespace KIODrive

#endif // KIODRIVE_CLOUDFILES_SYNC_ROOT_MANAGER_H

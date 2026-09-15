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

#include "placeholder_manager.h"

#include "common/logging.h"
#include "common/types.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winternl.h>
#include <cfapi.h>
#endif

namespace KIODrive
{

PlaceholderManager::PlaceholderManager(const QString &syncRootPath)
    : m_syncRootPath(syncRootPath)
{
}

QString PlaceholderManager::absolutePath(const QString &relativePath) const
{
    if (relativePath.isEmpty()) {
        return m_syncRootPath;
    }
    return QDir(m_syncRootPath).filePath(relativePath);
}

bool PlaceholderManager::createPlaceholders(const QString &parentRelativePath,
                                            const QVector<PlaceholderCreateRequest> &items)
{
    if (items.isEmpty()) {
        return true;
    }

    const QString parentPath = absolutePath(parentRelativePath);
    QDir().mkpath(parentPath);

#ifdef Q_OS_WIN
    QVector<CF_PLACEHOLDER_CREATE_INFO> infos;
    infos.reserve(items.size());
    QVector<QByteArray> identities;
    identities.reserve(items.size());
    QVector<QString> names;
    names.reserve(items.size());

    for (const PlaceholderCreateRequest &item : items) {
        names.push_back(item.fileName);
        identities.push_back(item.identity.serialize());

        CF_PLACEHOLDER_CREATE_INFO info{};
        info.RelativeFileName = reinterpret_cast<LPCWSTR>(names.last().utf16());
        info.FileIdentity = identities.last().constData();
        info.FileIdentityLength = static_cast<DWORD>(identities.last().size());
        info.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;
        info.FsMetadata.FileSize.QuadPart = item.isDirectory ? 0 : item.fileSize;
        info.FsMetadata.BasicInfo.FileAttributes =
            item.isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        infos.push_back(info);
    }

    const HRESULT hr = CfCreatePlaceholders(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(parentPath).utf16()),
                                            infos.data(),
                                            static_cast<DWORD>(infos.size()),
                                            CF_CREATE_FLAG_NONE,
                                            nullptr);
    if (FAILED(hr)) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "CfCreatePlaceholders failed" << Qt::hex << hr;
        return false;
    }
    return true;
#else
    for (const PlaceholderCreateRequest &item : items) {
        const QString path = QDir(parentPath).filePath(item.fileName);
        if (item.isDirectory) {
            QDir().mkpath(path);
        } else {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
            }
        }
    }
    return true;
#endif
}

bool PlaceholderManager::ensureMountRootPlaceholder(const MountInfo &mount)
{
#ifdef Q_OS_WIN
    // A prior bug created normal (non-placeholder) mount folders via mkpath. Remove empty
    // ones so CfCreatePlaceholders can install a proper cloud directory placeholder.
    const QString path = absolutePath(mount.localRelativePath);
    QFileInfo existing(path);
    if (existing.exists() && existing.isDir()) {
        const DWORD attrs = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()));
        const bool isReparse = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
        if (!isReparse) {
            QDir dir(path);
            if (dir.isEmpty() && !dir.rmdir(path)) {
                qCWarning(KIODRIVE_CFAPI_LOG) << "Could not remove empty non-placeholder mount dir" << path;
            }
        }
    }
#endif

    PlaceholderCreateRequest req;
    req.fileName = QDir::toNativeSeparators(mount.localRelativePath);
    req.isDirectory = true;
    req.fileSize = 0;
    req.identity.mountId = mount.id;
    req.identity.itemId = mount.id;
    req.identity.revisionSeq = 1;
    return createPlaceholders(QString(), {req});
}

bool PlaceholderManager::enableOnDemandPopulation(const QString &relativePath)
{
#ifdef Q_OS_WIN
    const QString path = absolutePath(relativePath);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
                                FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "enableOnDemandPopulation open failed" << path << GetLastError();
        return false;
    }
    const HRESULT hr = CfUpdatePlaceholder(handle,
                                           nullptr,
                                           nullptr,
                                           0,
                                           nullptr,
                                           0,
                                           CF_UPDATE_FLAG_ENABLE_ON_DEMAND_POPULATION,
                                           nullptr,
                                           nullptr);
    CloseHandle(handle);
    if (FAILED(hr)) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "CfUpdatePlaceholder ENABLE_ON_DEMAND failed" << Qt::hex
                                      << static_cast<quint32>(hr);
        return false;
    }
    return true;
#else
    Q_UNUSED(relativePath);
    return true;
#endif
}

bool PlaceholderManager::updatePlaceholderIdentity(const QString &relativePath, const FileIdentityV1 &identity)
{
#ifdef Q_OS_WIN
    const QString path = absolutePath(relativePath);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
                                FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    const QByteArray blob = identity.serialize();
    CF_FS_METADATA metadata{};
    const HRESULT hr = CfUpdatePlaceholder(handle,
                                           &metadata,
                                           blob.constData(),
                                           static_cast<DWORD>(blob.size()),
                                           nullptr,
                                           0,
                                           CF_UPDATE_FLAG_NONE,
                                           nullptr,
                                           nullptr);
    CloseHandle(handle);
    return SUCCEEDED(hr);
#else
    Q_UNUSED(relativePath);
    Q_UNUSED(identity);
    return true;
#endif
}

bool PlaceholderManager::setInSync(const QString &relativePath, bool inSync)
{
#ifdef Q_OS_WIN
    const QString path = absolutePath(relativePath);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
                                FILE_WRITE_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    const CF_IN_SYNC_STATE state = inSync ? CF_IN_SYNC_STATE_IN_SYNC : CF_IN_SYNC_STATE_NOT_IN_SYNC;
    const HRESULT hr = CfSetInSyncState(handle, state, CF_SET_IN_SYNC_FLAG_NONE, nullptr);
    CloseHandle(handle);
    return SUCCEEDED(hr);
#else
    Q_UNUSED(relativePath);
    Q_UNUSED(inSync);
    return true;
#endif
}

bool PlaceholderManager::dehydrate(const QString &relativePath)
{
#ifdef Q_OS_WIN
    const QString path = absolutePath(relativePath);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
                                FILE_WRITE_DATA | FILE_READ_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    CF_FILE_RANGE range{};
    range.StartingOffset.QuadPart = 0;
    range.Length.QuadPart = MAXLONGLONG;
    const HRESULT hr = CfUpdatePlaceholder(handle,
                                           nullptr,
                                           nullptr,
                                           0,
                                           &range,
                                           1,
                                           CF_UPDATE_FLAG_DEHYDRATE,
                                           nullptr,
                                           nullptr);
    CloseHandle(handle);
    return SUCCEEDED(hr);
#else
    Q_UNUSED(relativePath);
    return true;
#endif
}

bool PlaceholderManager::setPinState(const QString &relativePath, bool pinned)
{
#ifdef Q_OS_WIN
    const QString path = absolutePath(relativePath);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
                                FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    const CF_PIN_STATE state = pinned ? CF_PIN_STATE_PINNED : CF_PIN_STATE_UNPINNED;
    const HRESULT hr = CfSetPinState(handle, state, CF_SET_PIN_FLAG_NONE, nullptr);
    CloseHandle(handle);
    return SUCCEEDED(hr);
#else
    Q_UNUSED(relativePath);
    Q_UNUSED(pinned);
    return true;
#endif
}

} // namespace KIODrive

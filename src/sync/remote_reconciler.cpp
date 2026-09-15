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

#include "remote_reconciler.h"

#include "common/logging.h"
#include "kio/name_mapper.h"

#include <QSet>
#include <QUuid>

namespace KIODrive
{

namespace
{

QString joinRemotePath(const QString &parentRemotePath, const QString &name)
{
    if (parentRemotePath.isEmpty() || parentRemotePath == QLatin1String("/")) {
        return QLatin1Char('/') + name;
    }
    if (parentRemotePath.endsWith(QLatin1Char('/'))) {
        return parentRemotePath + name;
    }
    return parentRemotePath + QLatin1Char('/') + name;
}

} // namespace

RemoteReconciler::RemoteReconciler(MetadataStore *store,
                                   MountRegistry *mounts,
                                   KioBackend *backend,
                                   PlaceholderManager *placeholders,
                                   QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_mounts(mounts)
    , m_backend(backend)
    , m_placeholders(placeholders)
{
}

QVector<PlaceholderCreateRequest> RemoteReconciler::syncDirectoryEntries(const QUuid &mountId, const QString &remotePath)
{
    QVector<PlaceholderCreateRequest> creates;
    const auto mount = m_mounts->mount(mountId);
    if (!mount || !m_backend || !m_store) {
        return creates;
    }

    QUrl url = mount->remoteUrl;
    url.setPath(remotePath);

    m_backend->setMountId(mountId);
    const BackendResult result = m_backend->listDirSync(url);
    if (!result.ok) {
        qCWarning(KIODRIVE_SYNC_LOG) << "listDir failed" << remotePath << result.errorString;
        return creates;
    }

    NameMapper mapper;
    QSet<QString> usedNames;
    std::optional<ItemRecord> parentItem = m_store->itemByRemotePath(mountId, remotePath);

    for (const RemoteEntry &entry : result.entries) {
        const QString entryRemotePath = joinRemotePath(remotePath, entry.name);
        const NameMapping mapped = mapper.mapToWindows(entry.name, usedNames);
        usedNames.insert(mapped.windowsName);

        ItemRecord item;
        auto existing = m_store->itemByRemotePath(mountId, entryRemotePath);
        if (existing) {
            item = *existing;
            if (item.syncState == ItemSyncState::OutOfSync || item.syncState == ItemSyncState::Uploading) {
                if (item.mtime != entry.mtime || item.size != entry.size) {
                    Q_EMIT conflictDetected(item.id);
                    continue;
                }
            }
            item.revisionSeq += 1;
        } else {
            item.id = QUuid::createUuid();
            item.mountId = mountId;
            if (parentItem) {
                item.parentId = parentItem->id;
            }
            item.revisionSeq = 1;
        }

        item.remotePath = entryRemotePath;
        item.remoteName = entry.name;
        item.windowsName = mapped.windowsName;
        item.kind = entry.kind;
        item.mime = entry.mime;
        item.size = entry.size;
        item.mtime = entry.mtime;
        item.remoteInode = entry.remoteInode;
        item.syncState = ItemSyncState::OnlineOnly;
        m_store->upsertItem(item);

        PlaceholderCreateRequest req;
        req.fileName = mapped.windowsName;
        req.isDirectory = entry.kind == ItemKind::Directory;
        req.fileSize = entry.size < 0 ? 0 : entry.size;
        req.identity.mountId = mountId;
        req.identity.itemId = item.id;
        req.identity.revisionSeq = item.revisionSeq;
        creates.push_back(req);
    }
    return creates;
}

void RemoteReconciler::refreshDirectory(const QUuid &mountId,
                                        const QString &remotePath,
                                        const QString &localRelativePath)
{
    const QVector<PlaceholderCreateRequest> creates = syncDirectoryEntries(mountId, remotePath);
    if (m_placeholders) {
        m_placeholders->createPlaceholders(localRelativePath, creates);
    }
    Q_EMIT directoryRefreshed(mountId, localRelativePath);
}

} // namespace KIODrive

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

#ifndef KIODRIVE_METADATA_METADATA_STORE_H
#define KIODRIVE_METADATA_METADATA_STORE_H

#include "common/types.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <optional>

namespace KIODrive
{

struct ItemRecord {
    QUuid id;
    QUuid mountId;
    std::optional<QUuid> parentId;
    QString remotePath;
    QString remoteName;
    QString windowsName;
    ItemKind kind = ItemKind::File;
    QString mime;
    qint64 size = -1;
    qint64 mtime = 0;
    QString remoteInode;
    QString revisionToken;
    quint64 revisionSeq = 0;
    ItemSyncState syncState = ItemSyncState::OnlineOnly;
    int metadataVersion = 1;
};

struct OperationRecord {
    QUuid id;
    QUuid mountId;
    QUuid itemId;
    QString type;
    OperationState state = OperationState::Prepared;
    QString payloadJson;
    QString idempotencyKey;
};

/**
 * SQLite WAL store for mounts, items, operations, and conflicts.
 * Credentials are never stored here.
 */
class MetadataStore
{
public:
    explicit MetadataStore(const QString &databasePath);
    ~MetadataStore();

    bool open();
    bool isOpen() const;
    QString lastError() const;

    bool beginImmediate();
    bool commit();
    bool rollback();

    bool upsertMount(const MountInfo &mount);
    bool removeMount(const QUuid &mountId);
    std::optional<MountInfo> mountById(const QUuid &id) const;
    std::optional<MountInfo> mountByLocalRelPath(const QString &relPath) const;
    QVector<MountInfo> allMounts() const;

    bool upsertItem(const ItemRecord &item);
    std::optional<ItemRecord> itemById(const QUuid &id) const;
    std::optional<ItemRecord> itemByRemotePath(const QUuid &mountId, const QString &remotePath) const;
    QVector<ItemRecord> childrenOf(const QUuid &parentId) const;
    /** Items whose remote_path equals @p prefix or is under @p prefix/. */
    QVector<ItemRecord> itemsWithRemotePathPrefix(const QUuid &mountId, const QString &prefix) const;
    bool updateItemSyncState(const QUuid &itemId, ItemSyncState state);

    bool insertOperation(const OperationRecord &op);
    bool updateOperationState(const QUuid &opId, OperationState state);
    QVector<OperationRecord> incompleteOperations() const;

private:
    bool migrate();
    int schemaVersion() const;
    MountInfo mountFromQuery(const class QSqlQuery &query) const;
    ItemRecord itemFromQuery(const class QSqlQuery &query) const;

    QString m_databasePath;
    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_lastError;
};

} // namespace KIODrive

#endif // KIODRIVE_METADATA_METADATA_STORE_H

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

#include "metadata_store.h"
#include "schema.h"

#include "common/logging.h"
#include "common/path_utils.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace KIODrive
{

namespace
{

QString kindToString(ItemKind kind)
{
    switch (kind) {
    case ItemKind::File:
        return QStringLiteral("file");
    case ItemKind::Directory:
        return QStringLiteral("directory");
    case ItemKind::Symlink:
        return QStringLiteral("symlink");
    case ItemKind::Unsupported:
        return QStringLiteral("unsupported");
    }
    return QStringLiteral("file");
}

ItemKind kindFromString(const QString &value)
{
    if (value == QLatin1String("directory")) {
        return ItemKind::Directory;
    }
    if (value == QLatin1String("symlink")) {
        return ItemKind::Symlink;
    }
    if (value == QLatin1String("unsupported")) {
        return ItemKind::Unsupported;
    }
    return ItemKind::File;
}

MountState mountStateFromString(const QString &value)
{
    if (value == QLatin1String("Online")) {
        return MountState::Online;
    }
    if (value == QLatin1String("Connecting")) {
        return MountState::Connecting;
    }
    if (value == QLatin1String("Offline")) {
        return MountState::Offline;
    }
    if (value == QLatin1String("AuthenticationRequired")) {
        return MountState::AuthenticationRequired;
    }
    if (value == QLatin1String("Degraded")) {
        return MountState::Degraded;
    }
    if (value == QLatin1String("Conflict")) {
        return MountState::Conflict;
    }
    if (value == QLatin1String("Error")) {
        return MountState::Error;
    }
    if (value == QLatin1String("Disconnecting")) {
        return MountState::Disconnecting;
    }
    return MountState::Disabled;
}

ItemSyncState syncStateFromString(const QString &value)
{
    if (value == QLatin1String("InSync")) {
        return ItemSyncState::InSync;
    }
    if (value == QLatin1String("Hydrating")) {
        return ItemSyncState::Hydrating;
    }
    if (value == QLatin1String("Uploading")) {
        return ItemSyncState::Uploading;
    }
    if (value == QLatin1String("OutOfSync")) {
        return ItemSyncState::OutOfSync;
    }
    if (value == QLatin1String("Pinned")) {
        return ItemSyncState::Pinned;
    }
    if (value == QLatin1String("Conflict")) {
        return ItemSyncState::Conflict;
    }
    if (value == QLatin1String("DeletedRemote")) {
        return ItemSyncState::DeletedRemote;
    }
    if (value == QLatin1String("DeletedLocal")) {
        return ItemSyncState::DeletedLocal;
    }
    return ItemSyncState::OnlineOnly;
}

OperationState operationStateFromString(const QString &value)
{
    if (value == QLatin1String("SubmittedRemote")) {
        return OperationState::SubmittedRemote;
    }
    if (value == QLatin1String("AcknowledgedCf")) {
        return OperationState::AcknowledgedCf;
    }
    if (value == QLatin1String("Committed")) {
        return OperationState::Committed;
    }
    if (value == QLatin1String("Compensating")) {
        return OperationState::Compensating;
    }
    if (value == QLatin1String("Failed")) {
        return OperationState::Failed;
    }
    return OperationState::Prepared;
}

QString operationStateToString(OperationState state)
{
    switch (state) {
    case OperationState::Prepared:
        return QStringLiteral("Prepared");
    case OperationState::SubmittedRemote:
        return QStringLiteral("SubmittedRemote");
    case OperationState::AcknowledgedCf:
        return QStringLiteral("AcknowledgedCf");
    case OperationState::Committed:
        return QStringLiteral("Committed");
    case OperationState::Compensating:
        return QStringLiteral("Compensating");
    case OperationState::Failed:
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Prepared");
}

} // namespace

MetadataStore::MetadataStore(const QString &databasePath)
    : m_databasePath(databasePath)
    , m_connectionName(QStringLiteral("kiodrive_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

MetadataStore::~MetadataStore()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool MetadataStore::open()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_databasePath);
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        qCWarning(KIODRIVE_LOG) << "Failed to open metadata database:" << m_lastError;
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    return migrate();
}

bool MetadataStore::isOpen() const
{
    return m_db.isOpen();
}

QString MetadataStore::lastError() const
{
    return m_lastError;
}

bool MetadataStore::beginImmediate()
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool MetadataStore::commit()
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("COMMIT"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool MetadataStore::rollback()
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("ROLLBACK"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool MetadataStore::migrate()
{
    const int current = schemaVersion();
    const QStringList statements = Schema::migrationSql(current);
    for (const QString &sql : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            m_lastError = query.lastError().text();
            qCWarning(KIODRIVE_LOG) << "Schema migration failed:" << m_lastError << sql;
            return false;
        }
    }

    if (current < Schema::currentVersion()) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("INSERT OR REPLACE INTO schema_migrations(version, applied_at) VALUES(?, ?)"));
        query.addBindValue(Schema::currentVersion());
        query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            return false;
        }
    }
    return true;
}

int MetadataStore::schemaVersion() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='schema_migrations'"))) {
        return 0;
    }
    if (!query.next()) {
        return 0;
    }
    QSqlQuery versionQuery(m_db);
    if (!versionQuery.exec(QStringLiteral("SELECT MAX(version) FROM schema_migrations")) || !versionQuery.next()) {
        return 0;
    }
    return versionQuery.value(0).toInt();
}

bool MetadataStore::upsertMount(const MountInfo &mount)
{
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO mounts(id, name, local_relpath, remote_url_canonical, protocol, mode, state, "
        "caps_json, ttl_seconds, created_at, updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "name=excluded.name, local_relpath=excluded.local_relpath, "
        "remote_url_canonical=excluded.remote_url_canonical, protocol=excluded.protocol, "
        "mode=excluded.mode, state=excluded.state, caps_json=excluded.caps_json, "
        "updated_at=excluded.updated_at"));
    query.addBindValue(mount.id.toString(QUuid::WithoutBraces));
    query.addBindValue(mount.name);
    query.addBindValue(mount.localRelativePath);
    query.addBindValue(PathUtils::canonicalizeRemoteUrl(mount.remoteUrl).toString());
    query.addBindValue(mount.protocol);
    query.addBindValue(mount.readOnly ? QStringLiteral("readonly") : QStringLiteral("readwrite"));
    query.addBindValue(mountStateToString(mount.state));
    query.addBindValue(QString());
    query.addBindValue(30);
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool MetadataStore::removeMount(const QUuid &mountId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM mounts WHERE id=?"));
    query.addBindValue(mountId.toString(QUuid::WithoutBraces));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

MountInfo MetadataStore::mountFromQuery(const QSqlQuery &query) const
{
    MountInfo mount;
    mount.id = QUuid::fromString(query.value(QStringLiteral("id")).toString());
    mount.name = query.value(QStringLiteral("name")).toString();
    mount.localRelativePath = query.value(QStringLiteral("local_relpath")).toString();
    mount.remoteUrl = QUrl(query.value(QStringLiteral("remote_url_canonical")).toString());
    mount.protocol = query.value(QStringLiteral("protocol")).toString();
    mount.readOnly = query.value(QStringLiteral("mode")).toString() == QLatin1String("readonly");
    mount.state = mountStateFromString(query.value(QStringLiteral("state")).toString());
    return mount;
}

std::optional<MountInfo> MetadataStore::mountById(const QUuid &id) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT * FROM mounts WHERE id=?"));
    query.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return mountFromQuery(query);
}

std::optional<MountInfo> MetadataStore::mountByLocalRelPath(const QString &relPath) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT * FROM mounts WHERE local_relpath=?"));
    query.addBindValue(relPath);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return mountFromQuery(query);
}

QVector<MountInfo> MetadataStore::allMounts() const
{
    QVector<MountInfo> mounts;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT * FROM mounts ORDER BY name"))) {
        return mounts;
    }
    while (query.next()) {
        mounts.push_back(mountFromQuery(query));
    }
    return mounts;
}

bool MetadataStore::upsertItem(const ItemRecord &item)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO items(id, mount_id, parent_id, remote_path, remote_name, windows_name, kind, mime, "
        "size, mtime, remote_inode, revision_token, revision_seq, sync_state, pin_state, hydrate_state, "
        "metadata_version) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "parent_id=excluded.parent_id, remote_path=excluded.remote_path, remote_name=excluded.remote_name, "
        "windows_name=excluded.windows_name, kind=excluded.kind, mime=excluded.mime, size=excluded.size, "
        "mtime=excluded.mtime, remote_inode=excluded.remote_inode, revision_token=excluded.revision_token, "
        "revision_seq=excluded.revision_seq, sync_state=excluded.sync_state, "
        "metadata_version=excluded.metadata_version"));
    query.addBindValue(item.id.toString(QUuid::WithoutBraces));
    query.addBindValue(item.mountId.toString(QUuid::WithoutBraces));
    query.addBindValue(item.parentId ? item.parentId->toString(QUuid::WithoutBraces) : QVariant());
    query.addBindValue(item.remotePath);
    query.addBindValue(item.remoteName);
    query.addBindValue(item.windowsName);
    query.addBindValue(kindToString(item.kind));
    query.addBindValue(item.mime);
    query.addBindValue(item.size);
    query.addBindValue(item.mtime);
    query.addBindValue(item.remoteInode);
    query.addBindValue(item.revisionToken);
    query.addBindValue(static_cast<qulonglong>(item.revisionSeq));
    query.addBindValue(itemSyncStateToString(item.syncState));
    query.addBindValue(QVariant());
    query.addBindValue(QVariant());
    query.addBindValue(item.metadataVersion);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

ItemRecord MetadataStore::itemFromQuery(const QSqlQuery &query) const
{
    ItemRecord item;
    item.id = QUuid::fromString(query.value(QStringLiteral("id")).toString());
    item.mountId = QUuid::fromString(query.value(QStringLiteral("mount_id")).toString());
    const QString parent = query.value(QStringLiteral("parent_id")).toString();
    if (!parent.isEmpty()) {
        item.parentId = QUuid::fromString(parent);
    }
    item.remotePath = query.value(QStringLiteral("remote_path")).toString();
    item.remoteName = query.value(QStringLiteral("remote_name")).toString();
    item.windowsName = query.value(QStringLiteral("windows_name")).toString();
    item.kind = kindFromString(query.value(QStringLiteral("kind")).toString());
    item.mime = query.value(QStringLiteral("mime")).toString();
    item.size = query.value(QStringLiteral("size")).toLongLong();
    item.mtime = query.value(QStringLiteral("mtime")).toLongLong();
    item.remoteInode = query.value(QStringLiteral("remote_inode")).toString();
    item.revisionToken = query.value(QStringLiteral("revision_token")).toString();
    item.revisionSeq = query.value(QStringLiteral("revision_seq")).toULongLong();
    item.syncState = syncStateFromString(query.value(QStringLiteral("sync_state")).toString());
    item.metadataVersion = query.value(QStringLiteral("metadata_version")).toInt();
    return item;
}

std::optional<ItemRecord> MetadataStore::itemById(const QUuid &id) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT * FROM items WHERE id=?"));
    query.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return itemFromQuery(query);
}

std::optional<ItemRecord> MetadataStore::itemByRemotePath(const QUuid &mountId, const QString &remotePath) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT * FROM items WHERE mount_id=? AND remote_path=?"));
    query.addBindValue(mountId.toString(QUuid::WithoutBraces));
    query.addBindValue(remotePath);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return itemFromQuery(query);
}

QVector<ItemRecord> MetadataStore::childrenOf(const QUuid &parentId) const
{
    QVector<ItemRecord> items;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT * FROM items WHERE parent_id=?"));
    query.addBindValue(parentId.toString(QUuid::WithoutBraces));
    if (!query.exec()) {
        return items;
    }
    while (query.next()) {
        items.push_back(itemFromQuery(query));
    }
    return items;
}

QVector<ItemRecord> MetadataStore::itemsWithRemotePathPrefix(const QUuid &mountId, const QString &prefix) const
{
    QVector<ItemRecord> items;
    QString normalized = prefix;
    while (normalized.size() > 1 && normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT * FROM items WHERE mount_id=? AND (remote_path=? OR remote_path LIKE ?)"));
    query.addBindValue(mountId.toString(QUuid::WithoutBraces));
    query.addBindValue(normalized);
    query.addBindValue(QString(normalized + QLatin1String("/%")));
    if (!query.exec()) {
        return items;
    }
    while (query.next()) {
        items.push_back(itemFromQuery(query));
    }
    return items;
}

bool MetadataStore::updateItemSyncState(const QUuid &itemId, ItemSyncState state)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE items SET sync_state=? WHERE id=?"));
    query.addBindValue(itemSyncStateToString(state));
    query.addBindValue(itemId.toString(QUuid::WithoutBraces));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool MetadataStore::insertOperation(const OperationRecord &op)
{
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO operations(id, mount_id, item_id, type, state, payload_json, idempotency_key, "
        "created_at, updated_at) VALUES(?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(op.id.toString(QUuid::WithoutBraces));
    query.addBindValue(op.mountId.toString(QUuid::WithoutBraces));
    query.addBindValue(op.itemId.toString(QUuid::WithoutBraces));
    query.addBindValue(op.type);
    query.addBindValue(operationStateToString(op.state));
    query.addBindValue(op.payloadJson);
    query.addBindValue(op.idempotencyKey);
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool MetadataStore::updateOperationState(const QUuid &opId, OperationState state)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE operations SET state=?, updated_at=? WHERE id=?"));
    query.addBindValue(operationStateToString(state));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(opId.toString(QUuid::WithoutBraces));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QVector<OperationRecord> MetadataStore::incompleteOperations() const
{
    QVector<OperationRecord> ops;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT * FROM operations WHERE state NOT IN ('Committed','Failed') ORDER BY created_at"))) {
        return ops;
    }
    while (query.next()) {
        OperationRecord op;
        op.id = QUuid::fromString(query.value(QStringLiteral("id")).toString());
        op.mountId = QUuid::fromString(query.value(QStringLiteral("mount_id")).toString());
        op.itemId = QUuid::fromString(query.value(QStringLiteral("item_id")).toString());
        op.type = query.value(QStringLiteral("type")).toString();
        op.state = operationStateFromString(query.value(QStringLiteral("state")).toString());
        op.payloadJson = query.value(QStringLiteral("payload_json")).toString();
        op.idempotencyKey = query.value(QStringLiteral("idempotency_key")).toString();
        ops.push_back(op);
    }
    return ops;
}

} // namespace KIODrive

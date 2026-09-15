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

#include "operation_journal.h"

#include "common/logging.h"
#include "kio/kio_backend.h"
#include "mounts/mount_registry.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUuid>

namespace KIODrive
{

OperationJournal::OperationJournal(MetadataStore *store,
                                   KioBackend *backend,
                                   MountRegistry *mounts,
                                   QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_backend(backend)
    , m_mounts(mounts)
{
}

QUuid OperationJournal::beginRename(const QUuid &mountId,
                                    const QUuid &itemId,
                                    const QString &srcPath,
                                    const QString &destPath)
{
    OperationRecord op;
    op.id = QUuid::createUuid();
    op.mountId = mountId;
    op.itemId = itemId;
    op.type = QStringLiteral("rename");
    op.state = OperationState::Prepared;
    op.idempotencyKey = QStringLiteral("rename:%1:%2").arg(srcPath, destPath);
    QJsonObject payload;
    payload.insert(QStringLiteral("src"), srcPath);
    payload.insert(QStringLiteral("dest"), destPath);
    op.payloadJson = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_store->insertOperation(op);

    qCInfo(KIODRIVE_SYNC_LOG) << "Remote rename begin" << srcPath << "->" << destPath << "op" << op.id
                              << "itemId" << itemId;

    const auto mount = m_mounts->mount(mountId);
    if (!mount || !m_backend) {
        qCWarning(KIODRIVE_SYNC_LOG) << "Remote rename failed: mount or backend unavailable" << op.id;
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, QStringLiteral("Mount or backend unavailable"));
        return op.id;
    }

    QUrl src = mount->remoteUrl;
    src.setPath(srcPath);
    QUrl dest = mount->remoteUrl;
    dest.setPath(destPath);

    m_backend->setMountId(mountId);
    m_store->updateOperationState(op.id, OperationState::SubmittedRemote);

    if (!ensureRemoteParents(mountId, destPath)) {
        qCWarning(KIODRIVE_SYNC_LOG) << "Remote rename failed: cannot create destination parents" << destPath;
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, QStringLiteral("Cannot create destination parents"));
        return op.id;
    }

    const BackendResult result = m_backend->renameSync(src, dest);
    if (!result.ok) {
        // CREATE-before-REMOVE folder moves may mkdir an empty destination first.
        // Remove the conflicting dest and retry once so the source tree is preserved.
        const BackendResult destStat = m_backend->statSync(dest);
        const BackendResult srcStat = m_backend->statSync(src);
        qCWarning(KIODRIVE_SYNC_LOG) << "Remote rename first attempt failed" << result.errorString
                                     << "srcExists" << srcStat.ok << "destExists" << destStat.ok;
        if (destStat.ok && srcStat.ok) {
            const bool destIsDir = destStat.stat.kind == ItemKind::Directory;
            qCWarning(KIODRIVE_SYNC_LOG) << "Rename conflict; removing destination and retrying"
                                         << "destIsDir" << destIsDir << result.errorString;
            const BackendResult delDest = m_backend->delSync(dest, !destIsDir);
            if (delDest.ok) {
                const BackendResult retry = m_backend->renameSync(src, dest);
                if (!retry.ok) {
                    qCWarning(KIODRIVE_SYNC_LOG) << "Remote rename retry failed" << retry.errorString;
                    m_store->updateOperationState(op.id, OperationState::Failed);
                    Q_EMIT operationFailed(op.id, retry.errorString);
                    return op.id;
                }
                qCInfo(KIODRIVE_SYNC_LOG) << "Remote rename retry succeeded" << srcPath << "->" << destPath;
            } else if (!destIsDir) {
                // File race: dest already uploaded; drop source to finish the move.
                qCWarning(KIODRIVE_SYNC_LOG) << "Rename conflict: delete dest failed; deleting source instead"
                                             << delDest.errorString;
                const BackendResult delSrc = m_backend->delSync(src, true);
                if (!delSrc.ok) {
                    qCWarning(KIODRIVE_SYNC_LOG) << "Remote rename fallback delete-source failed"
                                                 << delSrc.errorString;
                    m_store->updateOperationState(op.id, OperationState::Failed);
                    Q_EMIT operationFailed(op.id, delSrc.errorString);
                    return op.id;
                }
            } else {
                qCWarning(KIODRIVE_SYNC_LOG) << "Remote rename conflict: cannot delete dest dir"
                                             << delDest.errorString;
                m_store->updateOperationState(op.id, OperationState::Failed);
                Q_EMIT operationFailed(op.id, delDest.errorString);
                return op.id;
            }
        } else {
            qCWarning(KIODRIVE_SYNC_LOG) << "Remote rename failed without recoverable conflict"
                                         << result.errorString;
            m_store->updateOperationState(op.id, OperationState::Failed);
            Q_EMIT operationFailed(op.id, result.errorString);
            return op.id;
        }
    } else {
        qCInfo(KIODRIVE_SYNC_LOG) << "Remote rename succeeded" << srcPath << "->" << destPath;
    }

    QString oldPrefix = srcPath;
    while (oldPrefix.size() > 1 && oldPrefix.endsWith(QLatin1Char('/'))) {
        oldPrefix.chop(1);
    }
    QString newPrefix = destPath;
    while (newPrefix.size() > 1 && newPrefix.endsWith(QLatin1Char('/'))) {
        newPrefix.chop(1);
    }
    const QVector<ItemRecord> affected = m_store->itemsWithRemotePathPrefix(mountId, oldPrefix);
    int rewritten = 0;
    for (ItemRecord item : affected) {
        const QString before = item.remotePath;
        if (item.remotePath == oldPrefix) {
            item.remotePath = newPrefix;
        } else if (item.remotePath.startsWith(oldPrefix + QLatin1Char('/'))) {
            item.remotePath = newPrefix + item.remotePath.mid(oldPrefix.size());
        } else {
            continue;
        }
        item.remoteName = item.remotePath.section(QLatin1Char('/'), -1);
        item.windowsName = item.remoteName;
        m_store->upsertItem(item);
        ++rewritten;
        qCDebug(KIODRIVE_SYNC_LOG) << "Rewrote child remotePath" << before << "->" << item.remotePath;
    }
    if (affected.isEmpty()) {
        if (auto item = m_store->itemById(itemId)) {
            item->remotePath = destPath;
            item->remoteName = destPath.section(QLatin1Char('/'), -1);
            item->windowsName = item->remoteName;
            m_store->upsertItem(*item);
            rewritten = 1;
            qCInfo(KIODRIVE_SYNC_LOG) << "Rewrote single item remotePath via itemId" << destPath;
        }
    }
    qCInfo(KIODRIVE_SYNC_LOG) << "Remote rename committed" << srcPath << "->" << destPath
                              << "metadataRewrites" << rewritten << "prefixHits" << affected.size();
    m_store->updateOperationState(op.id, OperationState::Committed);
    Q_EMIT operationCompleted(op.id);
    return op.id;
}

QUuid OperationJournal::beginDelete(const QUuid &mountId, const QUuid &itemId, const QString &remotePath)
{
    OperationRecord op;
    op.id = QUuid::createUuid();
    op.mountId = mountId;
    op.itemId = itemId;
    op.type = QStringLiteral("delete");
    op.state = OperationState::Prepared;
    op.idempotencyKey = QStringLiteral("delete:%1").arg(remotePath);
    QJsonObject payload;
    payload.insert(QStringLiteral("path"), remotePath);
    op.payloadJson = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_store->insertOperation(op);

    const auto mount = m_mounts->mount(mountId);
    if (!mount || !m_backend) {
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, QStringLiteral("Mount or backend unavailable"));
        return op.id;
    }

    QUrl url = mount->remoteUrl;
    url.setPath(remotePath);

    bool isFile = true;
    if (const auto item = m_store->itemById(itemId)) {
        isFile = item->kind != ItemKind::Directory;
    }

    m_backend->setMountId(mountId);
    m_store->updateOperationState(op.id, OperationState::SubmittedRemote);
    const BackendResult result = m_backend->delSync(url, isFile);
    if (!result.ok) {
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, result.errorString);
        return op.id;
    }
    m_store->updateItemSyncState(itemId, ItemSyncState::DeletedRemote);
    m_store->updateOperationState(op.id, OperationState::Committed);
    Q_EMIT operationCompleted(op.id);
    return op.id;
}

QUuid OperationJournal::beginUpload(const QUuid &mountId,
                                    const QUuid &itemId,
                                    const QString &localPath,
                                    const QString &remotePath)
{
    OperationRecord op;
    op.id = QUuid::createUuid();
    op.mountId = mountId;
    op.itemId = itemId;
    op.type = QStringLiteral("upload");
    op.state = OperationState::Prepared;
    op.idempotencyKey = QStringLiteral("upload:%1:%2").arg(itemId.toString(QUuid::WithoutBraces), remotePath);
    QJsonObject payload;
    payload.insert(QStringLiteral("local"), localPath);
    payload.insert(QStringLiteral("remote"), remotePath);
    op.payloadJson = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_store->insertOperation(op);

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, QStringLiteral("Cannot read local file"));
        return op.id;
    }
    const QByteArray data = file.readAll();
    file.close();

    const auto mount = m_mounts->mount(mountId);
    if (!mount || !m_backend) {
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, QStringLiteral("Mount or backend unavailable"));
        return op.id;
    }

    QUrl url = mount->remoteUrl;
    url.setPath(remotePath);

    m_backend->setMountId(mountId);
    m_store->updateItemSyncState(itemId, ItemSyncState::Uploading);
    m_store->updateOperationState(op.id, OperationState::SubmittedRemote);

    if (!ensureRemoteParents(mountId, remotePath)) {
        m_store->updateItemSyncState(itemId, ItemSyncState::OutOfSync);
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, QStringLiteral("Cannot create destination parents"));
        return op.id;
    }

    const BackendResult result = m_backend->putSync(url, data);
    if (!result.ok) {
        m_store->updateItemSyncState(itemId, ItemSyncState::OutOfSync);
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, result.errorString);
        return op.id;
    }
    m_store->updateItemSyncState(itemId, ItemSyncState::InSync);
    m_store->updateOperationState(op.id, OperationState::Committed);
    Q_EMIT operationCompleted(op.id);
    return op.id;
}

bool OperationJournal::ensureRemoteParents(const QUuid &mountId, const QString &remotePath)
{
    const auto mount = m_mounts->mount(mountId);
    if (!mount || !m_backend) {
        return false;
    }

    QString path = remotePath;
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0) {
        return true;
    }
    QString parent = path.left(slash);
    if (parent.isEmpty()) {
        parent = QStringLiteral("/");
    }

    // Walk from mount root down, creating missing directories.
    const QString root = mount->remoteUrl.path();
    QString prefix = root;
    while (prefix.endsWith(QLatin1Char('/'))) {
        prefix.chop(1);
    }

    QString relative = parent;
    if (relative.startsWith(prefix)) {
        relative = relative.mid(prefix.size());
        if (relative.startsWith(QLatin1Char('/'))) {
            relative = relative.mid(1);
        }
    }

    if (relative.isEmpty()) {
        return true;
    }

    QString current = prefix;
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    m_backend->setMountId(mountId);
    for (const QString &part : parts) {
        if (!current.endsWith(QLatin1Char('/'))) {
            current += QLatin1Char('/');
        }
        current += part;

        QUrl url = mount->remoteUrl;
        url.setPath(current);
        const BackendResult st = m_backend->statSync(url);
        if (st.ok) {
            continue;
        }
        const BackendResult mk = m_backend->mkdirSync(url);
        if (!mk.ok) {
            qCWarning(KIODRIVE_SYNC_LOG) << "ensureRemoteParents mkdir failed" << current << mk.errorString;
            return false;
        }
        qCInfo(KIODRIVE_SYNC_LOG) << "Created remote directory" << current;
    }
    return true;
}

QUuid OperationJournal::beginMkdir(const QUuid &mountId, const QUuid &itemId, const QString &remotePath)
{
    OperationRecord op;
    op.id = QUuid::createUuid();
    op.mountId = mountId;
    op.itemId = itemId;
    op.type = QStringLiteral("mkdir");
    op.state = OperationState::Prepared;
    op.idempotencyKey = QStringLiteral("mkdir:%1").arg(remotePath);
    QJsonObject payload;
    payload.insert(QStringLiteral("path"), remotePath);
    op.payloadJson = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_store->insertOperation(op);

    if (!ensureRemoteParents(mountId, remotePath)) {
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, QStringLiteral("Cannot create parent directories"));
        return op.id;
    }

    const auto mount = m_mounts->mount(mountId);
    if (!mount || !m_backend) {
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, QStringLiteral("Mount or backend unavailable"));
        return op.id;
    }

    QUrl url = mount->remoteUrl;
    url.setPath(remotePath);

    m_backend->setMountId(mountId);
    m_store->updateOperationState(op.id, OperationState::SubmittedRemote);

    const BackendResult existing = m_backend->statSync(url);
    if (existing.ok) {
        m_store->updateItemSyncState(itemId, ItemSyncState::InSync);
        m_store->updateOperationState(op.id, OperationState::Committed);
        Q_EMIT operationCompleted(op.id);
        return op.id;
    }

    const BackendResult result = m_backend->mkdirSync(url);
    if (!result.ok) {
        m_store->updateOperationState(op.id, OperationState::Failed);
        Q_EMIT operationFailed(op.id, result.errorString);
        return op.id;
    }
    m_store->updateItemSyncState(itemId, ItemSyncState::InSync);
    m_store->updateOperationState(op.id, OperationState::Committed);
    Q_EMIT operationCompleted(op.id);
    return op.id;
}

void OperationJournal::recoverIncomplete()
{
    if (!m_store) {
        return;
    }
    const QVector<OperationRecord> ops = m_store->incompleteOperations();
    for (const OperationRecord &op : ops) {
        qCWarning(KIODRIVE_SYNC_LOG) << "Incomplete operation after crash" << op.id << op.type
                                     << static_cast<int>(op.state)
                                     << "- marking for retry, not assuming remote success";
        if (op.state == OperationState::SubmittedRemote) {
            // Safe default: mark failed so UI can retry after verifying remote state.
            m_store->updateOperationState(op.id, OperationState::Failed);
        }
    }
}

} // namespace KIODrive

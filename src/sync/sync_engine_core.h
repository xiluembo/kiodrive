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

#ifndef KIODRIVE_SYNC_SYNC_ENGINE_CORE_H
#define KIODRIVE_SYNC_SYNC_ENGINE_CORE_H

#include "cloudfiles/placeholder_manager.h"
#include "local_change_monitor.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUuid>
#include <QVector>

#include <memory>

namespace KIODrive
{

class MetadataStore;
class MountRegistry;
class SyncRootManager;
class PlaceholderManager;
class CloudFilesCallbacks;
class HydrationManager;
class KioBackend;
class CredentialBroker;
class TransferManager;
class RemoteReconciler;
class ConflictResolver;
class OperationJournal;

class SyncEngineCore : public QObject
{
    Q_OBJECT

public:
    explicit SyncEngineCore(QObject *parent = nullptr);
    ~SyncEngineCore() override;

    bool start();
    void stop();

    MetadataStore *metadataStore() const;
    MountRegistry *mountRegistry() const;
    OperationJournal *operationJournal() const;
    TransferManager *transferManager() const;
    KioBackend *backend() const;
    CredentialBroker *credentialBroker() const;
    PlaceholderManager *placeholderManager() const;
    SyncRootManager *syncRootManager() const;

    /** Ensure each mount appears as a directory placeholder under the sync root. */
    void materializeMountRoots();

public Q_SLOTS:
    void onPlaceholdersRequested(const QString &relativePath);
    void onPlaceholdersFetchRequested(const QString &relativePath, QVector<PlaceholderCreateRequest> *outItems);
    void onHydrationRequested(const QString &relativePath, quint64 offset, quint64 length);
    /** CFAPI NOTIFY_RENAME: suppress FS noise; do not sync remotely yet. */
    void onRenameRequested(const QString &sourceRelativePath,
                           const QString &targetRelativePath,
                           bool isDirectory,
                           bool sourceInScope,
                           bool targetInScope);
    void onDeleteRequested(const QString &relativePath);
    void onLocalChange(const LocalChange &change);
    void onFileClosed(const QString &relativePath);
    void onDehydrateRequested(const QString &relativePath);
    void onDehydrateCompleted(const QString &relativePath);
    void onDeleteCompleted(const QString &relativePath);
    /** CFAPI NOTIFY_RENAME_COMPLETION: apply remote rename/delete/ingest. */
    void onRenameCompleted(const QString &sourceRelativePath,
                           const QString &targetRelativePath,
                           bool isDirectory,
                           bool sourceInScope,
                           bool targetInScope);

private:
    struct PendingRemove {
        QString absolutePath;
        QString relativePath;
        QUuid mountId;
        QUuid itemId;
        QString remotePath;
        QString fileName;
        qint64 size = -1;
        bool isDirectory = false;
    };

    struct PendingCreate {
        QString absolutePath;
        QString relativePath;
        QUuid mountId;
        QUuid itemId;
        QString remotePath;
        QString fileName;
        bool isDirectory = false;
    };

    struct PendingCfRename {
        QString sourceRelativePath;
        QString targetRelativePath;
        bool isDirectory = false;
        bool sourceInScope = false;
        bool targetInScope = false;
    };

    QString databasePath() const;
    bool resolveRelativeToItem(const QString &relativePath, QUuid *mountId, QUuid *itemId, QString *remotePath) const;
    void maybeUploadAfterClose(const QString &relativePath);
    /** Shared remote rename used by CFAPI completion and FS create/remove correlation. */
    void scheduleRemoteRename(const QString &sourceRelativePath, const QString &targetRelativePath);
    bool tryConsumePendingRemoveAsMove(const QString &destAbsolutePath,
                                       const QUuid &destMountId,
                                       const QString &destRemotePath,
                                       qint64 destSize,
                                       bool destIsDirectory);
    bool tryConsumePendingCreateAsMove(const PendingRemove &removed);
    void flushPendingRemove(const QString &absolutePath);
    void flushPendingCreate(const QString &absolutePath);
    void rememberPendingRemove(const PendingRemove &pending);
    void rememberPendingCreate(const PendingCreate &pending);

    std::unique_ptr<MetadataStore> m_store;
    std::unique_ptr<SyncRootManager> m_syncRoot;
    std::unique_ptr<MountRegistry> m_mounts;
    std::unique_ptr<CredentialBroker> m_credentials;
    std::unique_ptr<KioBackend> m_backend;
    std::unique_ptr<PlaceholderManager> m_placeholders;
    std::unique_ptr<HydrationManager> m_hydration;
    std::unique_ptr<CloudFilesCallbacks> m_callbacks;
    std::unique_ptr<TransferManager> m_transfers;
    std::unique_ptr<LocalChangeMonitor> m_localMonitor;
    std::unique_ptr<RemoteReconciler> m_reconciler;
    std::unique_ptr<ConflictResolver> m_conflicts;
    std::unique_ptr<OperationJournal> m_journal;
    QHash<QString, PendingRemove> m_pendingRemoves;
    QHash<QString, PendingCreate> m_pendingCreates;
    QHash<QString, PendingCfRename> m_pendingCfRenames;
    QSet<QString> m_inflightRemoteRenames;
};

} // namespace KIODrive

#endif // KIODRIVE_SYNC_SYNC_ENGINE_CORE_H

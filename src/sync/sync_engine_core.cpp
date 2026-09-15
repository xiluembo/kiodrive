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

#include "sync_engine_core.h"

#include "cloudfiles/cloud_files_callbacks.h"
#include "cloudfiles/hydration_manager.h"
#include "cloudfiles/placeholder_manager.h"
#include "cloudfiles/sync_root_manager.h"
#include "common/logging.h"
#include "common/path_utils.h"
#include "conflict_resolver.h"
#include "kio/credential_broker.h"
#include "kio/kio_backend.h"
#include "local_change_monitor.h"
#include "metadata/metadata_store.h"
#include "mounts/mount_registry.h"
#include "operation_journal.h"
#include "remote_reconciler.h"
#include "transfer_manager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace KIODrive
{

SyncEngineCore::SyncEngineCore(QObject *parent)
    : QObject(parent)
{
}

SyncEngineCore::~SyncEngineCore()
{
    stop();
}

QString SyncEngineCore::databasePath() const
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    return QDir(root).filePath(QStringLiteral("metadata.sqlite"));
}

bool SyncEngineCore::start()
{
    m_store = std::make_unique<MetadataStore>(databasePath());
    if (!m_store->open()) {
        qCCritical(KIODRIVE_LOG) << "Cannot open metadata store";
        return false;
    }

    m_syncRoot = std::make_unique<SyncRootManager>();
    if (!m_syncRoot->ensureSyncRootDirectory()) {
        return false;
    }
    // Registration may fail on unpackaged builds without identity; directory still usable for tests.
    m_syncRoot->registerSyncRoot();

    m_mounts = std::make_unique<MountRegistry>(m_store.get(), m_syncRoot->syncRootPath());
    m_mounts->rebuildRouter();

    m_credentials = std::make_unique<CredentialBroker>();
    m_backend = std::make_unique<KioBackend>();
    m_backend->setCredentialBroker(m_credentials.get());
    m_placeholders = std::make_unique<PlaceholderManager>(m_syncRoot->syncRootPath());
    m_hydration = std::make_unique<HydrationManager>(m_store.get(), m_mounts.get(), m_backend.get());
    m_localMonitor = std::make_unique<LocalChangeMonitor>();
    m_callbacks = std::make_unique<CloudFilesCallbacks>(m_store.get(),
                                                        m_mounts.get(),
                                                        m_backend.get(),
                                                        m_placeholders.get(),
                                                        m_hydration.get(),
                                                        m_syncRoot->syncRootPath());
    m_transfers = std::make_unique<TransferManager>();
    m_reconciler =
        std::make_unique<RemoteReconciler>(m_store.get(), m_mounts.get(), m_backend.get(), m_placeholders.get());
    m_conflicts = std::make_unique<ConflictResolver>(m_store.get());
    m_journal = std::make_unique<OperationJournal>(m_store.get(), m_backend.get(), m_mounts.get());

    connect(m_callbacks.get(), &CloudFilesCallbacks::placeholdersRequested, this, &SyncEngineCore::onPlaceholdersRequested);
    connect(m_callbacks.get(),
            &CloudFilesCallbacks::placeholdersFetchRequested,
            this,
            &SyncEngineCore::onPlaceholdersFetchRequested,
            Qt::DirectConnection);
    connect(m_callbacks.get(), &CloudFilesCallbacks::hydrationRequested, this, &SyncEngineCore::onHydrationRequested);
    connect(m_callbacks.get(), &CloudFilesCallbacks::hydrationStarted, this, [this](const QString &path) {
        if (m_localMonitor) {
            m_localMonitor->suppressPath(path);
        }
    });
    connect(m_callbacks.get(), &CloudFilesCallbacks::hydrationFinished, this, [this](const QString &path) {
        if (m_localMonitor) {
            // Keep suppressed briefly — CFAPI may still touch timestamps after TRANSFER_DATA.
            QTimer::singleShot(750, this, [this, path]() {
                if (m_localMonitor) {
                    m_localMonitor->unsuppressPath(path);
                }
            });
        }
    });
    connect(m_callbacks.get(), &CloudFilesCallbacks::renameRequested, this, &SyncEngineCore::onRenameRequested);
    connect(m_callbacks.get(), &CloudFilesCallbacks::deleteRequested, this, &SyncEngineCore::onDeleteRequested);
    connect(m_callbacks.get(), &CloudFilesCallbacks::fileClosed, this, &SyncEngineCore::onFileClosed);
    connect(m_callbacks.get(), &CloudFilesCallbacks::dehydrateRequested, this, &SyncEngineCore::onDehydrateRequested);
    connect(m_callbacks.get(), &CloudFilesCallbacks::dehydrateCompleted, this, &SyncEngineCore::onDehydrateCompleted);
    connect(m_callbacks.get(), &CloudFilesCallbacks::deleteCompleted, this, &SyncEngineCore::onDeleteCompleted);
    connect(m_callbacks.get(), &CloudFilesCallbacks::renameCompleted, this, &SyncEngineCore::onRenameCompleted);
    connect(m_localMonitor.get(), &LocalChangeMonitor::changeDetected, this, &SyncEngineCore::onLocalChange);

    m_journal->recoverIncomplete();
    m_localMonitor->start(m_syncRoot->syncRootPath());
#ifdef Q_OS_WIN
    if (!m_syncRoot->connectCallbacks(CloudFilesCallbacks::registrationTable(), m_callbacks->callbackContext())) {
        qCCritical(KIODRIVE_LOG) << "Cloud Files provider is not connected; Explorer cannot open"
                                 << m_syncRoot->syncRootPath();
        return false;
    }
#else
    m_syncRoot->connectCallbacks(nullptr, m_callbacks->callbackContext());
#endif

    // Recover from earlier empty DISABLED population of the sync root, then ensure mounts exist.
    m_placeholders->enableOnDemandPopulation();
    materializeMountRoots();

    qCInfo(KIODRIVE_LOG) << "KIODrive.SyncEngine started" << m_syncRoot->syncRootPath()
                         << "registered=" << m_syncRoot->isRegistered() << "connected=" << m_syncRoot->isConnected();
    return true;
}

void SyncEngineCore::stop()
{
    if (m_syncRoot) {
        m_syncRoot->disconnectCallbacks();
    }
    if (m_localMonitor) {
        m_localMonitor->stop();
    }
}

MetadataStore *SyncEngineCore::metadataStore() const
{
    return m_store.get();
}

MountRegistry *SyncEngineCore::mountRegistry() const
{
    return m_mounts.get();
}

OperationJournal *SyncEngineCore::operationJournal() const
{
    return m_journal.get();
}

TransferManager *SyncEngineCore::transferManager() const
{
    return m_transfers.get();
}

KioBackend *SyncEngineCore::backend() const
{
    return m_backend.get();
}

CredentialBroker *SyncEngineCore::credentialBroker() const
{
    return m_credentials.get();
}

PlaceholderManager *SyncEngineCore::placeholderManager() const
{
    return m_placeholders.get();
}

SyncRootManager *SyncEngineCore::syncRootManager() const
{
    return m_syncRoot.get();
}

void SyncEngineCore::materializeMountRoots()
{
    if (!m_placeholders || !m_mounts) {
        return;
    }
    m_placeholders->enableOnDemandPopulation();
    for (const MountInfo &mount : m_mounts->listMounts()) {
        if (!m_placeholders->ensureMountRootPlaceholder(mount)) {
            qCWarning(KIODRIVE_LOG) << "Failed to materialize mount root" << mount.localRelativePath;
        }
    }
}

bool SyncEngineCore::resolveRelativeToItem(const QString &relativePath,
                                           QUuid *mountId,
                                           QUuid *itemId,
                                           QString *remotePath) const
{
    const auto ownerId = m_mounts->router().findOwner(relativePath);
    if (!ownerId) {
        return false;
    }
    const auto mount = m_mounts->mount(*ownerId);
    if (!mount) {
        return false;
    }
    *mountId = mount->id;
    QString relInside = relativePath;
    if (relInside.startsWith(mount->localRelativePath)) {
        relInside = relInside.mid(mount->localRelativePath.size());
        if (relInside.startsWith(QLatin1Char('/'))) {
            relInside = relInside.mid(1);
        }
    }
    QString remote = mount->remoteUrl.path();
    if (!relInside.isEmpty()) {
        if (!remote.endsWith(QLatin1Char('/'))) {
            remote += QLatin1Char('/');
        }
        remote += relInside;
    }
    *remotePath = remote;
    if (const auto item = m_store->itemByRemotePath(mount->id, remote)) {
        *itemId = item->id;
        return true;
    }
    *itemId = QUuid();
    return true;
}

void SyncEngineCore::onPlaceholdersRequested(const QString &relativePath)
{
    const auto ownerId = m_mounts->router().findOwner(relativePath);
    if (!ownerId) {
        return;
    }
    const auto mount = m_mounts->mount(*ownerId);
    if (!mount) {
        return;
    }
    QString remote = mount->remoteUrl.path();
    QString relInside = relativePath;
    if (relInside.startsWith(mount->localRelativePath)) {
        relInside = relInside.mid(mount->localRelativePath.size());
        if (relInside.startsWith(QLatin1Char('/'))) {
            relInside = relInside.mid(1);
        }
    }
    if (!relInside.isEmpty()) {
        if (!remote.endsWith(QLatin1Char('/'))) {
            remote += QLatin1Char('/');
        }
        remote += relInside;
    }
    m_reconciler->refreshDirectory(mount->id, remote, relativePath);
}

void SyncEngineCore::onPlaceholdersFetchRequested(const QString &relativePath,
                                                  QVector<PlaceholderCreateRequest> *outItems)
{
    if (!outItems) {
        return;
    }
    outItems->clear();

    const auto ownerId = m_mounts->router().findOwner(relativePath);
    if (!ownerId) {
        return;
    }
    const auto mount = m_mounts->mount(*ownerId);
    if (!mount) {
        return;
    }
    QString remote = mount->remoteUrl.path();
    QString relInside = relativePath;
    if (relInside.startsWith(mount->localRelativePath)) {
        relInside = relInside.mid(mount->localRelativePath.size());
        if (relInside.startsWith(QLatin1Char('/'))) {
            relInside = relInside.mid(1);
        }
    }
    if (!relInside.isEmpty()) {
        if (!remote.endsWith(QLatin1Char('/'))) {
            remote += QLatin1Char('/');
        }
        remote += relInside;
    }
    // Metadata only — never CfCreatePlaceholders while FETCH_PLACEHOLDERS is outstanding.
    *outItems = m_reconciler->syncDirectoryEntries(mount->id, remote);
}

void SyncEngineCore::onHydrationRequested(const QString &relativePath, quint64 offset, quint64 length)
{
    // CFAPI completion (CfExecute TRANSFER_DATA) is owned by CloudFilesCallbacks::processFetchData.
    // This slot is only for observability / future UI progress.
    qCInfo(KIODRIVE_CFAPI_LOG) << "Hydration requested" << relativePath << offset << length;
}

void SyncEngineCore::onRenameRequested(const QString &sourceRelativePath,
                                       const QString &targetRelativePath,
                                       bool isDirectory,
                                       bool sourceInScope,
                                       bool targetInScope)
{
    qCInfo(KIODRIVE_SYNC_LOG) << "CFAPI rename authorize" << sourceRelativePath << "->" << targetRelativePath
                              << "isDir" << isDirectory << "sourceInScope" << sourceInScope << "targetInScope"
                              << targetInScope;

    // Remember scope for completion (RenameCompletion has no scope flags).
    if (!sourceRelativePath.isEmpty()) {
        PendingCfRename pending;
        pending.sourceRelativePath = sourceRelativePath;
        pending.targetRelativePath = targetRelativePath;
        pending.isDirectory = isDirectory;
        pending.sourceInScope = sourceInScope;
        pending.targetInScope = targetInScope;
        m_pendingCfRenames.insert(PathUtils::normalizedPathKey(sourceRelativePath), pending);
    }

    // Suppress FS watcher so REMOVE+ADD noise does not mkdir/delete while CFAPI owns the rename.
    if (m_localMonitor && m_syncRoot) {
        if (sourceInScope && !sourceRelativePath.isEmpty()) {
            m_localMonitor->suppressPath(QDir(m_syncRoot->syncRootPath()).filePath(sourceRelativePath));
        }
        if (targetInScope && !targetRelativePath.isEmpty()) {
            m_localMonitor->suppressPath(QDir(m_syncRoot->syncRootPath()).filePath(targetRelativePath));
        }
    }
}

void SyncEngineCore::scheduleRemoteRename(const QString &sourceRelativePath, const QString &targetRelativePath)
{
    qCInfo(KIODRIVE_SYNC_LOG) << "scheduleRemoteRename" << sourceRelativePath << "->" << targetRelativePath
                              << "pendingCreates" << m_pendingCreates.size() << "pendingRemoves"
                              << m_pendingRemoves.size();

    if (sourceRelativePath.isEmpty() || targetRelativePath.isEmpty()
        || sourceRelativePath.compare(targetRelativePath, Qt::CaseInsensitive) == 0) {
        qCWarning(KIODRIVE_SYNC_LOG) << "scheduleRemoteRename ignored (empty or identical paths)";
        return;
    }

    const QString dedupeKey =
        PathUtils::normalizedPathKey(sourceRelativePath) + QLatin1Char('>')
        + PathUtils::normalizedPathKey(targetRelativePath);
    if (m_inflightRemoteRenames.contains(dedupeKey)) {
        qCInfo(KIODRIVE_SYNC_LOG) << "scheduleRemoteRename skipped (already in flight)" << dedupeKey;
        return;
    }

    QUuid mountId;
    QUuid itemId;
    QString srcRemote;
    if (!resolveRelativeToItem(sourceRelativePath, &mountId, &itemId, &srcRemote)) {
        qCWarning(KIODRIVE_SYNC_LOG) << "Rename source not under a mount" << sourceRelativePath;
        return;
    }
    if (itemId.isNull()) {
        if (const auto item = m_store->itemByRemotePath(mountId, srcRemote)) {
            itemId = item->id;
            qCInfo(KIODRIVE_SYNC_LOG) << "Rename resolved itemId via remotePath" << srcRemote << itemId;
        } else {
            qCWarning(KIODRIVE_SYNC_LOG) << "Rename source has no itemId yet" << sourceRelativePath << srcRemote;
        }
    }

    QUuid mountId2;
    QUuid itemId2;
    QString destRemote;
    if (resolveRelativeToItem(targetRelativePath, &mountId2, &itemId2, &destRemote)) {
        if (mountId != mountId2) {
            qCWarning(KIODRIVE_SYNC_LOG) << "Cross-mount rename requires copy+delete";
            return;
        }
    } else {
        // Target is not yet indexed; keep the same mount and rebuild the remote path.
        const auto mount = m_mounts->mount(mountId);
        if (!mount) {
            return;
        }
        QString relInside = targetRelativePath;
        if (relInside.startsWith(mount->localRelativePath, Qt::CaseInsensitive)) {
            relInside = relInside.mid(mount->localRelativePath.size());
            if (relInside.startsWith(QLatin1Char('/'))) {
                relInside = relInside.mid(1);
            }
        }
        destRemote = mount->remoteUrl.path();
        if (!relInside.isEmpty()) {
            if (!destRemote.endsWith(QLatin1Char('/'))) {
                destRemote += QLatin1Char('/');
            }
            destRemote += relInside;
        }
        qCInfo(KIODRIVE_SYNC_LOG) << "Rename dest rebuilt from mount path" << destRemote;
    }

    if (srcRemote == destRemote) {
        qCWarning(KIODRIVE_SYNC_LOG) << "scheduleRemoteRename ignored (same remote path)" << srcRemote;
        return;
    }

    const QString absSrc = QDir(m_syncRoot->syncRootPath()).filePath(sourceRelativePath);
    const QString absDst = QDir(m_syncRoot->syncRootPath()).filePath(targetRelativePath);
    if (m_localMonitor) {
        m_localMonitor->suppressPath(absSrc);
        m_localMonitor->suppressPath(absDst);
    }

    m_inflightRemoteRenames.insert(dedupeKey);
    qCInfo(KIODRIVE_SYNC_LOG) << "Remote rename scheduled" << srcRemote << "->" << destRemote << "itemId" << itemId;
    // Defer off any nested CF callback stack: renameSync uses a local event loop.
    QTimer::singleShot(0, this, [this, mountId, itemId, srcRemote, destRemote, targetRelativePath, absSrc, absDst,
                                 dedupeKey]() {
        // A matching Created must not upload a copy while rename runs.
        m_pendingRemoves.remove(PathUtils::normalizedPathKey(absSrc));
        m_pendingCreates.remove(PathUtils::normalizedPathKey(absDst));
        m_journal->beginRename(mountId, itemId, srcRemote, destRemote);
        if (m_placeholders) {
            m_placeholders->setInSync(targetRelativePath, true);
        }
        m_inflightRemoteRenames.remove(dedupeKey);
        QTimer::singleShot(2000, this, [this, absSrc, absDst]() {
            if (!m_localMonitor) {
                return;
            }
            m_localMonitor->unsuppressPath(absSrc);
            m_localMonitor->unsuppressPath(absDst);
        });
    });
}

void SyncEngineCore::onDeleteRequested(const QString &relativePath)
{
    QUuid mountId;
    QUuid itemId;
    QString remotePath;
    if (!resolveRelativeToItem(relativePath, &mountId, &itemId, &remotePath)) {
        return;
    }
    m_journal->beginDelete(mountId, itemId, remotePath);
}

void SyncEngineCore::onLocalChange(const LocalChange &change)
{
    qCInfo(KIODRIVE_SYNC_LOG) << "onLocalChange" << localChangeTypeName(change.type) << change.path
                              << (change.optionalNewPath.isEmpty() ? QString() : change.optionalNewPath)
                              << "pendingCreates" << m_pendingCreates.size() << "pendingRemoves"
                              << m_pendingRemoves.size();

    // While CFAPI owns a rename, ignore correlated FS noise even if suppress races.
    if (!m_pendingCfRenames.isEmpty() && m_syncRoot) {
        const QString rel = QDir::cleanPath(QDir(m_syncRoot->syncRootPath()).relativeFilePath(change.path));
        const QString relKey = PathUtils::normalizedPathKey(rel);
        for (const PendingCfRename &pending : std::as_const(m_pendingCfRenames)) {
            if (relKey == PathUtils::normalizedPathKey(pending.sourceRelativePath)
                || relKey == PathUtils::normalizedPathKey(pending.targetRelativePath)) {
                qCInfo(KIODRIVE_SYNC_LOG) << "Local change ignored (CFAPI rename in flight)" << rel;
                return;
            }
        }
    }

    if (change.type == LocalChangeType::Renamed) {
        if (change.optionalNewPath.isEmpty()) {
            return;
        }
        const QString srcRel = QDir::cleanPath(QDir(m_syncRoot->syncRootPath()).relativeFilePath(change.path));
        const QString dstRel = QDir::cleanPath(QDir(m_syncRoot->syncRootPath()).relativeFilePath(change.optionalNewPath));
        if (srcRel.startsWith(QLatin1String("..")) || dstRel.startsWith(QLatin1String(".."))) {
            return;
        }
        qCInfo(KIODRIVE_SYNC_LOG) << "Handling as native Renamed event" << srcRel << "->" << dstRel;
        scheduleRemoteRename(srcRel, dstRel);
        return;
    }

    if (change.type == LocalChangeType::Removed) {
        const QString relative = QDir::cleanPath(QDir(m_syncRoot->syncRootPath()).relativeFilePath(change.path));
        if (relative.startsWith(QLatin1String(".."))) {
            return;
        }
        QUuid mountId;
        QUuid itemId;
        QString remotePath;
        if (!resolveRelativeToItem(relative, &mountId, &itemId, &remotePath)) {
            qCWarning(KIODRIVE_SYNC_LOG) << "Removed path not under a mount" << relative;
            return;
        }
        if (itemId.isNull()) {
            if (const auto item = m_store->itemByRemotePath(mountId, remotePath)) {
                itemId = item->id;
            }
        }
        if (itemId.isNull()) {
            qCWarning(KIODRIVE_SYNC_LOG) << "Removed path has no metadata item" << relative << remotePath;
            return;
        }

        PendingRemove pending;
        pending.absolutePath = change.path;
        pending.relativePath = relative;
        pending.mountId = mountId;
        pending.itemId = itemId;
        pending.remotePath = remotePath;
        pending.fileName = QFileInfo(change.path).fileName();
        if (const auto item = m_store->itemById(itemId)) {
            pending.size = item->size;
            pending.isDirectory = item->kind == ItemKind::Directory;
        }

        qCInfo(KIODRIVE_SYNC_LOG) << "Removed candidate" << relative << "remote" << remotePath << "isDir"
                                  << pending.isDirectory << "pendingCreates" << m_pendingCreates.size();

        // CREATE often arrives before REMOVE for folder moves — match a pending create.
        if (tryConsumePendingCreateAsMove(pending)) {
            return;
        }

        qCInfo(KIODRIVE_SYNC_LOG) << "No pending create matched remove; holding delete" << relative;
        rememberPendingRemove(pending);
        return;
    }

    if (change.type != LocalChangeType::Modified && change.type != LocalChangeType::Created) {
        return;
    }

    QFileInfo info(change.path);
    if (!info.exists()) {
        qCWarning(KIODRIVE_SYNC_LOG) << "Local change path missing on disk" << change.path;
        return;
    }

    QUuid mountId;
    QUuid itemId;
    QString remotePath;
    const QString relative = QDir::cleanPath(QDir(m_syncRoot->syncRootPath()).relativeFilePath(change.path));
    if (relative.startsWith(QLatin1String(".."))) {
        return;
    }
    if (!resolveRelativeToItem(relative, &mountId, &itemId, &remotePath)) {
        return;
    }

    // Skip the mount root directory itself.
    const auto mount = m_mounts->mount(mountId);
    if (mount && relative.compare(mount->localRelativePath, Qt::CaseInsensitive) == 0) {
        return;
    }

    if (info.isDir()) {
        if (change.type != LocalChangeType::Created) {
            return;
        }
        qCInfo(KIODRIVE_SYNC_LOG) << "Created directory candidate" << relative << "remote" << remotePath
                                  << "pendingRemoves" << m_pendingRemoves.size();
        // Folder moves are often CREATE(dest) then REMOVE(src). Defer mkdir and wait for remove.
        if (tryConsumePendingRemoveAsMove(change.path, mountId, remotePath, -1, true)) {
            return;
        }

        PendingCreate pending;
        pending.absolutePath = change.path;
        pending.relativePath = relative;
        pending.mountId = mountId;
        pending.itemId = itemId;
        pending.remotePath = remotePath;
        pending.fileName = info.fileName();
        pending.isDirectory = true;
        qCInfo(KIODRIVE_SYNC_LOG) << "No pending remove matched directory create; holding mkdir" << relative;
        rememberPendingCreate(pending);
        return;
    }

    if (!info.isFile()) {
        return;
    }

    // Explorer often reports placeholder moves as REMOVE + ADD instead of RENAME.
    if (change.type == LocalChangeType::Created) {
        qCInfo(KIODRIVE_SYNC_LOG) << "Created file candidate" << relative << "remote" << remotePath << "size"
                                  << info.size() << "pendingRemoves" << m_pendingRemoves.size();
        if (tryConsumePendingRemoveAsMove(change.path, mountId, remotePath, info.size(), false)) {
            return;
        }
        qCInfo(KIODRIVE_SYNC_LOG) << "No pending remove matched file create; uploading" << relative;
    }

    if (itemId.isNull()) {
        itemId = QUuid::createUuid();
        ItemRecord item;
        item.id = itemId;
        item.mountId = mountId;
        item.remotePath = remotePath;
        item.remoteName = info.fileName();
        item.windowsName = item.remoteName;
        item.kind = ItemKind::File;
        item.size = info.size();
        item.mtime = info.lastModified().toSecsSinceEpoch();
        item.syncState = ItemSyncState::OutOfSync;
        m_store->upsertItem(item);
    } else {
        m_store->updateItemSyncState(itemId, ItemSyncState::OutOfSync);
    }

    qCInfo(KIODRIVE_SYNC_LOG) << "Uploading local edit" << relative << "->" << remotePath;
    m_localMonitor->suppressPath(change.path);
    QTimer::singleShot(0, this, [this, mountId, itemId, path = change.path, remotePath, relative]() {
        m_journal->beginUpload(mountId, itemId, path, remotePath);
        if (m_placeholders) {
            m_placeholders->setInSync(relative, true);
        }
        QTimer::singleShot(750, this, [this, path]() {
            if (m_localMonitor) {
                m_localMonitor->unsuppressPath(path);
            }
        });
    });
}

void SyncEngineCore::rememberPendingRemove(const PendingRemove &pending)
{
    const QString key = PathUtils::normalizedPathKey(pending.absolutePath);
    m_pendingRemoves.insert(key, pending);
    qCInfo(KIODRIVE_SYNC_LOG) << "Pending remove held" << pending.relativePath << "remote" << pending.remotePath
                              << "isDir" << pending.isDirectory << "size" << pending.size << "queueSize"
                              << m_pendingRemoves.size() << "timeoutMs" << 2000;
    QTimer::singleShot(2000, this, [this, key, path = pending.absolutePath]() {
        if (!m_pendingRemoves.contains(key)) {
            qCDebug(KIODRIVE_SYNC_LOG) << "Pending remove timeout skipped (already consumed)" << path;
            return;
        }
        qCInfo(KIODRIVE_SYNC_LOG) << "Pending remove timeout flush" << path;
        flushPendingRemove(path);
    });
}

void SyncEngineCore::rememberPendingCreate(const PendingCreate &pending)
{
    const QString key = PathUtils::normalizedPathKey(pending.absolutePath);
    m_pendingCreates.insert(key, pending);
    qCInfo(KIODRIVE_SYNC_LOG) << "Pending create held" << pending.relativePath << "remote" << pending.remotePath
                              << "isDir" << pending.isDirectory << "queueSize" << m_pendingCreates.size()
                              << "timeoutMs" << 1500;
    QTimer::singleShot(1500, this, [this, key, path = pending.absolutePath]() {
        if (!m_pendingCreates.contains(key)) {
            qCDebug(KIODRIVE_SYNC_LOG) << "Pending create timeout skipped (already consumed)" << path;
            return;
        }
        qCInfo(KIODRIVE_SYNC_LOG) << "Pending create timeout flush" << path;
        flushPendingCreate(path);
    });
}

void SyncEngineCore::flushPendingRemove(const QString &absolutePath)
{
    const QString key = PathUtils::normalizedPathKey(absolutePath);
    const auto it = m_pendingRemoves.find(key);
    if (it == m_pendingRemoves.end()) {
        return;
    }
    const PendingRemove pending = it.value();
    m_pendingRemoves.erase(it);

    // If the path reappeared locally, it was not a delete.
    if (QFileInfo::exists(pending.absolutePath)) {
        qCInfo(KIODRIVE_SYNC_LOG) << "Flush remove cancelled (path exists again)" << pending.relativePath;
        return;
    }

    qCInfo(KIODRIVE_SYNC_LOG) << "Flush remove last-chance match against pendingCreates"
                              << m_pendingCreates.size() << pending.relativePath;
    // Last chance: a delayed create may still be pending.
    if (tryConsumePendingCreateAsMove(pending)) {
        return;
    }

    qCInfo(KIODRIVE_SYNC_LOG) << "Remote delete after remove" << pending.remotePath << "itemId" << pending.itemId;
    m_journal->beginDelete(pending.mountId, pending.itemId, pending.remotePath);
}

void SyncEngineCore::flushPendingCreate(const QString &absolutePath)
{
    const QString key = PathUtils::normalizedPathKey(absolutePath);
    const auto it = m_pendingCreates.find(key);
    if (it == m_pendingCreates.end()) {
        return;
    }
    const PendingCreate pending = it.value();
    m_pendingCreates.erase(it);

    if (!QFileInfo::exists(pending.absolutePath)) {
        qCInfo(KIODRIVE_SYNC_LOG) << "Flush create cancelled (path gone)" << pending.relativePath;
        return;
    }

    qCInfo(KIODRIVE_SYNC_LOG) << "Flush create last-chance match against pendingRemoves"
                              << m_pendingRemoves.size() << pending.relativePath;
    // Last chance: a remove may have arrived just as we timed out.
    if (tryConsumePendingRemoveAsMove(pending.absolutePath, pending.mountId, pending.remotePath, -1, pending.isDirectory)) {
        return;
    }

    QUuid itemId = pending.itemId;
    if (itemId.isNull()) {
        itemId = QUuid::createUuid();
        ItemRecord item;
        item.id = itemId;
        item.mountId = pending.mountId;
        item.remotePath = pending.remotePath;
        item.remoteName = pending.fileName;
        item.windowsName = pending.fileName;
        item.kind = ItemKind::Directory;
        item.syncState = ItemSyncState::OutOfSync;
        m_store->upsertItem(item);
    }

    qCInfo(KIODRIVE_SYNC_LOG) << "Creating remote directory (no move matched)" << pending.remotePath;
    if (m_localMonitor) {
        m_localMonitor->suppressPath(pending.absolutePath);
    }
    m_journal->beginMkdir(pending.mountId, itemId, pending.remotePath);
    QTimer::singleShot(750, this, [this, path = pending.absolutePath]() {
        if (m_localMonitor) {
            m_localMonitor->unsuppressPath(path);
        }
    });
}

bool SyncEngineCore::tryConsumePendingRemoveAsMove(const QString &destAbsolutePath,
                                                   const QUuid &destMountId,
                                                   const QString &destRemotePath,
                                                   qint64 destSize,
                                                   bool destIsDirectory)
{
    const QString destName = QFileInfo(destAbsolutePath).fileName();
    qCInfo(KIODRIVE_SYNC_LOG) << "Matching create against pendingRemoves" << "name" << destName << "destRemote"
                              << destRemotePath << "isDir" << destIsDirectory << "destSize" << destSize
                              << "candidates" << m_pendingRemoves.size();

    auto nameMatch = m_pendingRemoves.end();
    QList<decltype(m_pendingRemoves.begin())> renameCandidates;
    for (auto it = m_pendingRemoves.begin(); it != m_pendingRemoves.end(); ++it) {
        if (it->mountId != destMountId) {
            qCDebug(KIODRIVE_SYNC_LOG) << "  skip remove (mount mismatch)" << it->relativePath;
            continue;
        }
        if (it->isDirectory != destIsDirectory) {
            qCDebug(KIODRIVE_SYNC_LOG) << "  skip remove (kind mismatch)" << it->relativePath << "pendingIsDir"
                                       << it->isDirectory;
            continue;
        }
        if (it->remotePath.compare(destRemotePath, Qt::CaseInsensitive) == 0) {
            qCDebug(KIODRIVE_SYNC_LOG) << "  skip remove (same remote path)" << it->remotePath;
            continue;
        }
        if (!destIsDirectory && it->size >= 0 && destSize >= 0 && it->size != destSize) {
            qCDebug(KIODRIVE_SYNC_LOG) << "  skip remove (size mismatch)" << it->size << "!=" << destSize;
            continue;
        }
        // mv/rename changes the leaf name (e.g. xis -> w/zed). Keep as unique fallback.
        if (it->fileName.compare(destName, Qt::CaseInsensitive) != 0) {
            qCInfo(KIODRIVE_SYNC_LOG) << "  rename-move candidate" << it->relativePath << it->fileName << "->"
                                      << destName;
            renameCandidates.append(it);
            continue;
        }
        qCInfo(KIODRIVE_SYNC_LOG) << "  candidate matched" << it->relativePath << "remote" << it->remotePath;
        nameMatch = it;
        break;
    }

    auto match = nameMatch;
    if (match == m_pendingRemoves.end()) {
        if (renameCandidates.size() == 1) {
            match = renameCandidates.first();
            qCInfo(KIODRIVE_SYNC_LOG) << "  unique rename-move matched" << match->relativePath << "->"
                                      << destAbsolutePath;
        } else {
            if (!renameCandidates.isEmpty()) {
                qCInfo(KIODRIVE_SYNC_LOG) << "Ambiguous rename-move candidates" << renameCandidates.size()
                                          << "for create" << destName;
            }
            qCInfo(KIODRIVE_SYNC_LOG) << "No pending remove matched create" << destName << destRemotePath;
            return false;
        }
    }

    const PendingRemove pending = match.value();
    m_pendingRemoves.erase(match);

    const QString srcRel = pending.relativePath;
    const QString dstRel = QDir::cleanPath(QDir(m_syncRoot->syncRootPath()).relativeFilePath(destAbsolutePath));
    qCInfo(KIODRIVE_SYNC_LOG) << "Detected move via remove+create" << srcRel << "->" << dstRel
                              << "srcRemote" << pending.remotePath << "destRemote" << destRemotePath
                              << (pending.fileName.compare(destName, Qt::CaseInsensitive) == 0
                                      ? QStringLiteral("sameName")
                                      : QStringLiteral("renamed"));
    scheduleRemoteRename(srcRel, dstRel);
    return true;
}

bool SyncEngineCore::tryConsumePendingCreateAsMove(const PendingRemove &removed)
{
    qCInfo(KIODRIVE_SYNC_LOG) << "Matching remove against pendingCreates" << "name" << removed.fileName
                              << "srcRemote" << removed.remotePath << "isDir" << removed.isDirectory
                              << "candidates" << m_pendingCreates.size();

    auto nameMatch = m_pendingCreates.end();
    QList<decltype(m_pendingCreates.begin())> renameCandidates;
    for (auto it = m_pendingCreates.begin(); it != m_pendingCreates.end(); ++it) {
        if (it->mountId != removed.mountId) {
            qCDebug(KIODRIVE_SYNC_LOG) << "  skip create (mount mismatch)" << it->relativePath;
            continue;
        }
        if (it->isDirectory != removed.isDirectory) {
            qCDebug(KIODRIVE_SYNC_LOG) << "  skip create (kind mismatch)" << it->relativePath << "pendingIsDir"
                                       << it->isDirectory;
            continue;
        }
        if (it->remotePath.compare(removed.remotePath, Qt::CaseInsensitive) == 0) {
            qCDebug(KIODRIVE_SYNC_LOG) << "  skip create (same remote path)" << it->remotePath;
            continue;
        }
        if (it->fileName.compare(removed.fileName, Qt::CaseInsensitive) != 0) {
            qCInfo(KIODRIVE_SYNC_LOG) << "  rename-move candidate" << it->relativePath << removed.fileName << "->"
                                      << it->fileName;
            renameCandidates.append(it);
            continue;
        }
        qCInfo(KIODRIVE_SYNC_LOG) << "  candidate matched" << it->relativePath << "remote" << it->remotePath;
        nameMatch = it;
        break;
    }

    auto match = nameMatch;
    if (match == m_pendingCreates.end()) {
        if (renameCandidates.size() == 1) {
            match = renameCandidates.first();
            qCInfo(KIODRIVE_SYNC_LOG) << "  unique rename-move matched" << removed.relativePath << "->"
                                      << match->relativePath;
        } else {
            if (!renameCandidates.isEmpty()) {
                qCInfo(KIODRIVE_SYNC_LOG) << "Ambiguous rename-move candidates" << renameCandidates.size()
                                          << "for remove" << removed.relativePath;
            }
            qCInfo(KIODRIVE_SYNC_LOG) << "No pending create matched remove" << removed.relativePath;
            return false;
        }
    }

    const PendingCreate pending = match.value();
    m_pendingCreates.erase(match);

    qCInfo(KIODRIVE_SYNC_LOG) << "Detected move via create+remove" << removed.relativePath << "->"
                              << pending.relativePath << "srcRemote" << removed.remotePath << "destRemote"
                              << pending.remotePath
                              << (removed.fileName.compare(pending.fileName, Qt::CaseInsensitive) == 0
                                      ? QStringLiteral("sameName")
                                      : QStringLiteral("renamed"));
    scheduleRemoteRename(removed.relativePath, pending.relativePath);
    return true;
}

void SyncEngineCore::onFileClosed(const QString &relativePath)
{
    maybeUploadAfterClose(relativePath);
}

void SyncEngineCore::maybeUploadAfterClose(const QString &relativePath)
{
    QUuid mountId;
    QUuid itemId;
    QString remotePath;
    if (!resolveRelativeToItem(relativePath, &mountId, &itemId, &remotePath) || itemId.isNull()) {
        return;
    }

    const QString absolute = QDir(m_syncRoot->syncRootPath()).filePath(relativePath);
    QFileInfo info(absolute);
    if (!info.exists() || !info.isFile()) {
        return;
    }

    const auto item = m_store->itemById(itemId);
    if (!item) {
        return;
    }

    const qint64 mtime = info.lastModified().toSecsSinceEpoch();
    const bool dirty = item->syncState == ItemSyncState::OutOfSync || item->size != info.size() || item->mtime != mtime;
    if (!dirty) {
        return;
    }

    qCInfo(KIODRIVE_SYNC_LOG) << "Upload after close (dirty)" << relativePath;
    m_store->updateItemSyncState(itemId, ItemSyncState::OutOfSync);
    if (m_localMonitor) {
        m_localMonitor->suppressPath(absolute);
    }
    QTimer::singleShot(0, this, [this, mountId, itemId, absolute, remotePath, relativePath]() {
        m_journal->beginUpload(mountId, itemId, absolute, remotePath);
        if (auto updated = m_store->itemById(itemId)) {
            QFileInfo fi(absolute);
            updated->size = fi.size();
            updated->mtime = fi.lastModified().toSecsSinceEpoch();
            updated->syncState = ItemSyncState::InSync;
            m_store->upsertItem(*updated);
        }
        if (m_placeholders) {
            m_placeholders->setInSync(relativePath, true);
        }
        QTimer::singleShot(750, this, [this, absolute]() {
            if (m_localMonitor) {
                m_localMonitor->unsuppressPath(absolute);
            }
        });
    });
}

void SyncEngineCore::onDehydrateRequested(const QString &relativePath)
{
    QUuid mountId;
    QUuid itemId;
    QString remotePath;
    if (!resolveRelativeToItem(relativePath, &mountId, &itemId, &remotePath) || itemId.isNull()) {
        return;
    }
    m_store->updateItemSyncState(itemId, ItemSyncState::OnlineOnly);
    qCInfo(KIODRIVE_SYNC_LOG) << "Dehydrate requested" << relativePath;
}

void SyncEngineCore::onDehydrateCompleted(const QString &relativePath)
{
    QUuid mountId;
    QUuid itemId;
    QString remotePath;
    if (resolveRelativeToItem(relativePath, &mountId, &itemId, &remotePath) && !itemId.isNull()) {
        m_store->updateItemSyncState(itemId, ItemSyncState::OnlineOnly);
    }
    qCInfo(KIODRIVE_SYNC_LOG) << "Dehydrate completed" << relativePath;
}

void SyncEngineCore::onDeleteCompleted(const QString &relativePath)
{
    QUuid mountId;
    QUuid itemId;
    QString remotePath;
    if (resolveRelativeToItem(relativePath, &mountId, &itemId, &remotePath) && !itemId.isNull()) {
        m_store->updateItemSyncState(itemId, ItemSyncState::DeletedRemote);
    }
    qCInfo(KIODRIVE_SYNC_LOG) << "Delete completed" << relativePath;
}

void SyncEngineCore::onRenameCompleted(const QString &sourceRelativePath,
                                       const QString &targetRelativePath,
                                       bool isDirectory,
                                       bool sourceInScope,
                                       bool targetInScope)
{
    // Prefer scope recorded at NOTIFY_RENAME; completion params do not carry scope flags.
    bool srcIn = sourceInScope;
    bool dstIn = targetInScope;
    bool isDir = isDirectory;
    QString source = sourceRelativePath;
    QString target = targetRelativePath;

    const QString srcKey = PathUtils::normalizedPathKey(sourceRelativePath);
    if (m_pendingCfRenames.contains(srcKey)) {
        const PendingCfRename pending = m_pendingCfRenames.take(srcKey);
        srcIn = pending.sourceInScope;
        dstIn = pending.targetInScope;
        isDir = pending.isDirectory;
        if (source.isEmpty()) {
            source = pending.sourceRelativePath;
        }
        if (target.isEmpty()) {
            target = pending.targetRelativePath;
        }
    } else if (!targetRelativePath.isEmpty()) {
        // Completion sometimes arrives with only the final path as NormalizedPath; try match by target.
        for (auto it = m_pendingCfRenames.begin(); it != m_pendingCfRenames.end(); ++it) {
            if (it->targetRelativePath.compare(targetRelativePath, Qt::CaseInsensitive) == 0) {
                srcIn = it->sourceInScope;
                dstIn = it->targetInScope;
                isDir = it->isDirectory;
                if (source.isEmpty()) {
                    source = it->sourceRelativePath;
                }
                m_pendingCfRenames.erase(it);
                break;
            }
        }
    }

    qCInfo(KIODRIVE_SYNC_LOG) << "CFAPI rename completion" << source << "->" << target << "isDir" << isDir
                              << "sourceInScope" << srcIn << "targetInScope" << dstIn;

    if (srcIn && dstIn) {
        scheduleRemoteRename(source, target);
        return;
    }

    if (srcIn && !dstIn) {
        // Moved out of the sync root — delete remotely.
        qCInfo(KIODRIVE_SYNC_LOG) << "Rename left sync root; scheduling remote delete" << source;
        onDeleteRequested(source);
        if (m_localMonitor && m_syncRoot && !source.isEmpty()) {
            const QString abs = QDir(m_syncRoot->syncRootPath()).filePath(source);
            QTimer::singleShot(2000, this, [this, abs]() {
                if (m_localMonitor) {
                    m_localMonitor->unsuppressPath(abs);
                }
            });
        }
        return;
    }

    if (!srcIn && dstIn) {
        // Entered the sync root — treat destination as a local create.
        qCInfo(KIODRIVE_SYNC_LOG) << "Rename entered sync root; ingesting" << target << "isDir" << isDir;
        if (m_syncRoot && !target.isEmpty()) {
            LocalChange change;
            change.type = LocalChangeType::Created;
            change.path = QDir(m_syncRoot->syncRootPath()).filePath(target);
            onLocalChange(change);
        }
        return;
    }

    qCInfo(KIODRIVE_SYNC_LOG) << "CFAPI rename completion ignored (outside sync root)" << source << target;
}

} // namespace KIODrive

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

#ifndef KIODRIVE_CLOUDFILES_CLOUD_FILES_CALLBACKS_H
#define KIODRIVE_CLOUDFILES_CLOUD_FILES_CALLBACKS_H

#include "pending_fetch_registry.h"
#include "placeholder_manager.h"

#include <QObject>
#include <QString>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winternl.h>
#include <cfapi.h>
#endif

namespace KIODrive
{

class HydrationManager;
class MetadataStore;
class MountRegistry;
class KioBackend;

/**
 * CFAPI callback dispatch. Native callbacks must return quickly; heavy work is
 * queued onto the Qt thread via signals.
 */
class CloudFilesCallbacks : public QObject
{
    Q_OBJECT

public:
    CloudFilesCallbacks(MetadataStore *store,
                        MountRegistry *mounts,
                        KioBackend *backend,
                        PlaceholderManager *placeholders,
                        HydrationManager *hydration,
                        const QString &syncRootPath,
                        QObject *parent = nullptr);

    void *callbackContext();
    PendingFetchRegistry *pendingFetches();

#ifdef Q_OS_WIN
    /** Lifetime of the returned table is the process; pass to CfConnectSyncRoot. */
    static const CF_CALLBACK_REGISTRATION *registrationTable();
#endif

Q_SIGNALS:
    void placeholdersRequested(const QString &relativePath);
    /** DirectConnection: fill @p outItems without calling CfCreatePlaceholders. */
    void placeholdersFetchRequested(const QString &relativePath, QVector<PlaceholderCreateRequest> *outItems);
    void hydrationRequested(const QString &relativePath, quint64 offset, quint64 length);
    void hydrationStarted(const QString &absolutePath);
    void hydrationFinished(const QString &absolutePath);
    /**
     * CFAPI NOTIFY_RENAME (pre-ack). Authorizes the local rename only; remote sync
     * waits for renameCompleted. Scope flags mirror CF_CALLBACK_RENAME_FLAG_*.
     */
    void renameRequested(const QString &sourceRelativePath,
                         const QString &targetRelativePath,
                         bool isDirectory,
                         bool sourceInScope,
                         bool targetInScope);
    void deleteRequested(const QString &relativePath);
    void fileOpened(const QString &relativePath);
    void fileClosed(const QString &relativePath);
    void dehydrateRequested(const QString &relativePath);
    void dehydrateCompleted(const QString &relativePath);
    void deleteCompleted(const QString &relativePath);
    /** CFAPI NOTIFY_RENAME_COMPLETION — local rename finished; sync remotely. */
    void renameCompleted(const QString &sourceRelativePath,
                         const QString &targetRelativePath,
                         bool isDirectory,
                         bool sourceInScope,
                         bool targetInScope);

private:
#ifdef Q_OS_WIN
    static void CALLBACK onFetchPlaceholders(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onFetchData(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onCancelFetchData(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onCancelFetchPlaceholders(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onNotifyFileOpenCompletion(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onNotifyFileCloseCompletion(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onNotifyDehydrate(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onNotifyDehydrateCompletion(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onNotifyRename(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onNotifyRenameCompletion(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onNotifyDelete(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);
    static void CALLBACK onNotifyDeleteCompletion(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params);

    QString relativePathFromCallback(CONST CF_CALLBACK_INFO *info) const;
    /**
     * Convert a CFAPI path (often volume-relative) to a path relative to the sync
     * root. Sets @p inScope when the path is under the sync root (including the
     * root itself, which yields an empty relative path).
     */
    QString toSyncRelativePath(const QString &rawPath, bool *inScope = nullptr) const;
    void processFetchPlaceholders(const QString &relativePath,
                                  quint64 requestKey,
                                  CF_CONNECTION_KEY connectionKey,
                                  CF_TRANSFER_KEY transferKey);
    void processFetchData(const QString &relativePath,
                          quint64 offset,
                          quint64 length,
                          const QByteArray &fileIdentity,
                          quint64 requestKey,
                          const QUuid &operationId,
                          CF_CONNECTION_KEY connectionKey,
                          CF_TRANSFER_KEY transferKey);
    void transferPlaceholders(CF_CONNECTION_KEY connectionKey,
                              CF_TRANSFER_KEY transferKey,
                              const QVector<PlaceholderCreateRequest> &items,
                              NTSTATUS status);
    void transferData(CF_CONNECTION_KEY connectionKey,
                      CF_TRANSFER_KEY transferKey,
                      NTSTATUS status,
                      quint64 offset,
                      quint64 length,
                      const QByteArray &data);

    QVector<PlaceholderCreateRequest> mountRootPlaceholders() const;
    bool resolveHydrationTarget(const QString &relativePath,
                                const QByteArray &fileIdentity,
                                FileIdentityV1 *identity) const;
#endif

    MetadataStore *m_store = nullptr;
    MountRegistry *m_mounts = nullptr;
    KioBackend *m_backend = nullptr;
    PlaceholderManager *m_placeholders = nullptr;
    HydrationManager *m_hydration = nullptr;
    QString m_syncRootPath;
    PendingFetchRegistry m_pendingFetches;
};

} // namespace KIODrive

#endif // KIODRIVE_CLOUDFILES_CLOUD_FILES_CALLBACKS_H

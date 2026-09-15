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

#ifndef KIODRIVE_SYNC_REMOTE_RECONCILER_H
#define KIODRIVE_SYNC_REMOTE_RECONCILER_H

#include "cloudfiles/placeholder_manager.h"
#include "kio/kio_backend.h"
#include "metadata/metadata_store.h"
#include "mounts/mount_registry.h"

#include <QObject>
#include <QVector>

namespace KIODrive
{

class RemoteReconciler : public QObject
{
    Q_OBJECT

public:
    RemoteReconciler(MetadataStore *store,
                     MountRegistry *mounts,
                     KioBackend *backend,
                     PlaceholderManager *placeholders,
                     QObject *parent = nullptr);

    /**
     * List remote directory, upsert metadata, return placeholder descriptors.
     * Does not touch the local CF placeholder tree (safe during outstanding FETCH_PLACEHOLDERS).
     */
    QVector<PlaceholderCreateRequest> syncDirectoryEntries(const QUuid &mountId, const QString &remotePath);

    /** syncDirectoryEntries + CfCreatePlaceholders (only when no CF callback is waiting). */
    void refreshDirectory(const QUuid &mountId, const QString &remotePath, const QString &localRelativePath);

Q_SIGNALS:
    void directoryRefreshed(const QUuid &mountId, const QString &localRelativePath);
    void conflictDetected(const QUuid &itemId);

private:
    MetadataStore *m_store = nullptr;
    MountRegistry *m_mounts = nullptr;
    KioBackend *m_backend = nullptr;
    PlaceholderManager *m_placeholders = nullptr;
};

} // namespace KIODrive

#endif // KIODRIVE_SYNC_REMOTE_RECONCILER_H

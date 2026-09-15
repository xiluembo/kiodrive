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

#ifndef KIODRIVE_SYNC_OPERATION_JOURNAL_H
#define KIODRIVE_SYNC_OPERATION_JOURNAL_H

#include "metadata/metadata_store.h"

#include <QObject>

namespace KIODrive
{

class KioBackend;
class MountRegistry;

/**
 * Durable journal for rename/delete/upload. Crash recovery never assumes remote success.
 */
class OperationJournal : public QObject
{
    Q_OBJECT

public:
    OperationJournal(MetadataStore *store, KioBackend *backend, MountRegistry *mounts, QObject *parent = nullptr);

    QUuid beginRename(const QUuid &mountId, const QUuid &itemId, const QString &srcPath, const QString &destPath);
    QUuid beginDelete(const QUuid &mountId, const QUuid &itemId, const QString &remotePath);
    QUuid beginUpload(const QUuid &mountId, const QUuid &itemId, const QString &localPath, const QString &remotePath);
    QUuid beginMkdir(const QUuid &mountId, const QUuid &itemId, const QString &remotePath);

    /** Create missing remote parent directories for @p remotePath (file or dir). */
    bool ensureRemoteParents(const QUuid &mountId, const QString &remotePath);

    void recoverIncomplete();

Q_SIGNALS:
    void operationCompleted(const QUuid &operationId);
    void operationFailed(const QUuid &operationId, const QString &error);

private:
    MetadataStore *m_store = nullptr;
    KioBackend *m_backend = nullptr;
    MountRegistry *m_mounts = nullptr;
};

} // namespace KIODrive

#endif // KIODRIVE_SYNC_OPERATION_JOURNAL_H

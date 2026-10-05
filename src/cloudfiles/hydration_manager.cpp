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

#include "hydration_manager.h"

#include "kio/kio_backend.h"
#include "metadata/metadata_store.h"
#include "mounts/mount_registry.h"

#include "common/logging.h"

#include <QFile>
#include <QUrl>

namespace KIODrive
{

HydrationManager::HydrationManager(MetadataStore *store,
                                   MountRegistry *mounts,
                                   KioBackend *backend,
                                   QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_mounts(mounts)
    , m_backend(backend)
{
}

void HydrationManager::hydrate(const HydrationRequest &request,
                               ProgressCallback onProgress,
                               CompletionCallback onComplete)
{
    Q_UNUSED(onProgress);

    if (!m_store || !m_mounts || !m_backend) {
        if (onComplete) {
            onComplete(false, {}, QStringLiteral("HydrationManager is not configured"));
        }
        return;
    }

    const auto item = m_store->itemById(request.identity.itemId);
    const auto mount = m_mounts->mount(request.identity.mountId);
    if (!item || !mount) {
        if (onComplete) {
            onComplete(false, {}, QStringLiteral("Unknown placeholder identity"));
        }
        return;
    }

    QUrl url = mount->remoteUrl;
    url.setPath(item->remotePath);

    m_backend->setMountId(mount->id);
    m_store->updateItemSyncState(item->id, ItemSyncState::Hydrating);
    qCInfo(KIODRIVE_CFAPI_LOG) << "Hydrating" << item->remotePath << "offset" << request.requiredOffset
                               << "length" << request.requiredLength << "operationId" << request.operationId;

    m_activeOperationId = request.operationId;
    m_cancelRequested = false;

    QByteArray data;
    QString error;
    bool ok = false;

    // Local files: seek/read directly. KIO::get range metadata is unreliable for file://
    // and would return the wrong window for CFAPI RequiredOffset > 0.
    const QString localPath = url.toLocalFile();
    if (!localPath.isEmpty()) {
        QFile file(localPath);
        if (!file.open(QIODevice::ReadOnly)) {
            error = file.errorString();
        } else {
            if (!file.seek(static_cast<qint64>(request.requiredOffset))) {
                error = file.errorString();
            } else {
                const qint64 toRead = request.requiredLength == 0 ? -1 : static_cast<qint64>(request.requiredLength);
                data = file.read(toRead);
                ok = true;
            }
        }
    } else {
        const BackendResult result = m_backend->getSync(url,
                                                        static_cast<qint64>(request.requiredOffset),
                                                        request.requiredLength == 0
                                                            ? -1
                                                            : static_cast<qint64>(request.requiredLength));
        ok = result.ok;
        data = result.data;
        error = result.errorString;

        // If the worker ignored range headers and returned a prefix from byte 0, slice.
        if (ok && request.requiredOffset > 0
            && static_cast<quint64>(data.size()) > request.requiredOffset) {
            const int start = static_cast<int>(request.requiredOffset);
            const int len = request.requiredLength == 0
                ? data.size() - start
                : qMin(static_cast<int>(request.requiredLength), data.size() - start);
            data = data.mid(start, len);
        } else if (ok && request.requiredLength > 0
                   && static_cast<quint64>(data.size()) > request.requiredLength) {
            data = data.left(static_cast<int>(request.requiredLength));
        }
    }

    if (m_cancelRequested) {
        ok = false;
        data.clear();
        error = QStringLiteral("Hydration cancelled");
    } else if (ok && request.requiredLength > 0
               && static_cast<quint64>(data.size()) != request.requiredLength) {
        // Short-read must not be treated as success (QFile::read often returns ok with fewer bytes).
        qCWarning(KIODRIVE_CFAPI_LOG) << "Hydration short read for" << item->remotePath
                                      << "expected" << request.requiredLength << "got" << data.size();
        ok = false;
        error = QStringLiteral("Short read: expected %1 bytes, got %2")
                    .arg(request.requiredLength)
                    .arg(data.size());
        data.clear();
    }

    if (ok) {
        m_store->updateItemSyncState(item->id, ItemSyncState::InSync);
    }

    m_activeOperationId = QUuid();
    m_cancelRequested = false;

    if (onComplete) {
        onComplete(ok, data, error);
    }
}

void HydrationManager::cancel(const QUuid &operationId)
{
    if (operationId.isNull()) {
        return;
    }
    if (m_activeOperationId != operationId) {
        qCDebug(KIODRIVE_CFAPI_LOG) << "Ignoring cancel for inactive hydration" << operationId
                                    << "active" << m_activeOperationId;
        return;
    }
    m_cancelRequested = true;
    if (m_backend) {
        // Kill only the active get — never listDir or unrelated transfers.
        m_backend->cancelGet();
    }
}

} // namespace KIODrive

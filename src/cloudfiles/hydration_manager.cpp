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
                               << "length" << request.requiredLength;

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

    if (ok) {
        m_store->updateItemSyncState(item->id, ItemSyncState::InSync);
    }
    if (onComplete) {
        onComplete(ok, data, error);
    }
}

void HydrationManager::cancel(const QUuid &operationId)
{
    Q_UNUSED(operationId);
    if (m_backend) {
        m_backend->cancelTransfers();
    }
}

} // namespace KIODrive

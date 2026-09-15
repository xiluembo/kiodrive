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

#ifndef KIODRIVE_CLOUDFILES_HYDRATION_MANAGER_H
#define KIODRIVE_CLOUDFILES_HYDRATION_MANAGER_H

#include "common/file_identity.h"
#include "common/types.h"

#include <QByteArray>
#include <QObject>
#include <QUuid>
#include <functional>

namespace KIODrive
{

class KioBackend;
class MetadataStore;
class MountRegistry;

struct HydrationRequest {
    QUuid operationId;
    FileIdentityV1 identity;
    quint64 requiredOffset = 0;
    quint64 requiredLength = 0;
    QString absolutePath;
};

/**
 * Resolves placeholder identity → mount → remote URL → KIO read.
 * Prefers FileJob random reads when ReadCapabilities::randomRead is set.
 */
class HydrationManager : public QObject
{
    Q_OBJECT

public:
    HydrationManager(MetadataStore *store,
                     MountRegistry *mounts,
                     KioBackend *backend,
                     QObject *parent = nullptr);

    using ProgressCallback = std::function<void(quint64 transferred, quint64 total)>;
    using CompletionCallback = std::function<void(bool ok, const QByteArray &data, const QString &error)>;

    void hydrate(const HydrationRequest &request, ProgressCallback onProgress, CompletionCallback onComplete);
    void cancel(const QUuid &operationId);

private:
    MetadataStore *m_store = nullptr;
    MountRegistry *m_mounts = nullptr;
    KioBackend *m_backend = nullptr;
};

} // namespace KIODrive

#endif // KIODRIVE_CLOUDFILES_HYDRATION_MANAGER_H

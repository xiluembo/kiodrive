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

#ifndef KIODRIVE_MOUNTS_MOUNT_REGISTRY_H
#define KIODRIVE_MOUNTS_MOUNT_REGISTRY_H

#include "mount_conflict_detector.h"
#include "path_router.h"

#include "common/types.h"
#include "metadata/metadata_store.h"

#include <QString>
#include <QVector>
#include <optional>

namespace KIODrive
{

/**
 * Transactional mount lifecycle on top of MetadataStore, PathRouter, and
 * MountConflictDetector.
 */
class MountRegistry
{
public:
    explicit MountRegistry(MetadataStore *store, const QString &syncRootPath = {});

    MetadataStore *store() const;
    PathRouter &router();
    const PathRouter &router() const;

    QString syncRootPath() const;
    void setSyncRootPath(const QString &path);

    QString lastError() const;

    /** Reload router from persisted mounts. */
    bool rebuildRouter();

    /**
     * Validate, persist, and register a mount inside a single immediate
     * transaction. On failure the store is rolled back and the router is left
     * unchanged.
     */
    std::optional<MountInfo> createMount(MountInfo mount);

    bool removeMount(const QUuid &mountId);

    bool activateMount(const QUuid &mountId);
    bool disableMount(const QUuid &mountId);
    bool setMountState(const QUuid &mountId, MountState state);

    QVector<MountInfo> listMounts() const;
    std::optional<MountInfo> mount(const QUuid &id) const;
    std::optional<MountInfo> findOwnerMount(const QString &localPath) const;

    ValidationResult validateCandidate(const MountInfo &candidate) const;

private:
    bool updateState(const QUuid &mountId, MountState state);

    MetadataStore *m_store = nullptr;
    PathRouter m_router;
    QString m_syncRootPath;
    QString m_lastError;
};

} // namespace KIODrive

#endif // KIODRIVE_MOUNTS_MOUNT_REGISTRY_H

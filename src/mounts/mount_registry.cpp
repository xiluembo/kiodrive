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

#include "mount_registry.h"

#include "common/logging.h"
#include "common/path_utils.h"

#include <QDir>
#include <QFileInfo>

namespace KIODrive
{

MountRegistry::MountRegistry(MetadataStore *store, const QString &syncRootPath)
    : m_store(store)
    , m_syncRootPath(syncRootPath)
{
}

MetadataStore *MountRegistry::store() const
{
    return m_store;
}

PathRouter &MountRegistry::router()
{
    return m_router;
}

const PathRouter &MountRegistry::router() const
{
    return m_router;
}

QString MountRegistry::syncRootPath() const
{
    return m_syncRootPath;
}

void MountRegistry::setSyncRootPath(const QString &path)
{
    m_syncRootPath = path;
}

QString MountRegistry::lastError() const
{
    return m_lastError;
}

bool MountRegistry::rebuildRouter()
{
    m_router.clear();
    if (!m_store || !m_store->isOpen()) {
        m_lastError = QStringLiteral("Metadata store is not open.");
        return false;
    }

    const QVector<MountInfo> mounts = m_store->allMounts();
    for (const MountInfo &mount : mounts) {
        if (!m_router.registerMount(mount.id, mount.localRelativePath)) {
            m_lastError = QStringLiteral("Failed to rebuild router for mount %1").arg(mount.name);
            qCWarning(KIODRIVE_MOUNT_LOG) << m_lastError;
            return false;
        }
    }
    return true;
}

ValidationResult MountRegistry::validateCandidate(const MountInfo &candidate) const
{
    QVector<MountInfo> existing;
    if (m_store && m_store->isOpen()) {
        existing = m_store->allMounts();
    }
    return MountConflictDetector::validate(candidate, existing, m_syncRootPath);
}

std::optional<MountInfo> MountRegistry::createMount(MountInfo mount)
{
    m_lastError.clear();

    if (!m_store || !m_store->isOpen()) {
        m_lastError = QStringLiteral("Metadata store is not open.");
        return std::nullopt;
    }

    if (mount.id.isNull()) {
        mount.id = QUuid::createUuid();
    }

    mount.remoteUrl = PathUtils::canonicalizeRemoteUrl(mount.remoteUrl);
    if (mount.protocol.isEmpty()) {
        mount.protocol = mount.remoteUrl.scheme();
    }

    const ValidationResult validation = validateCandidate(mount);
    if (!validation.ok) {
        m_lastError = validation.message;
        qCWarning(KIODRIVE_MOUNT_LOG) << "createMount rejected:" << validation.message;
        return std::nullopt;
    }

    if (m_router.wouldOverlap(mount.localRelativePath)) {
        m_lastError = QStringLiteral("Path router rejected overlapping local path.");
        return std::nullopt;
    }

    if (!m_store->beginImmediate()) {
        m_lastError = m_store->lastError();
        return std::nullopt;
    }

    if (!m_store->upsertMount(mount)) {
        m_lastError = m_store->lastError();
        m_store->rollback();
        return std::nullopt;
    }

    if (!m_router.registerMount(mount.id, mount.localRelativePath)) {
        m_lastError = QStringLiteral("Failed to register mount in path router.");
        m_store->rollback();
        return std::nullopt;
    }

    if (!m_store->commit()) {
        m_lastError = m_store->lastError();
        m_router.unregisterMount(mount.id);
        m_store->rollback();
        return std::nullopt;
    }

    // Ensure parent directories exist; the mount leaf itself is created as a CF placeholder.
    if (!m_syncRootPath.isEmpty() && !mount.localRelativePath.isEmpty()) {
        const QString absolute = QDir(m_syncRootPath).filePath(mount.localRelativePath);
        const QString parent = QFileInfo(absolute).absolutePath();
        if (!QDir().mkpath(parent)) {
            qCWarning(KIODRIVE_MOUNT_LOG) << "Failed to create parent of mount directory" << parent;
        }
    }

    qCInfo(KIODRIVE_MOUNT_LOG) << "Created mount" << mount.name << mount.id << mount.localRelativePath;
    return mount;
}

bool MountRegistry::removeMount(const QUuid &mountId)
{
    m_lastError.clear();
    if (!m_store || !m_store->isOpen()) {
        m_lastError = QStringLiteral("Metadata store is not open.");
        return false;
    }

    if (!m_store->beginImmediate()) {
        m_lastError = m_store->lastError();
        return false;
    }

    if (!m_store->removeMount(mountId)) {
        m_lastError = m_store->lastError();
        m_store->rollback();
        return false;
    }

    m_router.unregisterMount(mountId);

    if (!m_store->commit()) {
        m_lastError = m_store->lastError();
        m_store->rollback();
        rebuildRouter();
        return false;
    }
    return true;
}

bool MountRegistry::updateState(const QUuid &mountId, MountState state)
{
    m_lastError.clear();
    if (!m_store || !m_store->isOpen()) {
        m_lastError = QStringLiteral("Metadata store is not open.");
        return false;
    }

    auto existing = m_store->mountById(mountId);
    if (!existing) {
        m_lastError = QStringLiteral("Mount not found.");
        return false;
    }

    existing->state = state;
    if (!m_store->upsertMount(*existing)) {
        m_lastError = m_store->lastError();
        return false;
    }
    return true;
}

bool MountRegistry::activateMount(const QUuid &mountId)
{
    return updateState(mountId, MountState::Connecting);
}

bool MountRegistry::disableMount(const QUuid &mountId)
{
    return updateState(mountId, MountState::Disabled);
}

bool MountRegistry::setMountState(const QUuid &mountId, MountState state)
{
    return updateState(mountId, state);
}

QVector<MountInfo> MountRegistry::listMounts() const
{
    if (!m_store || !m_store->isOpen()) {
        return {};
    }
    return m_store->allMounts();
}

std::optional<MountInfo> MountRegistry::mount(const QUuid &id) const
{
    if (!m_store || !m_store->isOpen()) {
        return std::nullopt;
    }
    return m_store->mountById(id);
}

std::optional<MountInfo> MountRegistry::findOwnerMount(const QString &localPath) const
{
    const auto ownerId = m_router.findOwner(localPath);
    if (!ownerId) {
        return std::nullopt;
    }
    return mount(*ownerId);
}

} // namespace KIODrive

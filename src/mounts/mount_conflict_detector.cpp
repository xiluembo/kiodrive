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

#include "mount_conflict_detector.h"

#include "common/path_utils.h"

#include <QDir>
#include <QFileInfo>

namespace KIODrive
{

bool MountConflictDetector::localPathsOverlap(const QString &a, const QString &b)
{
    return PathUtils::isSameOrDescendant(a, b) || PathUtils::isSameOrDescendant(b, a);
}

bool MountConflictDetector::remoteWritableOverlap(const MountInfo &a, const MountInfo &b)
{
    if (a.readOnly && b.readOnly) {
        return false;
    }
    return PathUtils::isRemoteSameOrDescendant(a.remoteUrl, b.remoteUrl)
        || PathUtils::isRemoteSameOrDescendant(b.remoteUrl, a.remoteUrl);
}

bool MountConflictDetector::invalidUnderSyncRoot(const QString &localRelativePath,
                                                 const QString &syncRootPath)
{
    Q_UNUSED(syncRootPath);

    const QString normalized = PathUtils::normalizeLexical(localRelativePath);
    if (normalized.isEmpty() || normalized == QLatin1String(".") || normalized == QLatin1String("..")) {
        return true;
    }
    if (normalized.startsWith(QLatin1String(".."))) {
        return true;
    }

    const QFileInfo info(normalized);
    if (info.isAbsolute()) {
        return true;
    }

    // Reject drive-letter style paths that slipped through.
    if (normalized.size() >= 2 && normalized.at(1) == QLatin1Char(':')) {
        return true;
    }

    return false;
}

ValidationResult MountConflictDetector::validate(const MountInfo &candidate,
                                                 const QVector<MountInfo> &existing,
                                                 const QString &syncRootPath)
{
    if (candidate.name.trimmed().isEmpty()) {
        return ValidationResult::failure(ConflictKind::InvalidConfiguration,
                                         QStringLiteral("Mount name must not be empty."));
    }

    if (candidate.localRelativePath.trimmed().isEmpty()) {
        return ValidationResult::failure(ConflictKind::InvalidConfiguration,
                                         QStringLiteral("Local relative path must not be empty."));
    }

    if (!candidate.remoteUrl.isValid() || candidate.remoteUrl.scheme().isEmpty()) {
        return ValidationResult::failure(ConflictKind::InvalidConfiguration,
                                         QStringLiteral("Remote URL is invalid."));
    }

    if (invalidUnderSyncRoot(candidate.localRelativePath, syncRootPath)) {
        return ValidationResult::failure(
            ConflictKind::SyncRootAncestry,
            QStringLiteral(
                "Mount path must be a relative subdirectory of the KIODrive sync root; "
                "volume roots and parent traversal are not allowed."));
    }

    if (!syncRootPath.isEmpty()) {
        const QString combined =
            PathUtils::normalizeLexical(QDir(syncRootPath).filePath(candidate.localRelativePath));
        const QString rootKey = PathUtils::normalizedPathKey(syncRootPath);
        if (!PathUtils::isSameOrDescendant(rootKey, combined)
            || PathUtils::normalizedPathKey(combined) == rootKey) {
            return ValidationResult::failure(
                ConflictKind::SyncRootAncestry,
                QStringLiteral(
                    "Mount path must stay under the sync root and must not equal the sync root itself."));
        }
    }

    for (const MountInfo &other : existing) {
        if (!candidate.id.isNull() && candidate.id == other.id) {
            continue;
        }

        if (localPathsOverlap(candidate.localRelativePath, other.localRelativePath)) {
            return ValidationResult::failure(
                ConflictKind::LocalPathOverlap,
                QStringLiteral("Local path '%1' overlaps existing mount '%2' at '%3'.")
                    .arg(candidate.localRelativePath, other.name, other.localRelativePath));
        }

        if (remoteWritableOverlap(candidate, other)) {
            return ValidationResult::failure(
                ConflictKind::RemoteWritableOverlap,
                QStringLiteral(
                    "Remote URL '%1' overlaps writable mount '%2' at '%3'. "
                    "Overlapping writable remotes are not allowed.")
                    .arg(candidate.remoteUrl.toDisplayString(QUrl::RemovePassword),
                         other.name,
                         other.remoteUrl.toDisplayString(QUrl::RemovePassword)));
        }
    }

    return ValidationResult::success();
}

} // namespace KIODrive

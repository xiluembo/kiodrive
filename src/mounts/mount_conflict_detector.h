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

#ifndef KIODRIVE_MOUNTS_MOUNT_CONFLICT_DETECTOR_H
#define KIODRIVE_MOUNTS_MOUNT_CONFLICT_DETECTOR_H

#include "common/types.h"

#include <QString>
#include <QVector>

namespace KIODrive
{

enum class ConflictKind {
    None,
    LocalPathOverlap,
    RemoteWritableOverlap,
    SyncRootAncestry,
    InvalidConfiguration,
};

struct ValidationResult {
    bool ok = true;
    ConflictKind kind = ConflictKind::None;
    QString message;

    static ValidationResult success()
    {
        return {};
    }

    static ValidationResult failure(ConflictKind kind, const QString &message)
    {
        ValidationResult result;
        result.ok = false;
        result.kind = kind;
        result.message = message;
        return result;
    }
};

/**
 * Validates mount configurations against local path overlap, remote writable
 * URL overlap, and sync-root ancestry constraints.
 */
class MountConflictDetector
{
public:
    /**
     * Validate @p candidate against @p existing mounts and optional sync root.
     * @p syncRootPath is the absolute or relative sync-root directory; candidate
     * localRelativePath is interpreted under that root when checking ancestry.
     */
    static ValidationResult validate(const MountInfo &candidate,
                                     const QVector<MountInfo> &existing,
                                     const QString &syncRootPath = {});

    /** Local path ancestry/equality overlap between two relative mount paths. */
    static bool localPathsOverlap(const QString &a, const QString &b);

    /**
     * True when two remote URLs overlap and at least one mount is writable
     * (not read-only). Read-only overlaps are allowed.
     */
    static bool remoteWritableOverlap(const MountInfo &a, const MountInfo &b);

    /**
     * True when @p localRelativePath equals or escapes the sync root layout
     * (empty path, ".", "..", absolute path, or drive letter).
     */
    static bool invalidUnderSyncRoot(const QString &localRelativePath, const QString &syncRootPath);
};

} // namespace KIODrive

#endif // KIODRIVE_MOUNTS_MOUNT_CONFLICT_DETECTOR_H

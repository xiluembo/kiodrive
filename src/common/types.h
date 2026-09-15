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

#ifndef KIODRIVE_COMMON_TYPES_H
#define KIODRIVE_COMMON_TYPES_H

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QUuid>

#include <cstdint>

namespace KIODrive
{

enum class MountState {
    Disabled,
    Connecting,
    Online,
    Offline,
    AuthenticationRequired,
    Degraded,
    Conflict,
    Error,
    Disconnecting,
};

enum class ItemSyncState {
    InSync,
    Hydrating,
    Uploading,
    OutOfSync,
    Pinned,
    OnlineOnly,
    Conflict,
    DeletedRemote,
    DeletedLocal,
};

enum class OperationState {
    Prepared,
    SubmittedRemote,
    AcknowledgedCf,
    Committed,
    Compensating,
    Failed,
};

enum class ItemKind {
    File,
    Directory,
    Symlink,
    Unsupported,
};

struct ReadCapabilities {
    bool sequentialRead = true;
    bool randomRead = false;
    bool resumableRead = false;
    bool knownSize = false;
};

struct KioCapabilities {
    bool listing = false;
    bool reading = false;
    bool writing = false;
    bool mkdir = false;
    bool deleting = false;
    bool moving = false;
    bool rename = false;
    bool truncate = false;
    bool permissions = false;
    bool linking = false;
    bool opening = false;
    ReadCapabilities read;
};

struct MountInfo {
    QUuid id;
    QString name;
    QString localRelativePath;
    QUrl remoteUrl;
    QString protocol;
    MountState state = MountState::Disabled;
    KioCapabilities capabilities;
    bool readOnly = false;
};

QString mountStateToString(MountState state);
QString itemSyncStateToString(ItemSyncState state);

} // namespace KIODrive

#endif // KIODRIVE_COMMON_TYPES_H

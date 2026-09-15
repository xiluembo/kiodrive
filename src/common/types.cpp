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

#include "types.h"

namespace KIODrive
{

QString mountStateToString(MountState state)
{
    switch (state) {
    case MountState::Disabled:
        return QStringLiteral("Disabled");
    case MountState::Connecting:
        return QStringLiteral("Connecting");
    case MountState::Online:
        return QStringLiteral("Online");
    case MountState::Offline:
        return QStringLiteral("Offline");
    case MountState::AuthenticationRequired:
        return QStringLiteral("AuthenticationRequired");
    case MountState::Degraded:
        return QStringLiteral("Degraded");
    case MountState::Conflict:
        return QStringLiteral("Conflict");
    case MountState::Error:
        return QStringLiteral("Error");
    case MountState::Disconnecting:
        return QStringLiteral("Disconnecting");
    }
    return QStringLiteral("Unknown");
}

QString itemSyncStateToString(ItemSyncState state)
{
    switch (state) {
    case ItemSyncState::InSync:
        return QStringLiteral("InSync");
    case ItemSyncState::Hydrating:
        return QStringLiteral("Hydrating");
    case ItemSyncState::Uploading:
        return QStringLiteral("Uploading");
    case ItemSyncState::OutOfSync:
        return QStringLiteral("OutOfSync");
    case ItemSyncState::Pinned:
        return QStringLiteral("Pinned");
    case ItemSyncState::OnlineOnly:
        return QStringLiteral("OnlineOnly");
    case ItemSyncState::Conflict:
        return QStringLiteral("Conflict");
    case ItemSyncState::DeletedRemote:
        return QStringLiteral("DeletedRemote");
    case ItemSyncState::DeletedLocal:
        return QStringLiteral("DeletedLocal");
    }
    return QStringLiteral("Unknown");
}

} // namespace KIODrive

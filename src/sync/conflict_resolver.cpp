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

#include "conflict_resolver.h"

#include "common/logging.h"

#include <QSqlQuery>
#include <QUuid>

namespace KIODrive
{

ConflictResolver::ConflictResolver(MetadataStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

bool ConflictResolver::recordConflict(const QUuid &itemId, const QString &localRev, const QString &remoteRev)
{
    if (!m_store) {
        return false;
    }
    m_store->updateItemSyncState(itemId, ItemSyncState::Conflict);
    qCWarning(KIODRIVE_SYNC_LOG) << "Conflict recorded for item" << itemId << "local" << localRev << "remote"
                                 << remoteRev;
    Q_EMIT conflictRecorded(itemId);
    return true;
}

bool ConflictResolver::applyPolicy(const QUuid &conflictId, ConflictPolicy policy)
{
    Q_UNUSED(conflictId);
    Q_UNUSED(policy);
    // Policies are applied by SyncEngineCore after user choice.
    return true;
}

} // namespace KIODrive

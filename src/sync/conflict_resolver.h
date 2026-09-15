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

#ifndef KIODRIVE_SYNC_CONFLICT_RESOLVER_H
#define KIODRIVE_SYNC_CONFLICT_RESOLVER_H

#include "metadata/metadata_store.h"

#include <QObject>
#include <QUuid>

namespace KIODrive
{

enum class ConflictPolicy {
    PreserveBoth,
    Retry,
    UserIntervention,
    PreferLocal,
    PreferRemote,
};

class ConflictResolver : public QObject
{
    Q_OBJECT

public:
    explicit ConflictResolver(MetadataStore *store, QObject *parent = nullptr);

    bool recordConflict(const QUuid &itemId, const QString &localRev, const QString &remoteRev);
    bool applyPolicy(const QUuid &conflictId, ConflictPolicy policy);

Q_SIGNALS:
    void conflictRecorded(const QUuid &itemId);

private:
    MetadataStore *m_store = nullptr;
};

} // namespace KIODrive

#endif // KIODRIVE_SYNC_CONFLICT_RESOLVER_H

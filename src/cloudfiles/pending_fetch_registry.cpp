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

#include "pending_fetch_registry.h"

#include "common/logging.h"

namespace KIODrive
{

QString PendingFetchRegistry::keyFor(PendingFetchKind kind, quint64 requestKey)
{
    const QLatin1Char sep('|');
    return (kind == PendingFetchKind::Data ? QStringLiteral("data") : QStringLiteral("ph")) + sep
        + QString::number(requestKey);
}

QUuid PendingFetchRegistry::begin(PendingFetchKind kind, const QString &relativePath, quint64 requestKey)
{
    QMutexLocker lock(&m_mutex);
    const QString key = keyFor(kind, requestKey);
    if (m_pending.contains(key)) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "Overwriting pending fetch entry" << key << "path" << relativePath;
    }
    PendingFetch fetch;
    fetch.operationId = QUuid::createUuid();
    fetch.kind = kind;
    fetch.relativePath = relativePath;
    fetch.requestKey = requestKey;
    fetch.cancelled = false;
    m_pending.insert(key, fetch);
    return fetch.operationId;
}

bool PendingFetchRegistry::markCancelled(PendingFetchKind kind, quint64 requestKey, QUuid *operationId)
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_pending.find(keyFor(kind, requestKey));
    if (it == m_pending.end()) {
        return false;
    }
    it->cancelled = true;
    if (operationId) {
        *operationId = it->operationId;
    }
    return true;
}

bool PendingFetchRegistry::isCancelled(PendingFetchKind kind, quint64 requestKey) const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_pending.constFind(keyFor(kind, requestKey));
    return it != m_pending.constEnd() && it->cancelled;
}

bool PendingFetchRegistry::takeIfActive(PendingFetchKind kind, quint64 requestKey, PendingFetch *out)
{
    QMutexLocker lock(&m_mutex);
    const QString key = keyFor(kind, requestKey);
    const auto it = m_pending.find(key);
    if (it == m_pending.end()) {
        return false;
    }
    if (out) {
        *out = *it;
    }
    const bool active = !it->cancelled;
    m_pending.erase(it);
    return active;
}

void PendingFetchRegistry::clear(PendingFetchKind kind, quint64 requestKey)
{
    QMutexLocker lock(&m_mutex);
    m_pending.remove(keyFor(kind, requestKey));
}

} // namespace KIODrive

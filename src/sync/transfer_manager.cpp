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

#include "transfer_manager.h"

#include "common/logging.h"

#include <QUuid>
#include <algorithm>
#include <functional>

namespace KIODrive
{

TransferManager::TransferManager(QObject *parent)
    : QObject(parent)
{
}

void TransferManager::setGlobalConcurrency(int limit)
{
    m_globalLimit = qMax(1, limit);
}

QUuid TransferManager::enqueue(TransferPriority priority, std::function<void()> start)
{
    TransferJob job;
    job.id = QUuid::createUuid();
    job.priority = priority;
    job.start = std::move(start);
    m_queue.push_back(job);
    std::sort(m_queue.begin(), m_queue.end(), [](const TransferJob &a, const TransferJob &b) {
        return static_cast<int>(a.priority) > static_cast<int>(b.priority);
    });
    pump();
    return job.id;
}

void TransferManager::cancel(const QUuid &id)
{
    m_queue.erase(std::remove_if(m_queue.begin(),
                                 m_queue.end(),
                                 [&](const TransferJob &job) {
                                     return job.id == id;
                                 }),
                  m_queue.end());
}

void TransferManager::notifyFinished(const QUuid &id)
{
    Q_UNUSED(id);
    m_active = qMax(0, m_active - 1);
    Q_EMIT transferFinished(id);
    pump();
}

void TransferManager::pump()
{
    while (m_active < m_globalLimit && !m_queue.isEmpty()) {
        TransferJob job = m_queue.takeFirst();
        ++m_active;
        Q_EMIT transferStarted(job.id);
        qCDebug(KIODRIVE_SYNC_LOG) << "Starting transfer" << job.id;
        if (job.start) {
            job.start();
        }
    }
}

} // namespace KIODrive

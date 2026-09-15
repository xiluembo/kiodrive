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

#ifndef KIODRIVE_SYNC_TRANSFER_MANAGER_H
#define KIODRIVE_SYNC_TRANSFER_MANAGER_H

#include "kio/kio_backend.h"

#include <QList>
#include <QObject>
#include <QQueue>
#include <QUuid>

#include <functional>

namespace KIODrive
{

enum class TransferPriority {
    Background = 0,
    Normal = 50,
    ForegroundHydration = 100,
};

struct TransferJob {
    QUuid id;
    TransferPriority priority = TransferPriority::Normal;
    std::function<void()> start;
};

class TransferManager : public QObject
{
    Q_OBJECT

public:
    explicit TransferManager(QObject *parent = nullptr);

    void setGlobalConcurrency(int limit);
    QUuid enqueue(TransferPriority priority, std::function<void()> start);
    void cancel(const QUuid &id);
    void notifyFinished(const QUuid &id);

Q_SIGNALS:
    void transferStarted(const QUuid &id);
    void transferFinished(const QUuid &id);

private:
    void pump();

    int m_globalLimit = 4;
    int m_active = 0;
    QList<TransferJob> m_queue;
};

} // namespace KIODrive

#endif // KIODRIVE_SYNC_TRANSFER_MANAGER_H

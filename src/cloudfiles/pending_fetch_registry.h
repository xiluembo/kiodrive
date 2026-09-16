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

#ifndef KIODRIVE_CLOUDFILES_PENDING_FETCH_REGISTRY_H
#define KIODRIVE_CLOUDFILES_PENDING_FETCH_REGISTRY_H

#include <QHash>
#include <QMutex>
#include <QString>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winternl.h>
#include <cfapi.h>
#endif

namespace KIODrive
{

enum class PendingFetchKind {
    Data,
    Placeholders,
};

struct PendingFetch {
    QUuid operationId;
    PendingFetchKind kind = PendingFetchKind::Data;
    QString relativePath;
    quint64 requestKey = 0;
    bool cancelled = false;
#ifdef Q_OS_WIN
    CF_CONNECTION_KEY connectionKey{};
    CF_TRANSFER_KEY transferKey{};
#endif
};

/**
 * Tracks in-flight CFAPI FETCH_DATA / FETCH_PLACEHOLDERS so CANCEL_* can
 * abort work before CfExecute completes the transfer.
 */
class PendingFetchRegistry
{
public:
    QUuid begin(PendingFetchKind kind, const QString &relativePath, quint64 requestKey
#ifdef Q_OS_WIN
                ,
                CF_CONNECTION_KEY connectionKey,
                CF_TRANSFER_KEY transferKey
#endif
    );

    bool markCancelled(PendingFetchKind kind, quint64 requestKey);
    bool isCancelled(PendingFetchKind kind, quint64 requestKey) const;
    bool takeIfActive(PendingFetchKind kind, quint64 requestKey, PendingFetch *out);
    void clear(PendingFetchKind kind, quint64 requestKey);

private:
    static QString keyFor(PendingFetchKind kind, quint64 requestKey);

    mutable QMutex m_mutex;
    QHash<QString, PendingFetch> m_pending;
};

} // namespace KIODrive

#endif // KIODRIVE_CLOUDFILES_PENDING_FETCH_REGISTRY_H

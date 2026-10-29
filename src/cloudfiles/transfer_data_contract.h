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

#ifndef KIODRIVE_CLOUDFILES_TRANSFER_DATA_CONTRACT_H
#define KIODRIVE_CLOUDFILES_TRANSFER_DATA_CONTRACT_H

#include <QByteArray>
#include <QString>

#include <QtGlobal>

namespace KIODrive
{

/**
 * Portable CFAPI TRANSFER_DATA completion rules for FETCH_DATA.
 * Failure and cancellation must still report the full requested range length.
 * Success requires an exact byte match for that range.
 */
struct TransferDataCompletion {
    enum class Status {
        Success,
        Failure,
        Cancelled,
    };

    Status status = Status::Failure;
    quint64 offset = 0;
    quint64 length = 0;
    QByteArray buffer;
    QString diagnostic;
};

inline TransferDataCompletion planTransferDataCompletion(bool cancelled,
                                                         bool hydrationOk,
                                                         quint64 offset,
                                                         quint64 requestedLength,
                                                         const QByteArray &data,
                                                         const QString &hydrationError = {})
{
    TransferDataCompletion completion;
    completion.offset = offset;
    completion.length = requestedLength;

    if (cancelled) {
        completion.status = TransferDataCompletion::Status::Cancelled;
        return completion;
    }

    if (!hydrationOk) {
        completion.status = TransferDataCompletion::Status::Failure;
        completion.diagnostic =
            hydrationError.isEmpty() ? QStringLiteral("Hydration failed") : hydrationError;
        return completion;
    }

    if (static_cast<quint64>(data.size()) != requestedLength) {
        completion.status = TransferDataCompletion::Status::Failure;
        completion.diagnostic =
            QStringLiteral("Short read: expected %1 bytes, got %2").arg(requestedLength).arg(data.size());
        return completion;
    }

    completion.status = TransferDataCompletion::Status::Success;
    completion.buffer = data;
    return completion;
}

} // namespace KIODrive

#endif // KIODRIVE_CLOUDFILES_TRANSFER_DATA_CONTRACT_H

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

#include "file_identity.h"

#include <QDataStream>
#include <QIODevice>
#include <QtEndian>

namespace KIODrive
{

QByteArray FileIdentityV1::serialize() const
{
    QByteArray blob;
    blob.reserve(1 + 1 + 16 + 16 + 8);
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << version << flags;
    const QByteArray mountBytes = mountId.toRfc4122();
    const QByteArray itemBytes = itemId.toRfc4122();
    stream.writeRawData(mountBytes.constData(), mountBytes.size());
    stream.writeRawData(itemBytes.constData(), itemBytes.size());
    stream << revisionSeq;
    return blob;
}

bool FileIdentityV1::deserialize(const QByteArray &blob, FileIdentityV1 *out)
{
    if (!out || blob.size() < 42) {
        return false;
    }

    QDataStream stream(blob);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint8 version = 0;
    quint8 flags = 0;
    stream >> version >> flags;
    if (version != CurrentVersion) {
        return false;
    }

    char mountRaw[16];
    char itemRaw[16];
    if (stream.readRawData(mountRaw, 16) != 16 || stream.readRawData(itemRaw, 16) != 16) {
        return false;
    }

    quint64 revisionSeq = 0;
    stream >> revisionSeq;
    if (stream.status() != QDataStream::Ok) {
        return false;
    }

    out->version = version;
    out->flags = flags;
    out->mountId = QUuid::fromRfc4122(QByteArray(mountRaw, 16));
    out->itemId = QUuid::fromRfc4122(QByteArray(itemRaw, 16));
    out->revisionSeq = revisionSeq;
    return true;
}

} // namespace KIODrive

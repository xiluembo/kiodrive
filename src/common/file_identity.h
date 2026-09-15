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

#ifndef KIODRIVE_COMMON_FILE_IDENTITY_H
#define KIODRIVE_COMMON_FILE_IDENTITY_H

#include <QByteArray>
#include <QUuid>

#include <cstdint>

namespace KIODrive
{

/**
 * Compact Cloud Files FileIdentity blob (v1).
 *
 * Layout (little-endian):
 *   version:u8 | flags:u8 | mountUuid:16 | itemUuid:16 | revisionSeq:u64
 *
 * Richer metadata lives in SQLite; this blob must stay within CFAPI 4 KiB limit.
 */
struct FileIdentityV1 {
    static constexpr quint8 CurrentVersion = 1;

    quint8 version = CurrentVersion;
    quint8 flags = 0;
    QUuid mountId;
    QUuid itemId;
    quint64 revisionSeq = 0;

    QByteArray serialize() const;
    static bool deserialize(const QByteArray &blob, FileIdentityV1 *out);
};

} // namespace KIODrive

#endif // KIODRIVE_COMMON_FILE_IDENTITY_H

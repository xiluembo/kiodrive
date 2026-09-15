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

#ifndef KIODRIVE_KIO_PROTOCOL_REGISTRY_H
#define KIODRIVE_KIO_PROTOCOL_REGISTRY_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace KIODrive
{

struct ProtocolInfoEntry {
    QString name;
    QString description;
    bool filesystemLike = false;
};

/**
 * Enumerates KIO protocols and filters filesystem-like candidates suitable
 * for Cloud Files mounts.
 */
class ProtocolRegistry
{
public:
    /** All known protocol names from KProtocolInfo::protocols(). */
    static QStringList allProtocols();

    /**
     * Filesystem-like protocols: supportsListing && supportsReading,
     * not filter/helper, prefer T_FILESYSTEM via KProtocolManager::inputType.
     */
    static QStringList filesystemProtocols();

    static bool isFilesystemLike(const QString &protocol);

    static QVector<ProtocolInfoEntry> enumerate();
};

} // namespace KIODrive

#endif // KIODRIVE_KIO_PROTOCOL_REGISTRY_H

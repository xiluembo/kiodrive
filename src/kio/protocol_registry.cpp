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

#include "protocol_registry.h"

#include <KProtocolInfo>
#include <KProtocolManager>

#include <QUrl>

namespace KIODrive
{

namespace
{

QUrl protocolProbeUrl(const QString &protocol)
{
    QUrl url;
    url.setScheme(protocol);
    url.setPath(QStringLiteral("/"));
    return url;
}

} // namespace

QStringList ProtocolRegistry::allProtocols()
{
    return KProtocolInfo::protocols();
}

bool ProtocolRegistry::isFilesystemLike(const QString &protocol)
{
    if (protocol.isEmpty() || !KProtocolInfo::isKnownProtocol(protocol)) {
        return false;
    }
    if (KProtocolInfo::isFilterProtocol(protocol) || KProtocolInfo::isHelperProtocol(protocol)) {
        return false;
    }

    const QUrl url = protocolProbeUrl(protocol);
    if (!KProtocolManager::supportsListing(url) || !KProtocolManager::supportsReading(url)) {
        return false;
    }

    // Prefer T_FILESYSTEM; allow T_NONE when listing+reading already passed.
    const KProtocolInfo::Type input = KProtocolManager::inputType(url);
    if (input == KProtocolInfo::T_STREAM || input == KProtocolInfo::T_ERROR) {
        return false;
    }

    return input == KProtocolInfo::T_FILESYSTEM || input == KProtocolInfo::T_NONE;
}

QStringList ProtocolRegistry::filesystemProtocols()
{
    QStringList result;
    const QStringList protocols = allProtocols();
    for (const QString &protocol : protocols) {
        if (isFilesystemLike(protocol)) {
            result.append(protocol);
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QVector<ProtocolInfoEntry> ProtocolRegistry::enumerate()
{
    QVector<ProtocolInfoEntry> entries;
    const QStringList protocols = allProtocols();
    entries.reserve(protocols.size());
    for (const QString &protocol : protocols) {
        ProtocolInfoEntry entry;
        entry.name = protocol;
        entry.description = KProtocolInfo::docPath(protocol);
        if (entry.description.isEmpty()) {
            entry.description = KProtocolInfo::protocolClass(protocol);
        }
        entry.filesystemLike = isFilesystemLike(protocol);
        entries.append(entry);
    }
    return entries;
}

} // namespace KIODrive

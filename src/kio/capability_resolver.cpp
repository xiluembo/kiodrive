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

#include "capability_resolver.h"

#include <KProtocolInfo>
#include <KProtocolManager>

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

KioCapabilities CapabilityResolver::resolve(const QUrl &url)
{
    KioCapabilities caps;
    if (url.scheme().isEmpty() || !KProtocolInfo::isKnownProtocol(url)) {
        return caps;
    }

    caps.listing = KProtocolManager::supportsListing(url);
    caps.reading = KProtocolManager::supportsReading(url);
    caps.writing = KProtocolManager::supportsWriting(url);
    caps.mkdir = KProtocolManager::supportsMakeDir(url);
    caps.deleting = KProtocolManager::supportsDeleting(url);
    caps.moving = KProtocolManager::supportsMoving(url);
    caps.rename = caps.moving;
    caps.permissions = KProtocolManager::supportsPermissions(url);
    caps.linking = KProtocolManager::supportsLinking(url);
    caps.opening = KProtocolManager::supportsOpening(url);
    caps.truncate = KProtocolManager::supportsTruncating(url);

    caps.read.sequentialRead = caps.reading;
    // Random reads when FileJob opening is supported.
    caps.read.randomRead = caps.opening;
    caps.read.resumableRead = caps.opening;
    caps.read.knownSize = caps.reading;

    return caps;
}

KioCapabilities CapabilityResolver::resolveProtocol(const QString &protocol)
{
    return resolve(protocolProbeUrl(protocol));
}

} // namespace KIODrive

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

#ifndef KIODRIVE_KIO_CAPABILITY_RESOLVER_H
#define KIODRIVE_KIO_CAPABILITY_RESOLVER_H

#include "kio_capabilities.h"

#include <QUrl>

namespace KIODrive
{

/**
 * Fills KioCapabilities from KProtocolManager / KProtocolInfo for a URL.
 */
class CapabilityResolver
{
public:
    static KioCapabilities resolve(const QUrl &url);
    static KioCapabilities resolveProtocol(const QString &protocol);
};

} // namespace KIODrive

#endif // KIODRIVE_KIO_CAPABILITY_RESOLVER_H

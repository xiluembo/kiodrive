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

#include "error_mapper.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifndef ERROR_ACCESS_DENIED
#define ERROR_ACCESS_DENIED 5
#endif
#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND 2
#endif
#ifndef ERROR_NOT_SUPPORTED
#define ERROR_NOT_SUPPORTED 50
#endif
#ifndef ERROR_NETWORK_UNREACHABLE
#define ERROR_NETWORK_UNREACHABLE 1231
#endif
#ifndef ERROR_LOGON_FAILURE
#define ERROR_LOGON_FAILURE 1326
#endif
#ifndef ERROR_TIMEOUT
#define ERROR_TIMEOUT 1460
#endif
#ifndef ERROR_DISK_FULL
#define ERROR_DISK_FULL 112
#endif

namespace KIODrive
{

MappedError ErrorMapper::fromKioError(int kioError, const QString &kioText)
{
    // KIO::ERR_* numeric values from KIO Core. Keep mapping defensive.
    MappedError mapped;
    mapped.diagnostic = QStringLiteral("kioError=%1 text=%2").arg(kioError).arg(kioText);

    switch (kioError) {
    case 0:
        mapped.userMessage = QStringLiteral("Success");
        return mapped;
    case 101: // ERR_DOES_NOT_EXIST-ish historically; keep generic
    case 102:
        return notFound();
    case 103: // access denied family
        return accessDenied();
    case 111: // could not login
    case 112:
        return authenticationRequired();
    case 128: // timeout-ish
        return timeout();
    case 149: // unsupported action
        return unsupported();
    default:
        break;
    }

    if (kioText.contains(QStringLiteral("auth"), Qt::CaseInsensitive)
        || kioText.contains(QStringLiteral("password"), Qt::CaseInsensitive)
        || kioText.contains(QStringLiteral("passwd"), Qt::CaseInsensitive)) {
        return authenticationRequired();
    }

    mapped.win32Error = ERROR_NETWORK_UNREACHABLE;
    mapped.userMessage = QStringLiteral("The remote resource could not be accessed.");
    return mapped;
}

MappedError ErrorMapper::authenticationRequired()
{
    MappedError mapped;
    mapped.win32Error = ERROR_LOGON_FAILURE;
    mapped.userMessage = QStringLiteral("Authentication is required for this mount.");
    mapped.diagnostic = QStringLiteral("AuthenticationRequired");
    return mapped;
}

MappedError ErrorMapper::remoteUnavailable()
{
    MappedError mapped;
    mapped.win32Error = ERROR_NETWORK_UNREACHABLE;
    mapped.userMessage = QStringLiteral("The remote host is unavailable.");
    mapped.diagnostic = QStringLiteral("RemoteUnavailable");
    return mapped;
}

MappedError ErrorMapper::notFound()
{
    MappedError mapped;
    mapped.win32Error = ERROR_FILE_NOT_FOUND;
    mapped.userMessage = QStringLiteral("The remote file or directory was not found.");
    mapped.diagnostic = QStringLiteral("NotFound");
    return mapped;
}

MappedError ErrorMapper::accessDenied()
{
    MappedError mapped;
    mapped.win32Error = ERROR_ACCESS_DENIED;
    mapped.userMessage = QStringLiteral("Access to the remote resource was denied.");
    mapped.diagnostic = QStringLiteral("AccessDenied");
    return mapped;
}

MappedError ErrorMapper::unsupported()
{
    MappedError mapped;
    mapped.win32Error = ERROR_NOT_SUPPORTED;
    mapped.userMessage = QStringLiteral("This operation is not supported by the remote protocol.");
    mapped.diagnostic = QStringLiteral("Unsupported");
    return mapped;
}

MappedError ErrorMapper::timeout()
{
    MappedError mapped;
    mapped.win32Error = ERROR_TIMEOUT;
    mapped.userMessage = QStringLiteral("The remote operation timed out.");
    mapped.diagnostic = QStringLiteral("Timeout");
    return mapped;
}

} // namespace KIODrive

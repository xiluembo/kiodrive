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

#ifndef KIODRIVE_COMMON_ERROR_MAPPER_H
#define KIODRIVE_COMMON_ERROR_MAPPER_H

#include <QString>

#include <cstdint>

namespace KIODrive
{

struct MappedError {
    qint32 win32Error = 0;
    qint32 hresult = 0;
    QString userMessage;
    QString diagnostic;
};

/**
 * Maps KIO / provider errors to Windows-facing codes and user messages.
 * Never include credentials in diagnostic strings.
 */
class ErrorMapper
{
public:
    static MappedError fromKioError(int kioError, const QString &kioText = {});
    static MappedError authenticationRequired();
    static MappedError remoteUnavailable();
    static MappedError notFound();
    static MappedError accessDenied();
    static MappedError unsupported();
    static MappedError timeout();
};

} // namespace KIODrive

#endif // KIODRIVE_COMMON_ERROR_MAPPER_H

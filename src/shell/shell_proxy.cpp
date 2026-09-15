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

#include "shell_proxy.h"

#include "common/logging.h"

namespace KIODrive
{

bool ShellProxy::startLocalServer()
{
    qCInfo(KIODRIVE_LOG) << "KIODrive.Shell local server placeholder started";
    return true;
}

QString ShellProxy::queryItemStatus(const QString &absolutePath) const
{
    Q_UNUSED(absolutePath);
    return QStringLiteral("InSync");
}

} // namespace KIODrive

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

#ifndef KIODRIVE_SHELL_SHELL_PROXY_H
#define KIODRIVE_SHELL_SHELL_PROXY_H

#include <QString>

namespace KIODrive
{

/**
 * Lightweight COM local-server proxy. Does not load Qt/KF6 into Explorer;
 * forwards thumbnail/status/custom-state requests to KIODrive.SyncEngine via named pipe.
 */
class ShellProxy
{
public:
    bool startLocalServer();
    QString queryItemStatus(const QString &absolutePath) const;
};

} // namespace KIODrive

#endif // KIODRIVE_SHELL_SHELL_PROXY_H

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

#ifndef KIODRIVE_METADATA_SCHEMA_H
#define KIODRIVE_METADATA_SCHEMA_H

#include <QStringList>

namespace KIODrive
{

class Schema
{
public:
    static int currentVersion();
    static QStringList migrationSql(int fromVersion);
};

} // namespace KIODrive

#endif // KIODRIVE_METADATA_SCHEMA_H

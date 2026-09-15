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

#ifndef KIODRIVE_CLOUDFILES_PLACEHOLDER_MANAGER_H
#define KIODRIVE_CLOUDFILES_PLACEHOLDER_MANAGER_H

#include "common/file_identity.h"
#include "metadata/metadata_store.h"

#include <QString>
#include <QVector>

namespace KIODrive
{

struct PlaceholderCreateRequest {
    QString relativePath;
    QString fileName;
    bool isDirectory = false;
    qint64 fileSize = 0;
    FileIdentityV1 identity;
};

class PlaceholderManager
{
public:
    explicit PlaceholderManager(const QString &syncRootPath);

    bool createPlaceholders(const QString &parentRelativePath, const QVector<PlaceholderCreateRequest> &items);
    bool ensureMountRootPlaceholder(const MountInfo &mount);
    bool enableOnDemandPopulation(const QString &relativePath = {});
    bool updatePlaceholderIdentity(const QString &relativePath, const FileIdentityV1 &identity);
    bool setInSync(const QString &relativePath, bool inSync);
    bool dehydrate(const QString &relativePath);
    bool setPinState(const QString &relativePath, bool pinned);

private:
    QString absolutePath(const QString &relativePath) const;

    QString m_syncRootPath;
};

} // namespace KIODrive

#endif // KIODRIVE_CLOUDFILES_PLACEHOLDER_MANAGER_H

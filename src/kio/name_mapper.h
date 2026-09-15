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

#ifndef KIODRIVE_KIO_NAME_MAPPER_H
#define KIODRIVE_KIO_NAME_MAPPER_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace KIODrive
{

enum class NameMappingReason {
    None,
    ReservedDeviceName,
    ForbiddenCharacters,
    TrailingDotsOrSpaces,
    CaseCollision,
    EmptyOrDotName,
    Combined,
};

struct NameMapping {
    QString remoteName;
    QString windowsName;
    NameMappingReason reason = NameMappingReason::None;

    bool changed() const
    {
        return reason != NameMappingReason::None || remoteName != windowsName;
    }
};

/**
 * Maps remote file/directory names to Windows-safe names and keeps a reversible
 * mapping with a persisted reason.
 */
class NameMapper
{
public:
    NameMapper() = default;

    /** Map @p remoteName given sibling Windows names already used in the folder. */
    NameMapping mapToWindows(const QString &remoteName, const QSet<QString> &usedWindowsNames = {});

    /** Record a mapping (e.g. loaded from metadata). */
    void remember(const NameMapping &mapping);

    /** Reverse lookup: Windows name → remote name within this mapper scope. */
    QString toRemoteName(const QString &windowsName) const;

    /** Forward lookup of a previously remembered mapping. */
    QString toWindowsName(const QString &remoteName) const;

    std::optional<NameMappingReason> reasonForWindowsName(const QString &windowsName) const;
    std::optional<NameMappingReason> reasonForRemoteName(const QString &remoteName) const;

    QVector<NameMapping> allMappings() const;
    void clear();

    static bool isReservedDeviceName(const QString &baseName);
    static bool containsForbiddenCharacters(const QString &name);
    static bool hasTrailingDotOrSpace(const QString &name);
    static QString reasonToString(NameMappingReason reason);

private:
    static QString sanitizeBase(const QString &remoteName, NameMappingReason *reason);
    static QString makeUnique(const QString &candidate, const QSet<QString> &used);

    QHash<QString, QString> m_remoteToWindows;
    QHash<QString, QString> m_windowsToRemote;
    QHash<QString, NameMappingReason> m_reasonByRemote;
};

} // namespace KIODrive

#endif // KIODRIVE_KIO_NAME_MAPPER_H

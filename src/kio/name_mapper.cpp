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

#include "name_mapper.h"

#include <QFileInfo>

namespace KIODrive
{

namespace
{

const QSet<QString> &reservedNames()
{
    static const QSet<QString> names = {
        QStringLiteral("con"),  QStringLiteral("prn"),  QStringLiteral("aux"),  QStringLiteral("nul"),
        QStringLiteral("com1"), QStringLiteral("com2"), QStringLiteral("com3"), QStringLiteral("com4"),
        QStringLiteral("com5"), QStringLiteral("com6"), QStringLiteral("com7"), QStringLiteral("com8"),
        QStringLiteral("com9"), QStringLiteral("lpt1"), QStringLiteral("lpt2"), QStringLiteral("lpt3"),
        QStringLiteral("lpt4"), QStringLiteral("lpt5"), QStringLiteral("lpt6"), QStringLiteral("lpt7"),
        QStringLiteral("lpt8"), QStringLiteral("lpt9"),
    };
    return names;
}

bool caseInsensitiveContains(const QSet<QString> &used, const QString &candidate)
{
    const QString key = candidate.toLower();
    for (const QString &existing : used) {
        if (existing.toLower() == key) {
            return true;
        }
    }
    return false;
}

} // namespace

bool NameMapper::isReservedDeviceName(const QString &baseName)
{
    const QString stem = QFileInfo(baseName).completeBaseName().toLower();
    // Also treat "CON.txt" style: Windows reserves the stem before first dot.
    const int dot = baseName.indexOf(QLatin1Char('.'));
    const QString first = (dot < 0 ? baseName : baseName.left(dot)).toLower();
    return reservedNames().contains(stem) || reservedNames().contains(first);
}

bool NameMapper::containsForbiddenCharacters(const QString &name)
{
    for (const QChar ch : name) {
        const ushort u = ch.unicode();
        if (u < 0x20) {
            return true;
        }
        switch (ch.toLatin1()) {
        case '<':
        case '>':
        case ':':
        case '"':
        case '/':
        case '\\':
        case '|':
        case '?':
        case '*':
            return true;
        default:
            break;
        }
    }
    return false;
}

bool NameMapper::hasTrailingDotOrSpace(const QString &name)
{
    return name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '));
}

QString NameMapper::reasonToString(NameMappingReason reason)
{
    switch (reason) {
    case NameMappingReason::None:
        return QStringLiteral("none");
    case NameMappingReason::ReservedDeviceName:
        return QStringLiteral("reserved-device-name");
    case NameMappingReason::ForbiddenCharacters:
        return QStringLiteral("forbidden-characters");
    case NameMappingReason::TrailingDotsOrSpaces:
        return QStringLiteral("trailing-dots-or-spaces");
    case NameMappingReason::CaseCollision:
        return QStringLiteral("case-collision");
    case NameMappingReason::EmptyOrDotName:
        return QStringLiteral("empty-or-dot-name");
    case NameMappingReason::Combined:
        return QStringLiteral("combined");
    }
    return QStringLiteral("unknown");
}

QString NameMapper::sanitizeBase(const QString &remoteName, NameMappingReason *reason)
{
    NameMappingReason localReason = NameMappingReason::None;
    QString result = remoteName;

    if (result.isEmpty() || result == QLatin1String(".") || result == QLatin1String("..")) {
        result = QStringLiteral("_unnamed");
        localReason = NameMappingReason::EmptyOrDotName;
    }

    if (containsForbiddenCharacters(result)) {
        QString cleaned;
        cleaned.reserve(result.size());
        for (const QChar ch : result) {
            const ushort u = ch.unicode();
            bool forbidden = u < 0x20;
            if (!forbidden) {
                switch (ch.toLatin1()) {
                case '<':
                case '>':
                case ':':
                case '"':
                case '/':
                case '\\':
                case '|':
                case '?':
                case '*':
                    forbidden = true;
                    break;
                default:
                    break;
                }
            }
            cleaned.append(forbidden ? QLatin1Char('_') : ch);
        }
        result = cleaned;
        localReason = (localReason == NameMappingReason::None) ? NameMappingReason::ForbiddenCharacters
                                                               : NameMappingReason::Combined;
    }

    while (hasTrailingDotOrSpace(result)) {
        result.chop(1);
        if (localReason == NameMappingReason::None) {
            localReason = NameMappingReason::TrailingDotsOrSpaces;
        } else if (localReason != NameMappingReason::TrailingDotsOrSpaces) {
            localReason = NameMappingReason::Combined;
        }
    }
    if (result.isEmpty()) {
        result = QStringLiteral("_unnamed");
        localReason = NameMappingReason::Combined;
    }

    if (isReservedDeviceName(result)) {
        result = QLatin1Char('_') + result;
        localReason = (localReason == NameMappingReason::None) ? NameMappingReason::ReservedDeviceName
                                                               : NameMappingReason::Combined;
    }

    if (reason) {
        *reason = localReason;
    }
    return result;
}

QString NameMapper::makeUnique(const QString &candidate, const QSet<QString> &used)
{
    if (!caseInsensitiveContains(used, candidate)) {
        return candidate;
    }

    const QFileInfo info(candidate);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();
    for (int i = 1; i < 10000; ++i) {
        QString next = base + QLatin1Char('~') + QString::number(i);
        if (!suffix.isEmpty()) {
            next += QLatin1Char('.') + suffix;
        }
        if (!caseInsensitiveContains(used, next)) {
            return next;
        }
    }
    return candidate + QStringLiteral("~x");
}

NameMapping NameMapper::mapToWindows(const QString &remoteName, const QSet<QString> &usedWindowsNames)
{
    // Return remembered mapping when present.
    const auto remembered = m_remoteToWindows.constFind(remoteName);
    if (remembered != m_remoteToWindows.cend()) {
        NameMapping mapping;
        mapping.remoteName = remoteName;
        mapping.windowsName = *remembered;
        mapping.reason = m_reasonByRemote.value(remoteName, NameMappingReason::None);
        return mapping;
    }

    NameMappingReason reason = NameMappingReason::None;
    QString candidate = sanitizeBase(remoteName, &reason);

    QSet<QString> used = usedWindowsNames;
    // Also treat already-mapped windows names as used.
    for (auto it = m_windowsToRemote.cbegin(); it != m_windowsToRemote.cend(); ++it) {
        used.insert(it.key());
    }

    const QString unique = makeUnique(candidate, used);
    if (unique.compare(candidate, Qt::CaseInsensitive) != 0
        || (unique != candidate && caseInsensitiveContains(usedWindowsNames, candidate))) {
        reason = (reason == NameMappingReason::None) ? NameMappingReason::CaseCollision : NameMappingReason::Combined;
    } else if (unique != candidate) {
        reason = (reason == NameMappingReason::None) ? NameMappingReason::CaseCollision : NameMappingReason::Combined;
    }

    // Detect pure case collision against siblings when sanitize left name intact.
    if (reason == NameMappingReason::None && caseInsensitiveContains(usedWindowsNames, candidate)
        && !usedWindowsNames.contains(candidate)) {
        reason = NameMappingReason::CaseCollision;
    }

    NameMapping mapping;
    mapping.remoteName = remoteName;
    mapping.windowsName = unique;
    mapping.reason = (unique == remoteName) ? NameMappingReason::None : reason;
    if (unique != remoteName && mapping.reason == NameMappingReason::None) {
        mapping.reason = NameMappingReason::Combined;
    }

    remember(mapping);
    return mapping;
}

void NameMapper::remember(const NameMapping &mapping)
{
    if (mapping.remoteName.isEmpty() || mapping.windowsName.isEmpty()) {
        return;
    }
    m_remoteToWindows.insert(mapping.remoteName, mapping.windowsName);
    m_windowsToRemote.insert(mapping.windowsName, mapping.remoteName);
    m_reasonByRemote.insert(mapping.remoteName, mapping.reason);
}

QString NameMapper::toRemoteName(const QString &windowsName) const
{
    return m_windowsToRemote.value(windowsName);
}

QString NameMapper::toWindowsName(const QString &remoteName) const
{
    return m_remoteToWindows.value(remoteName);
}

std::optional<NameMappingReason> NameMapper::reasonForWindowsName(const QString &windowsName) const
{
    const QString remote = toRemoteName(windowsName);
    if (remote.isEmpty()) {
        return std::nullopt;
    }
    return reasonForRemoteName(remote);
}

std::optional<NameMappingReason> NameMapper::reasonForRemoteName(const QString &remoteName) const
{
    const auto it = m_reasonByRemote.constFind(remoteName);
    if (it == m_reasonByRemote.cend()) {
        return std::nullopt;
    }
    return *it;
}

QVector<NameMapping> NameMapper::allMappings() const
{
    QVector<NameMapping> out;
    out.reserve(m_remoteToWindows.size());
    for (auto it = m_remoteToWindows.cbegin(); it != m_remoteToWindows.cend(); ++it) {
        NameMapping mapping;
        mapping.remoteName = it.key();
        mapping.windowsName = it.value();
        mapping.reason = m_reasonByRemote.value(it.key(), NameMappingReason::None);
        out.append(mapping);
    }
    return out;
}

void NameMapper::clear()
{
    m_remoteToWindows.clear();
    m_windowsToRemote.clear();
    m_reasonByRemote.clear();
}

} // namespace KIODrive

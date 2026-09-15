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

#ifndef KIODRIVE_COMMON_PATH_UTILS_H
#define KIODRIVE_COMMON_PATH_UTILS_H

#include <QString>
#include <QUrl>

namespace KIODrive
{

class PathUtils
{
public:
    /** Normalize separators and collapse . / .. without resolving reparse points. */
    static QString normalizeLexical(const QString &path);

    /** Windows-oriented case-folded key for path routing on NTFS. */
    static QString normalizedPathKey(const QString &path);

    /** True if @p candidate is equal to or under @p ancestor (normalized keys). */
    static bool isSameOrDescendant(const QString &ancestor, const QString &candidate);

    /** Protocol-aware remote URL canonicalization (does not lowercase remote paths). */
    static QUrl canonicalizeRemoteUrl(const QUrl &url);

    /** True if @p candidate is the same URL or a path descendant of @p ancestor. */
    static bool isRemoteSameOrDescendant(const QUrl &ancestor, const QUrl &candidate);

#ifdef Q_OS_WIN
    /** Resolve final path via Win32 APIs when the path exists. */
    static QString resolveFinalPath(const QString &path);
#endif
};

} // namespace KIODrive

#endif // KIODRIVE_COMMON_PATH_UTILS_H

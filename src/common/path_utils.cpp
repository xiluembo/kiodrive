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

#include "path_utils.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace KIODrive
{

QString PathUtils::normalizeLexical(const QString &path)
{
    QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (cleaned.size() > 1 && cleaned.endsWith(QLatin1Char('/'))) {
        cleaned.chop(1);
    }
    return cleaned;
}

QString PathUtils::normalizedPathKey(const QString &path)
{
    return normalizeLexical(path).toLower();
}

bool PathUtils::isSameOrDescendant(const QString &ancestor, const QString &candidate)
{
    const QString a = normalizedPathKey(ancestor);
    const QString c = normalizedPathKey(candidate);
    if (a.isEmpty() || c.isEmpty()) {
        return false;
    }
    if (c == a) {
        return true;
    }
    return c.startsWith(a + QLatin1Char('/'));
}

QUrl PathUtils::canonicalizeRemoteUrl(const QUrl &url)
{
    QUrl result = url;
    result.setScheme(result.scheme().toLower());

    // Windows users often type file://e:/folder (two slashes). QUrl then treats "e:" as host
    // and drops the drive from the path. Normalize to a proper local-file URL.
    if (result.scheme() == QLatin1String("file")) {
        QString local = result.toLocalFile();
        if (local.isEmpty()) {
            const QString host = result.host();
            const QString path = result.path();
            if (host.size() == 1 && host.at(0).isLetter()
                && (path.startsWith(QLatin1String(":/")) || path.startsWith(QLatin1String(":\\")))) {
                local = host + path;
            } else if (host.size() == 2 && host.at(0).isLetter() && host.at(1) == QLatin1Char(':')) {
                local = host + path;
            } else if (!path.isEmpty()) {
                local = path;
            }
        }
        if (!local.isEmpty()) {
            result = QUrl::fromLocalFile(QDir::cleanPath(QDir::fromNativeSeparators(local)));
        }
        result.setFragment(QString());
        return result;
    }

    result.setHost(result.host().toLower());

    const QString scheme = result.scheme();
    if (result.port() != -1) {
        if ((scheme == QLatin1String("sftp") && result.port() == 22)
            || (scheme == QLatin1String("ftp") && result.port() == 21)
            || (scheme == QLatin1String("http") && result.port() == 80)
            || (scheme == QLatin1String("https") && result.port() == 443)
            || (scheme == QLatin1String("webdav") && result.port() == 80)
            || (scheme == QLatin1String("webdavs") && result.port() == 443)
            || (scheme == QLatin1String("dav") && result.port() == 80)
            || (scheme == QLatin1String("davs") && result.port() == 443)) {
            result.setPort(-1);
        }
    }

    // Strip credentials from persisted canonical form.
    result.setUserName(QString());
    result.setPassword(QString());

    QString path = result.path();
    if (path.isEmpty()) {
        path = QStringLiteral("/");
    }
    path = QDir::cleanPath(path);
    if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }
    result.setPath(path);
    result.setFragment(QString());
    return result;
}

bool PathUtils::isRemoteSameOrDescendant(const QUrl &ancestor, const QUrl &candidate)
{
    const QUrl a = canonicalizeRemoteUrl(ancestor);
    const QUrl c = canonicalizeRemoteUrl(candidate);
    if (a.scheme() != c.scheme() || a.host() != c.host() || a.port() != c.port()) {
        return false;
    }
    // Keep remote path case as-is (case-sensitive remotes).
    const QString ap = a.path();
    const QString cp = c.path();
    if (cp == ap) {
        return true;
    }
    const QString prefix = ap.endsWith(QLatin1Char('/')) ? ap : (ap + QLatin1Char('/'));
    return cp.startsWith(prefix);
}

#ifdef Q_OS_WIN
QString PathUtils::resolveFinalPath(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                flags,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return normalizeLexical(path);
    }

    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetFinalPathNameByHandleW(handle, buffer, MAX_PATH * 4, FILE_NAME_NORMALIZED);
    CloseHandle(handle);
    if (length == 0 || length >= MAX_PATH * 4) {
        return normalizeLexical(path);
    }

    QString finalPath = QString::fromWCharArray(buffer, static_cast<int>(length));
    if (finalPath.startsWith(QLatin1String("\\\\?\\"))) {
        finalPath = finalPath.mid(4);
        if (finalPath.startsWith(QLatin1String("UNC\\"))) {
            finalPath = QLatin1String("\\\\") + finalPath.mid(4);
        }
    }
    return normalizeLexical(finalPath);
}
#endif

} // namespace KIODrive

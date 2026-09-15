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

#include "credential_broker.h"

#include "common/logging.h"

#include <QDataStream>
#include <QIODevice>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace KIODrive
{

CredentialBroker::CredentialBroker() = default;

CredentialBroker::~CredentialBroker() = default;

void CredentialBroker::setInMemoryOnly(bool enabled)
{
    m_inMemoryOnly = enabled;
}

bool CredentialBroker::inMemoryOnly() const
{
    return m_inMemoryOnly;
}

QString CredentialBroker::lastError() const
{
    return m_lastError;
}

QString CredentialBroker::targetName(const QUuid &mountId)
{
    return QStringLiteral("KIODrive/mount/") + mountId.toString(QUuid::WithoutBraces);
}

QByteArray CredentialBroker::serialize(const Credential &credential)
{
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_5);
    quint32 version = 1;
    stream << version << credential.username << credential.password << credential.privateKeyPath;
    return blob;
}

bool CredentialBroker::deserialize(const QByteArray &blob, Credential *out)
{
    if (!out || blob.isEmpty()) {
        return false;
    }
    QDataStream stream(blob);
    stream.setVersion(QDataStream::Qt_6_5);
    quint32 version = 0;
    stream >> version;
    if (version != 1 || stream.status() != QDataStream::Ok) {
        return false;
    }
    stream >> out->username >> out->password >> out->privateKeyPath;
    return stream.status() == QDataStream::Ok;
}

bool CredentialBroker::store(const QUuid &mountId, const Credential &credential)
{
    m_lastError.clear();
    if (mountId.isNull()) {
        m_lastError = QStringLiteral("Mount id is null.");
        return false;
    }

#ifndef Q_OS_WIN
    m_memory.insert(mountId, credential);
    return true;
#else
    if (m_inMemoryOnly) {
        m_memory.insert(mountId, credential);
        return true;
    }
    return storeWindows(mountId, credential);
#endif
}

std::optional<Credential> CredentialBroker::retrieve(const QUuid &mountId) const
{
    m_lastError.clear();
    if (mountId.isNull()) {
        m_lastError = QStringLiteral("Mount id is null.");
        return std::nullopt;
    }

#ifndef Q_OS_WIN
    const auto it = m_memory.constFind(mountId);
    if (it == m_memory.cend()) {
        return std::nullopt;
    }
    return *it;
#else
    if (m_inMemoryOnly) {
        const auto it = m_memory.constFind(mountId);
        if (it == m_memory.cend()) {
            return std::nullopt;
        }
        return *it;
    }
    return retrieveWindows(mountId);
#endif
}

bool CredentialBroker::remove(const QUuid &mountId)
{
    m_lastError.clear();
    if (mountId.isNull()) {
        m_lastError = QStringLiteral("Mount id is null.");
        return false;
    }

#ifndef Q_OS_WIN
    return m_memory.remove(mountId) > 0;
#else
    if (m_inMemoryOnly) {
        return m_memory.remove(mountId) > 0;
    }
    m_memory.remove(mountId);
    return removeWindows(mountId);
#endif
}

bool CredentialBroker::contains(const QUuid &mountId) const
{
    return retrieve(mountId).has_value();
}

#ifdef Q_OS_WIN

bool CredentialBroker::storeWindows(const QUuid &mountId, const Credential &credential)
{
    const QString target = targetName(mountId);
    const QByteArray blob = serialize(credential);

    CREDENTIALW cred;
    ZeroMemory(&cred, sizeof(cred));
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = reinterpret_cast<LPWSTR>(const_cast<ushort *>(target.utf16()));
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = reinterpret_cast<LPWSTR>(const_cast<ushort *>(credential.username.utf16()));

    if (!CredWriteW(&cred, 0)) {
        m_lastError = QStringLiteral("CredWriteW failed.");
        qCWarning(KIODRIVE_KIO_LOG) << "CredentialBroker store failed for mount" << mountId;
        return false;
    }
    // Never log username/password/key material.
    qCDebug(KIODRIVE_KIO_LOG) << "CredentialBroker stored credentials for mount" << mountId;
    return true;
}

std::optional<Credential> CredentialBroker::retrieveWindows(const QUuid &mountId) const
{
    const QString target = targetName(mountId);
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &cred)) {
        const DWORD err = GetLastError();
        if (err != ERROR_NOT_FOUND) {
            m_lastError = QStringLiteral("CredReadW failed.");
            qCWarning(KIODRIVE_KIO_LOG) << "CredentialBroker retrieve failed for mount" << mountId;
        }
        return std::nullopt;
    }

    Credential result;
    const QByteArray blob(reinterpret_cast<const char *>(cred->CredentialBlob),
                          static_cast<int>(cred->CredentialBlobSize));
    const bool ok = deserialize(blob, &result);
    CredFree(cred);
    if (!ok) {
        m_lastError = QStringLiteral("Credential blob is corrupt.");
        return std::nullopt;
    }
    return result;
}

bool CredentialBroker::removeWindows(const QUuid &mountId)
{
    const QString target = targetName(mountId);
    if (!CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0)) {
        const DWORD err = GetLastError();
        if (err == ERROR_NOT_FOUND) {
            return false;
        }
        m_lastError = QStringLiteral("CredDeleteW failed.");
        qCWarning(KIODRIVE_KIO_LOG) << "CredentialBroker remove failed for mount" << mountId;
        return false;
    }
    return true;
}

#else

bool CredentialBroker::storeWindows(const QUuid &, const Credential &)
{
    return false;
}

std::optional<Credential> CredentialBroker::retrieveWindows(const QUuid &) const
{
    return std::nullopt;
}

bool CredentialBroker::removeWindows(const QUuid &)
{
    return false;
}

#endif

} // namespace KIODrive

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

#ifndef KIODRIVE_KIO_CREDENTIAL_BROKER_H
#define KIODRIVE_KIO_CREDENTIAL_BROKER_H

#include <QHash>
#include <QString>
#include <QUuid>

#include <optional>

namespace KIODrive
{

struct Credential {
    QString username;
    QString password;
    QString privateKeyPath;
};

/**
 * Stores and retrieves mount credentials.
 * On Windows uses Credential Manager (CredWriteW/CredReadW).
 * Elsewhere (and in tests) uses an in-memory map.
 * Secrets are never logged.
 */
class CredentialBroker
{
public:
    CredentialBroker();
    ~CredentialBroker();

    CredentialBroker(const CredentialBroker &) = delete;
    CredentialBroker &operator=(const CredentialBroker &) = delete;

    /** Force in-memory storage even on Windows (unit tests). */
    void setInMemoryOnly(bool enabled);
    bool inMemoryOnly() const;

    bool store(const QUuid &mountId, const Credential &credential);
    std::optional<Credential> retrieve(const QUuid &mountId) const;
    bool remove(const QUuid &mountId);
    bool contains(const QUuid &mountId) const;

    QString lastError() const;

private:
    static QString targetName(const QUuid &mountId);
    static QByteArray serialize(const Credential &credential);
    static bool deserialize(const QByteArray &blob, Credential *out);

    bool storeWindows(const QUuid &mountId, const Credential &credential);
    std::optional<Credential> retrieveWindows(const QUuid &mountId) const;
    bool removeWindows(const QUuid &mountId);

    bool m_inMemoryOnly = false;
    mutable QHash<QUuid, Credential> m_memory;
    mutable QString m_lastError;
};

} // namespace KIODrive

#endif // KIODRIVE_KIO_CREDENTIAL_BROKER_H

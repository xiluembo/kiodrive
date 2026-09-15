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

#ifndef KIODRIVE_KIO_KIO_BACKEND_H
#define KIODRIVE_KIO_KIO_BACKEND_H

#include "common/types.h"
#include "credential_broker.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVector>

#include <functional>
#include <optional>

class KJob;

namespace KIO
{
class Job;
class FileJob;
class UDSEntry;
}

namespace KIODrive
{

struct RemoteEntry {
    QString name;
    QString displayName;
    ItemKind kind = ItemKind::File;
    qint64 size = -1;
    qint64 mtime = 0;
    QString mime;
    QString remoteInode;
    QString revisionToken;
};

struct BackendResult {
    bool ok = false;
    int errorCode = 0;
    QString errorString;
    QVector<RemoteEntry> entries;
    RemoteEntry stat;
    QByteArray data;

    static BackendResult success()
    {
        BackendResult r;
        r.ok = true;
        return r;
    }

    static BackendResult failure(int code, const QString &message)
    {
        BackendResult r;
        r.ok = false;
        r.errorCode = code;
        r.errorString = message;
        return r;
    }
};

/**
 * Abstract KIO-facing backend. Concrete implementations talk to KF6 jobs or
 * in-memory mocks. Blocking *Sync helpers run a local QEventLoop for tests.
 */
class KioBackend : public QObject
{
    Q_OBJECT

public:
    explicit KioBackend(QObject *parent = nullptr);
    ~KioBackend() override;

    void setCredentialBroker(CredentialBroker *broker);
    CredentialBroker *credentialBroker() const;

    void setMountId(const QUuid &mountId);
    QUuid mountId() const;

    // --- Async API ---
    virtual void listDir(const QUrl &url);
    virtual void stat(const QUrl &url);
    virtual void get(const QUrl &url, qint64 offset = 0, qint64 length = -1);
    virtual void open(const QUrl &url);
    virtual void seek(qint64 offset);
    virtual void read(qint64 length);
    virtual void closeFile();
    virtual void rename(const QUrl &src, const QUrl &dest, bool overwrite = false);
    virtual void del(const QUrl &url, bool isFile = true);
    virtual void mkdir(const QUrl &url);
    virtual void put(const QUrl &url, const QByteArray &data, bool overwrite = true);

    // --- Blocking helpers (tests / simple callers) ---
    BackendResult listDirSync(const QUrl &url, int timeoutMs = 30000);
    BackendResult statSync(const QUrl &url, int timeoutMs = 30000);
    BackendResult getSync(const QUrl &url, qint64 offset = 0, qint64 length = -1, int timeoutMs = 60000);
    BackendResult renameSync(const QUrl &src, const QUrl &dest, bool overwrite = false, int timeoutMs = 30000);
    BackendResult delSync(const QUrl &url, bool isFile = true, int timeoutMs = 30000);
    BackendResult mkdirSync(const QUrl &url, int timeoutMs = 30000);
    BackendResult putSync(const QUrl &url, const QByteArray &data, bool overwrite = true, int timeoutMs = 60000);

    bool hasOpenFile() const;

    /** Kill in-flight listDir/get transfer jobs (CFAPI CANCEL_FETCH_*). */
    void cancelTransfers();

Q_SIGNALS:
    void listDirFinished(const BackendResult &result);
    void statFinished(const BackendResult &result);
    void getFinished(const BackendResult &result);
    void openFinished(const BackendResult &result);
    void readFinished(const BackendResult &result);
    void seekFinished(const BackendResult &result);
    void closeFinished(const BackendResult &result);
    void renameFinished(const BackendResult &result);
    void delFinished(const BackendResult &result);
    void mkdirFinished(const BackendResult &result);
    void putFinished(const BackendResult &result);

protected:
    void applyCredentials(QUrl *url) const;
    static RemoteEntry fromUdsEntry(const KIO::UDSEntry &entry);
    BackendResult waitForSignal(void (KioBackend::*signal)(const BackendResult &),
                                const std::function<void()> &start,
                                int timeoutMs);

    CredentialBroker *m_broker = nullptr;
    QUuid m_mountId;
    KIO::FileJob *m_fileJob = nullptr;
    QPointer<KJob> m_activeListJob;
    QPointer<KJob> m_activeGetJob;
    QByteArray m_getBuffer;
    qint64 m_getOffset = 0;
    qint64 m_getLength = -1;
};

} // namespace KIODrive

#endif // KIODRIVE_KIO_KIO_BACKEND_H

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

#include "kio_backend.h"

#include "common/logging.h"

#include <KIO/DeleteJob>
#include <KIO/FileJob>
#include <KIO/ListJob>
#include <KIO/MkdirJob>
#include <KIO/SimpleJob>
#include <KIO/StatJob>
#include <KIO/TransferJob>
#include <KIO/UDSEntry>

#include <QEventLoop>
#include <QIODevice>
#include <QTimer>

namespace KIODrive
{

namespace
{

BackendResult fromJobError(KJob *job)
{
    if (!job) {
        return BackendResult::failure(-1, QStringLiteral("Null job"));
    }
    if (job->error() == 0) {
        return BackendResult::success();
    }
    return BackendResult::failure(job->error(), job->errorString());
}

KIO::JobFlags jobFlags(bool overwrite)
{
    KIO::JobFlags flags = KIO::HideProgressInfo;
    if (overwrite) {
        flags |= KIO::Overwrite;
    }
    return flags;
}

} // namespace

KioBackend::KioBackend(QObject *parent)
    : QObject(parent)
{
}

KioBackend::~KioBackend()
{
    if (m_fileJob) {
        m_fileJob->close();
        m_fileJob = nullptr;
    }
}

void KioBackend::setCredentialBroker(CredentialBroker *broker)
{
    m_broker = broker;
}

CredentialBroker *KioBackend::credentialBroker() const
{
    return m_broker;
}

void KioBackend::setMountId(const QUuid &mountId)
{
    m_mountId = mountId;
}

QUuid KioBackend::mountId() const
{
    return m_mountId;
}

bool KioBackend::hasOpenFile() const
{
    return m_fileJob != nullptr;
}

void KioBackend::cancelTransfers()
{
    if (m_activeListJob) {
        qCInfo(KIODRIVE_KIO_LOG) << "Cancelling active listDir job";
        m_activeListJob->kill(KJob::EmitResult);
        m_activeListJob.clear();
    }
    if (m_activeGetJob) {
        qCInfo(KIODRIVE_KIO_LOG) << "Cancelling active get job";
        m_activeGetJob->kill(KJob::EmitResult);
        m_activeGetJob.clear();
    }
    m_getBuffer.clear();
}

void KioBackend::applyCredentials(QUrl *url) const
{
    if (!url || !m_broker || m_mountId.isNull()) {
        return;
    }
    const auto cred = m_broker->retrieve(m_mountId);
    if (!cred) {
        return;
    }
    if (!cred->username.isEmpty()) {
        url->setUserName(cred->username);
    }
    if (!cred->password.isEmpty()) {
        url->setPassword(cred->password);
    }
}

RemoteEntry KioBackend::fromUdsEntry(const KIO::UDSEntry &entry)
{
    RemoteEntry remote;
    remote.name = entry.stringValue(KIO::UDSEntry::UDS_NAME);
    remote.displayName = entry.stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME);
    if (remote.displayName.isEmpty()) {
        remote.displayName = remote.name;
    }
    remote.size = entry.numberValue(KIO::UDSEntry::UDS_SIZE, -1);
    remote.mtime = entry.numberValue(KIO::UDSEntry::UDS_MODIFICATION_TIME, 0);
    remote.mime = entry.stringValue(KIO::UDSEntry::UDS_MIME_TYPE);
    const qint64 inode = entry.numberValue(KIO::UDSEntry::UDS_INODE, -1);
    if (inode >= 0) {
        remote.remoteInode = QString::number(inode);
    }

    if (entry.isLink()) {
        remote.kind = ItemKind::Symlink;
    } else if (entry.isDir()) {
        remote.kind = ItemKind::Directory;
    } else {
        remote.kind = ItemKind::File;
    }
    return remote;
}

void KioBackend::listDir(const QUrl &url)
{
    QUrl working = url;
    applyCredentials(&working);

    auto *job = KIO::listDir(working, KIO::HideProgressInfo);
    m_activeListJob = job;
    auto *entries = new QVector<RemoteEntry>();

    connect(job, &KIO::ListJob::entries, this, [entries](KIO::Job *, const KIO::UDSEntryList &list) {
        for (const KIO::UDSEntry &entry : list) {
            const RemoteEntry remote = fromUdsEntry(entry);
            if (remote.name == QLatin1String(".") || remote.name == QLatin1String("..")) {
                continue;
            }
            entries->append(remote);
        }
    });

    connect(job, &KJob::result, this, [this, job, entries]() {
        if (m_activeListJob == job) {
            m_activeListJob.clear();
        }
        BackendResult result = fromJobError(job);
        if (result.ok) {
            result.entries = *entries;
        }
        delete entries;
        Q_EMIT listDirFinished(result);
    });
}

void KioBackend::stat(const QUrl &url)
{
    QUrl working = url;
    applyCredentials(&working);

    auto *job = KIO::stat(working, KIO::HideProgressInfo);
    connect(job, &KJob::result, this, [this, job]() {
        BackendResult result = fromJobError(job);
        if (result.ok) {
            auto *statJob = qobject_cast<KIO::StatJob *>(job);
            if (statJob) {
                result.stat = fromUdsEntry(statJob->statResult());
            }
        }
        Q_EMIT statFinished(result);
    });
}

void KioBackend::get(const QUrl &url, qint64 offset, qint64 length)
{
    QUrl working = url;
    applyCredentials(&working);

    m_getBuffer.clear();
    m_getOffset = offset;
    m_getLength = length;

    auto *job = KIO::get(working, KIO::NoReload, KIO::HideProgressInfo);
    m_activeGetJob = job;
    if (offset > 0) {
        job->addMetaData(QStringLiteral("range-start"), QString::number(offset));
        if (length >= 0) {
            job->addMetaData(QStringLiteral("range-end"), QString::number(offset + length - 1));
        }
        job->addMetaData(QStringLiteral("resume"), QString::number(offset));
    }

    connect(job, &KIO::TransferJob::data, this, [this, length](KIO::Job *, const QByteArray &data) {
        if (length >= 0 && m_getBuffer.size() >= length) {
            return;
        }
        QByteArray chunk = data;
        if (length >= 0) {
            const qint64 remaining = length - m_getBuffer.size();
            if (chunk.size() > remaining) {
                chunk = chunk.left(static_cast<int>(remaining));
            }
        }
        m_getBuffer.append(chunk);
    });

    connect(job, &KJob::result, this, [this, job]() {
        if (m_activeGetJob == job) {
            m_activeGetJob.clear();
        }
        BackendResult result = fromJobError(job);
        if (result.ok) {
            result.data = m_getBuffer;
        }
        m_getBuffer.clear();
        Q_EMIT getFinished(result);
    });
}

void KioBackend::open(const QUrl &url)
{
    if (m_fileJob) {
        Q_EMIT openFinished(BackendResult::failure(-1, QStringLiteral("A file is already open.")));
        return;
    }

    QUrl working = url;
    applyCredentials(&working);

    m_fileJob = KIO::open(working, QIODevice::ReadOnly);
    connect(m_fileJob, &KIO::FileJob::open, this, [this](KIO::Job *) {
        Q_EMIT openFinished(BackendResult::success());
    });
    connect(m_fileJob, &KJob::result, this, [this](KJob *job) {
        if (job->error() != 0 && m_fileJob == job) {
            m_fileJob = nullptr;
            Q_EMIT openFinished(fromJobError(job));
        }
    });
}

void KioBackend::seek(qint64 offset)
{
    if (!m_fileJob) {
        Q_EMIT seekFinished(BackendResult::failure(-1, QStringLiteral("No open file.")));
        return;
    }

    connect(
        m_fileJob,
        &KIO::FileJob::position,
        this,
        [this](KIO::Job *, KIO::filesize_t) {
            Q_EMIT seekFinished(BackendResult::success());
        },
        Qt::SingleShotConnection);

    connect(
        m_fileJob,
        &KJob::result,
        this,
        [this](KJob *job) {
            if (job->error() != 0) {
                Q_EMIT seekFinished(fromJobError(job));
            }
        },
        Qt::SingleShotConnection);

    m_fileJob->seek(static_cast<KIO::filesize_t>(offset));
}

void KioBackend::read(qint64 length)
{
    if (!m_fileJob) {
        Q_EMIT readFinished(BackendResult::failure(-1, QStringLiteral("No open file.")));
        return;
    }

    connect(
        m_fileJob,
        &KIO::FileJob::data,
        this,
        [this](KIO::Job *, const QByteArray &data) {
            BackendResult result = BackendResult::success();
            result.data = data;
            Q_EMIT readFinished(result);
        },
        Qt::SingleShotConnection);

    connect(
        m_fileJob,
        &KJob::result,
        this,
        [this](KJob *job) {
            if (job->error() != 0) {
                Q_EMIT readFinished(fromJobError(job));
            }
        },
        Qt::SingleShotConnection);

    m_fileJob->read(static_cast<KIO::filesize_t>(length));
}

void KioBackend::closeFile()
{
    if (!m_fileJob) {
        Q_EMIT closeFinished(BackendResult::success());
        return;
    }

    auto *job = m_fileJob;
    m_fileJob = nullptr;
    connect(job, &KIO::FileJob::fileClosed, this, [this](KIO::Job *) {
        Q_EMIT closeFinished(BackendResult::success());
    });
    connect(job, &KJob::result, this, [this](KJob *finished) {
        if (finished->error() != 0) {
            Q_EMIT closeFinished(fromJobError(finished));
        }
    });
    job->close();
}

void KioBackend::rename(const QUrl &src, const QUrl &dest, bool overwrite)
{
    QUrl s = src;
    QUrl d = dest;
    applyCredentials(&s);
    applyCredentials(&d);

    auto *job = KIO::rename(s, d, jobFlags(overwrite));
    connect(job, &KJob::result, this, [this, job]() {
        Q_EMIT renameFinished(fromJobError(job));
    });
}

void KioBackend::del(const QUrl &url, bool isFile)
{
    Q_UNUSED(isFile);
    QUrl working = url;
    applyCredentials(&working);

    auto *job = KIO::del(working, KIO::HideProgressInfo);
    connect(job, &KJob::result, this, [this, job]() {
        Q_EMIT delFinished(fromJobError(job));
    });
}

void KioBackend::mkdir(const QUrl &url)
{
    QUrl working = url;
    applyCredentials(&working);

    auto *job = KIO::mkdir(working);
    connect(job, &KJob::result, this, [this, job]() {
        Q_EMIT mkdirFinished(fromJobError(job));
    });
}

void KioBackend::put(const QUrl &url, const QByteArray &data, bool overwrite)
{
    QUrl working = url;
    applyCredentials(&working);

    auto *job = KIO::put(working, -1, jobFlags(overwrite));
    auto *remaining = new QByteArray(data);

    connect(job, &KIO::TransferJob::dataReq, this, [remaining](KIO::Job *, QByteArray &data) {
        data = *remaining;
        remaining->clear();
    });

    connect(job, &KJob::result, this, [this, job, remaining]() {
        delete remaining;
        Q_EMIT putFinished(fromJobError(job));
    });
}

BackendResult KioBackend::waitForSignal(void (KioBackend::*signal)(const BackendResult &),
                                        const std::function<void()> &start,
                                        int timeoutMs)
{
    BackendResult result = BackendResult::failure(-1, QStringLiteral("Timed out"));
    QEventLoop loop;
    QMetaObject::Connection conn = connect(this, signal, this, [&](const BackendResult &r) {
        result = r;
        loop.quit();
    });

    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (timeoutMs > 0) {
        timer.start(timeoutMs);
    }

    start();
    loop.exec();
    disconnect(conn);
    return result;
}

BackendResult KioBackend::listDirSync(const QUrl &url, int timeoutMs)
{
    return waitForSignal(&KioBackend::listDirFinished, [this, url]() { listDir(url); }, timeoutMs);
}

BackendResult KioBackend::statSync(const QUrl &url, int timeoutMs)
{
    return waitForSignal(&KioBackend::statFinished, [this, url]() { stat(url); }, timeoutMs);
}

BackendResult KioBackend::getSync(const QUrl &url, qint64 offset, qint64 length, int timeoutMs)
{
    return waitForSignal(
        &KioBackend::getFinished, [this, url, offset, length]() { get(url, offset, length); }, timeoutMs);
}

BackendResult KioBackend::renameSync(const QUrl &src, const QUrl &dest, bool overwrite, int timeoutMs)
{
    return waitForSignal(
        &KioBackend::renameFinished, [this, src, dest, overwrite]() { rename(src, dest, overwrite); }, timeoutMs);
}

BackendResult KioBackend::delSync(const QUrl &url, bool isFile, int timeoutMs)
{
    return waitForSignal(&KioBackend::delFinished, [this, url, isFile]() { del(url, isFile); }, timeoutMs);
}

BackendResult KioBackend::mkdirSync(const QUrl &url, int timeoutMs)
{
    return waitForSignal(&KioBackend::mkdirFinished, [this, url]() { mkdir(url); }, timeoutMs);
}

BackendResult KioBackend::putSync(const QUrl &url, const QByteArray &data, bool overwrite, int timeoutMs)
{
    return waitForSignal(
        &KioBackend::putFinished, [this, url, data, overwrite]() { put(url, data, overwrite); }, timeoutMs);
}

} // namespace KIODrive

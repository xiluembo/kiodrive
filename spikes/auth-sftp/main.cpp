/*
 * SPDX-FileCopyrightText: 2026 KIODrive authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of KIODrive.
 *
 * Spike: SFTP auth on Windows Craft without kpasswdserver.
 * Usage: kiodrive_spike_auth_sftp sftp://user@host/path/
 * Credentials via env KIODRIVE_SPIKE_USER / KIODRIVE_SPIKE_PASSWORD (never logged).
 */

#include <KIO/StatJob>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        return 2;
    }

    QUrl url(QString::fromLocal8Bit(argv[1]));
    const QByteArray user = qgetenv("KIODRIVE_SPIKE_USER");
    const QByteArray password = qgetenv("KIODRIVE_SPIKE_PASSWORD");
    if (!user.isEmpty()) {
        url.setUserName(QString::fromUtf8(user));
    }
    if (!password.isEmpty()) {
        url.setPassword(QString::fromUtf8(password));
    }

    auto *job = KIO::stat(url, KIO::StatJob::SourceSide, KIO::StatDefaultDetails, KIO::HideProgressInfo);
    job->addMetaData(QStringLiteral("no-auth-prompt"), QStringLiteral("true"));

    QEventLoop loop;
    int exitCode = 1;
    QObject::connect(job, &KJob::result, &app, [&](KJob *kjob) {
        exitCode = kjob->error() == 0 ? 0 : 1;
        // Do not print passwords or full URL with credentials.
        qInfo("stat finished error=%d", kjob->error());
        loop.quit();
    });
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    loop.exec();
    return exitCode;
}

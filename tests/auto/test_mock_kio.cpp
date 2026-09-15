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

#include "kio/mock_kio_backend.h"

#include <QTest>

using namespace KIODrive;

class TestMockKio : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listAndGet()
    {
        MockKioBackend backend;
        const QUrl root(QStringLiteral("mock://host/dir/"));
        const QUrl file(QStringLiteral("mock://host/dir/hello.txt"));
        backend.seedDirectory(root);
        backend.seedFile(file, QByteArrayLiteral("hello world"));

        const BackendResult listed = backend.listDirSync(root);
        QVERIFY(listed.ok);
        QVERIFY(listed.entries.size() >= 1);

        const BackendResult data = backend.getSync(file);
        QVERIFY(data.ok);
        QCOMPARE(data.data, QByteArrayLiteral("hello world"));
    }

    void renameWithoutHydration()
    {
        MockKioBackend backend;
        const QUrl src(QStringLiteral("mock://host/a.txt"));
        const QUrl dest(QStringLiteral("mock://host/b.txt"));
        backend.seedFile(src, QByteArrayLiteral("x"));
        const BackendResult result = backend.renameSync(src, dest);
        QVERIFY(result.ok);
        QVERIFY(backend.exists(dest));
    }
};

QTEST_GUILESS_MAIN(TestMockKio)
#include "test_mock_kio.moc"

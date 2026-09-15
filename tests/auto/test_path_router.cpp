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

#include "mounts/path_router.h"

#include <QTest>

using namespace KIODrive;

class TestPathRouter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void rejectsAncestor()
    {
        PathRouter router;
        const QUuid a = QUuid::createUuid();
        const QUuid b = QUuid::createUuid();
        QVERIFY(router.registerMount(a, QStringLiteral("server")));
        QVERIFY(!router.registerMount(b, QStringLiteral("server/documents")));
    }

    void findsOwner()
    {
        PathRouter router;
        const QUuid a = QUuid::createUuid();
        QVERIFY(router.registerMount(a, QStringLiteral("PersonalServer")));
        const auto owner = router.findOwner(QStringLiteral("PersonalServer/docs/a.txt"));
        QVERIFY(owner.has_value());
        QCOMPARE(*owner, a);
    }

    void rejectsDuplicate()
    {
        PathRouter router;
        const QUuid a = QUuid::createUuid();
        const QUuid b = QUuid::createUuid();
        QVERIFY(router.registerMount(a, QStringLiteral("ftp")));
        QVERIFY(!router.registerMount(b, QStringLiteral("ftp")));
    }
};

QTEST_GUILESS_MAIN(TestPathRouter)
#include "test_path_router.moc"

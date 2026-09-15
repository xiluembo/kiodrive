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

#include "common/file_identity.h"

#include <QTest>

using namespace KIODrive;

class TestFileIdentity : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTrip()
    {
        FileIdentityV1 identity;
        identity.mountId = QUuid::createUuid();
        identity.itemId = QUuid::createUuid();
        identity.revisionSeq = 42;
        const QByteArray blob = identity.serialize();
        QVERIFY(blob.size() >= 42);
        FileIdentityV1 decoded;
        QVERIFY(FileIdentityV1::deserialize(blob, &decoded));
        QCOMPARE(decoded.mountId, identity.mountId);
        QCOMPARE(decoded.itemId, identity.itemId);
        QCOMPARE(decoded.revisionSeq, identity.revisionSeq);
    }
};

QTEST_GUILESS_MAIN(TestFileIdentity)
#include "test_file_identity.moc"

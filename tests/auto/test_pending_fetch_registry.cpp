/*
 * SPDX-FileCopyrightText: 2026 KIODrive authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "cloudfiles/pending_fetch_registry.h"

#include <QTest>

using namespace KIODrive;

class TestPendingFetchRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tracksConcurrentRangesForTheSamePath();
    void separatesDataAndPlaceholderRequests();
};

void TestPendingFetchRegistry::tracksConcurrentRangesForTheSamePath()
{
    PendingFetchRegistry registry;
    const QUuid op101 = registry.begin(PendingFetchKind::Data, QStringLiteral("mount/file.bin"), 101);
    registry.begin(PendingFetchKind::Data, QStringLiteral("mount/file.bin"), 102);

    QUuid cancelledOp;
    QVERIFY(registry.markCancelled(PendingFetchKind::Data, 101, &cancelledOp));
    QCOMPARE(cancelledOp, op101);
    QVERIFY(registry.isCancelled(PendingFetchKind::Data, 101));
    QVERIFY(!registry.isCancelled(PendingFetchKind::Data, 102));

    PendingFetch fetch;
    QVERIFY(registry.takeIfActive(PendingFetchKind::Data, 102, &fetch));
    QCOMPARE(fetch.relativePath, QStringLiteral("mount/file.bin"));
    QCOMPARE(fetch.requestKey, quint64(102));
    QVERIFY(!registry.takeIfActive(PendingFetchKind::Data, 101, nullptr));
}

void TestPendingFetchRegistry::separatesDataAndPlaceholderRequests()
{
    PendingFetchRegistry registry;
    registry.begin(PendingFetchKind::Data, QStringLiteral("mount"), 7);
    registry.begin(PendingFetchKind::Placeholders, QStringLiteral("mount"), 7);

    QVERIFY(registry.markCancelled(PendingFetchKind::Placeholders, 7));
    QVERIFY(!registry.isCancelled(PendingFetchKind::Data, 7));
    QVERIFY(registry.isCancelled(PendingFetchKind::Placeholders, 7));
}

QTEST_MAIN(TestPendingFetchRegistry)
#include "test_pending_fetch_registry.moc"

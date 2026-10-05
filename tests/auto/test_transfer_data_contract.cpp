/*
 * SPDX-FileCopyrightText: 2026 KIODrive authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "cloudfiles/transfer_data_contract.h"

#include <QTest>

using namespace KIODrive;

class TestTransferDataContract : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void successRequiresExactLength();
    void failureReportsRequestedLength();
    void cancelReportsRequestedLength();
    void shortReadFailsWithSizesInDiagnostic();
};

void TestTransferDataContract::successRequiresExactLength()
{
    const QByteArray data(16, 'a');
    const auto completion = planTransferDataCompletion(false, true, 100, 16, data);
    QCOMPARE(completion.status, TransferDataCompletion::Status::Success);
    QCOMPARE(completion.offset, quint64(100));
    QCOMPARE(completion.length, quint64(16));
    QCOMPARE(completion.buffer, data);
}

void TestTransferDataContract::failureReportsRequestedLength()
{
    const auto completion =
        planTransferDataCompletion(false, false, 40, 64, {}, QStringLiteral("backend error"));
    QCOMPARE(completion.status, TransferDataCompletion::Status::Failure);
    QCOMPARE(completion.offset, quint64(40));
    QCOMPARE(completion.length, quint64(64));
    QVERIFY(completion.buffer.isEmpty());
    QCOMPARE(completion.diagnostic, QStringLiteral("backend error"));
}

void TestTransferDataContract::cancelReportsRequestedLength()
{
    const QByteArray data(8, 'x');
    const auto completion = planTransferDataCompletion(true, true, 0, 4096, data);
    QCOMPARE(completion.status, TransferDataCompletion::Status::Cancelled);
    QCOMPARE(completion.length, quint64(4096));
    QVERIFY(completion.buffer.isEmpty());
}

void TestTransferDataContract::shortReadFailsWithSizesInDiagnostic()
{
    const QByteArray data(4, 'b');
    const auto completion = planTransferDataCompletion(false, true, 0, 16, data);
    QCOMPARE(completion.status, TransferDataCompletion::Status::Failure);
    QCOMPARE(completion.length, quint64(16));
    QVERIFY(completion.buffer.isEmpty());
    QVERIFY(completion.diagnostic.contains(QStringLiteral("expected 16")));
    QVERIFY(completion.diagnostic.contains(QStringLiteral("got 4")));
}

QTEST_MAIN(TestTransferDataContract)
#include "test_transfer_data_contract.moc"

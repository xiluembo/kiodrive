/*
 * SPDX-FileCopyrightText: 2026 KIODrive authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "cloudfiles/hydration_manager.h"
#include "kio/mock_kio_backend.h"
#include "metadata/metadata_store.h"
#include "mounts/mount_registry.h"

#include <QTemporaryDir>
#include <QTest>

using namespace KIODrive;

class TestHydrationManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void shortReadDoesNotMarkInSync();
    void exactReadMarksInSync();
    void cancelIgnoresUnrelatedOperationId();
};

namespace
{

struct Fixture {
    QTemporaryDir dir;
    MetadataStore store;
    MountRegistry mounts;
    MockKioBackend backend;
    HydrationManager hydration;
    MountInfo mount;
    ItemRecord item;

    Fixture()
        : store(dir.filePath(QStringLiteral("meta.sqlite")))
        , mounts(&store, dir.path())
        , hydration(&store, &mounts, &backend)
    {
        Q_ASSERT(dir.isValid());
        Q_ASSERT(store.open());

        mount.id = QUuid::createUuid();
        mount.name = QStringLiteral("Remote");
        mount.localRelativePath = QStringLiteral("Remote");
        mount.remoteUrl = QUrl(QStringLiteral("sftp://example.test/home"));
        mount.protocol = QStringLiteral("sftp");
        mount.state = MountState::Online;
        const auto created = mounts.createMount(mount);
        Q_ASSERT(created.has_value());
        mount = *created;

        item.id = QUuid::createUuid();
        item.mountId = mount.id;
        item.remotePath = QStringLiteral("/home/file.bin");
        item.remoteName = QStringLiteral("file.bin");
        item.windowsName = QStringLiteral("file.bin");
        item.kind = ItemKind::File;
        item.size = 4;
        item.syncState = ItemSyncState::OnlineOnly;
        Q_ASSERT(store.upsertItem(item));
    }

    HydrationRequest makeRequest(quint64 offset, quint64 length) const
    {
        HydrationRequest request;
        request.operationId = QUuid::createUuid();
        request.identity.mountId = mount.id;
        request.identity.itemId = item.id;
        request.requiredOffset = offset;
        request.requiredLength = length;
        request.absolutePath = dir.filePath(QStringLiteral("Remote/file.bin"));
        return request;
    }
};

} // namespace

void TestHydrationManager::shortReadDoesNotMarkInSync()
{
    Fixture fx;
    fx.backend.seedFile(QUrl(QStringLiteral("sftp://example.test/home/file.bin")), QByteArray("abcd"));

    bool ok = true;
    QByteArray data;
    QString error;
    fx.hydration.hydrate(fx.makeRequest(0, 16), {}, [&](bool succeeded, const QByteArray &bytes, const QString &err) {
        ok = succeeded;
        data = bytes;
        error = err;
    });

    QVERIFY(!ok);
    QVERIFY(data.isEmpty());
    QVERIFY(error.contains(QStringLiteral("expected 16")));
    QVERIFY(error.contains(QStringLiteral("got 4")));
    const auto item = fx.store.itemById(fx.item.id);
    QVERIFY(item.has_value());
    QCOMPARE(item->syncState, ItemSyncState::Hydrating);
}

void TestHydrationManager::exactReadMarksInSync()
{
    Fixture fx;
    fx.backend.seedFile(QUrl(QStringLiteral("sftp://example.test/home/file.bin")), QByteArray("abcd"));

    bool ok = false;
    QByteArray data;
    fx.hydration.hydrate(fx.makeRequest(0, 4), {}, [&](bool succeeded, const QByteArray &bytes, const QString &) {
        ok = succeeded;
        data = bytes;
    });

    QVERIFY(ok);
    QCOMPARE(data, QByteArray("abcd"));
    const auto item = fx.store.itemById(fx.item.id);
    QVERIFY(item.has_value());
    QCOMPARE(item->syncState, ItemSyncState::InSync);
}

void TestHydrationManager::cancelIgnoresUnrelatedOperationId()
{
    Fixture fx;
    fx.backend.seedFile(QUrl(QStringLiteral("sftp://example.test/home/file.bin")), QByteArray("abcd"));
    fx.backend.setLatencyMs(50);

    HydrationRequest request = fx.makeRequest(0, 4);
    bool ok = false;
    QByteArray data;

    // Unrelated cancel must not abort the active hydration.
    QMetaObject::invokeMethod(
        &fx.hydration,
        [&fx]() { fx.hydration.cancel(QUuid::createUuid()); },
        Qt::QueuedConnection);

    fx.hydration.hydrate(request, {}, [&](bool succeeded, const QByteArray &bytes, const QString &) {
        ok = succeeded;
        data = bytes;
    });

    QVERIFY(ok);
    QCOMPARE(data, QByteArray("abcd"));
}

QTEST_GUILESS_MAIN(TestHydrationManager)
#include "test_hydration_manager.moc"

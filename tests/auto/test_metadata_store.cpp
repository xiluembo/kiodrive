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

#include "metadata/metadata_store.h"
#include "mounts/mount_registry.h"

#include <QTemporaryDir>
#include <QTest>

using namespace KIODrive;

class TestMetadataStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void createMountTransactional()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MetadataStore store(dir.filePath(QStringLiteral("meta.sqlite")));
        QVERIFY(store.open());

        MountRegistry registry(&store, dir.path());
        MountInfo mount;
        mount.id = QUuid::createUuid();
        mount.name = QStringLiteral("Test");
        mount.localRelativePath = QStringLiteral("TestMount");
        mount.remoteUrl = QUrl(QStringLiteral("sftp://example.test/home/"));
        mount.protocol = QStringLiteral("sftp");
        mount.state = MountState::Disabled;

        const auto created = registry.createMount(mount);
        QVERIFY(created.has_value());
        QCOMPARE(registry.listMounts().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestMetadataStore)
#include "test_metadata_store.moc"

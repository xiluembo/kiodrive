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

#include "common/logging.h"
#include "sync/sync_engine_core.h"

#include <QCoreApplication>
#include <QStandardPaths>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("KIODrive"));
    QCoreApplication::setApplicationName(QStringLiteral("KIODrive.SyncEngine"));
    QStandardPaths::setTestModeEnabled(false);

    // Ensure Craft/KIO plugins are discoverable when running from build tree.
    const QByteArray craftRoot = qgetenv("CRAFT_ROOT");
    if (!craftRoot.isEmpty()) {
        QCoreApplication::addLibraryPath(QString::fromLocal8Bit(craftRoot) + QStringLiteral("/plugins"));
        QCoreApplication::addLibraryPath(QString::fromLocal8Bit(craftRoot) + QStringLiteral("/bin"));
    }

    KIODrive::SyncEngineCore engine;
    if (!engine.start()) {
        qCCritical(KIODRIVE_LOG) << "Failed to start sync engine";
        return 1;
    }

    return app.exec();
}

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

#include "main_window.h"
#include "tray_controller.h"

#include "common/logging.h"
#include "sync/sync_engine_core.h"

#include <KLocalizedString>

#include <QApplication>
#include <QMessageBox>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("kiodrive");
    QCoreApplication::setOrganizationName(QStringLiteral("KIODrive"));
    QCoreApplication::setApplicationName(QStringLiteral("KIODrive.Gui"));

    const QByteArray craftRoot = qgetenv("CRAFT_ROOT");
    if (!craftRoot.isEmpty()) {
        QCoreApplication::addLibraryPath(QString::fromLocal8Bit(craftRoot) + QStringLiteral("/plugins"));
    }

    // During development the GUI embeds the sync engine (CFAPI connection).
    // Do not also start KIODrive.SyncEngine.exe — only one process may CfConnectSyncRoot.
    KIODrive::SyncEngineCore engine;
    if (!engine.start()) {
        QMessageBox::critical(nullptr,
                              QStringLiteral("KIODrive"),
                              QStringLiteral(
                                  "Could not start the Cloud Files provider.\n\n"
                                  "Close any other KIODrive.SyncEngine / KIODrive.Gui instance, "
                                  "then try again.\n\n"
                                  "Explorer needs an active CfConnectSyncRoot connection to open "
                                  "%USERPROFILE%\\KIO."));
        qCCritical(KIODRIVE_LOG) << "GUI could not start embedded sync engine";
        return 1;
    }

    KIODrive::MainWindow window(&engine);
    KIODrive::TrayController tray(&window, &engine);
    Q_UNUSED(tray);
    window.show();
    return app.exec();
}

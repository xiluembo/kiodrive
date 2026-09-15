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

#ifndef KIODRIVE_GUI_TRAY_CONTROLLER_H
#define KIODRIVE_GUI_TRAY_CONTROLLER_H

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;

namespace KIODrive
{

class MainWindow;
class SyncEngineCore;

class TrayController : public QObject
{
    Q_OBJECT

public:
    TrayController(MainWindow *window, SyncEngineCore *engine, QObject *parent = nullptr);

private:
    void updateTooltip();

    MainWindow *m_window = nullptr;
    SyncEngineCore *m_engine = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
};

} // namespace KIODrive

#endif // KIODRIVE_GUI_TRAY_CONTROLLER_H

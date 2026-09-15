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

#include "tray_controller.h"
#include "main_window.h"

#include "mounts/mount_registry.h"
#include "sync/sync_engine_core.h"

#include <KLocalizedString>

#include <QApplication>
#include <QMenu>

namespace KIODrive
{

TrayController::TrayController(MainWindow *window, SyncEngineCore *engine, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_engine(engine)
{
    m_tray = new QSystemTrayIcon(this);
    m_tray->setToolTip(i18n("KIODrive"));
    m_menu = new QMenu();
    m_menu->addAction(i18n("Open KIODrive"), m_window, &QWidget::show);
    m_menu->addAction(i18n("Quit"), qApp, &QApplication::quit);
    m_tray->setContextMenu(m_menu);
    m_tray->show();
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            m_window->show();
            m_window->raise();
        }
    });
    updateTooltip();
}

void TrayController::updateTooltip()
{
    int online = 0;
    int offline = 0;
    if (m_engine && m_engine->mountRegistry()) {
        for (const MountInfo &mount : m_engine->mountRegistry()->listMounts()) {
            if (mount.state == MountState::Online) {
                ++online;
            } else if (mount.state == MountState::Offline || mount.state == MountState::AuthenticationRequired) {
                ++offline;
            }
        }
    }
    m_tray->setToolTip(i18n("KIODrive — %1 online, %2 need attention", online, offline));
}

} // namespace KIODrive

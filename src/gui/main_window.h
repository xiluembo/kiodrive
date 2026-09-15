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

#ifndef KIODRIVE_GUI_MAIN_WINDOW_H
#define KIODRIVE_GUI_MAIN_WINDOW_H

#include <QMainWindow>

class QTableWidget;

namespace KIODrive
{

class SyncEngineCore;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(SyncEngineCore *engine, QWidget *parent = nullptr);

public Q_SLOTS:
    void refreshMountTable();
    void addMount();
    void removeSelectedMount();
    void openSelectedInExplorer();
    void reconnectSelected();

private:
    SyncEngineCore *m_engine = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace KIODrive

#endif // KIODRIVE_GUI_MAIN_WINDOW_H

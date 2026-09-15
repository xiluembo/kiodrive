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

#include "common/types.h"
#include "kio/capability_resolver.h"
#include "kio/credential_broker.h"
#include "kio/protocol_registry.h"
#include "mounts/mount_conflict_detector.h"
#include "mounts/mount_registry.h"
#include "sync/sync_engine_core.h"

#include <KLocalizedString>

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace KIODrive
{

namespace
{

QString absoluteMountPath(MountRegistry *registry, const MountInfo &mount)
{
    if (!registry) {
        return mount.localRelativePath;
    }
    return QDir(registry->syncRootPath()).filePath(mount.localRelativePath);
}

} // namespace

MainWindow::MainWindow(SyncEngineCore *engine, QWidget *parent)
    : QMainWindow(parent)
    , m_engine(engine)
{
    setWindowTitle(i18n("KIODrive"));
    resize(960, 480);

    auto *toolbar = addToolBar(i18n("Main"));
    toolbar->addAction(i18n("Add mount"), this, &MainWindow::addMount);
    toolbar->addAction(i18n("Remove mount"), this, &MainWindow::removeSelectedMount);
    toolbar->addAction(i18n("Open in Explorer"), this, &MainWindow::openSelectedInExplorer);
    toolbar->addAction(i18n("Reconnect"), this, &MainWindow::reconnectSelected);
    toolbar->addAction(i18n("Refresh"), this, &MainWindow::refreshMountTable);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({
        i18n("Name"),
        i18n("Remote URL"),
        i18n("Local path"),
        i18n("Protocol"),
        i18n("State"),
        i18n("Capabilities"),
        i18n("Read-only"),
        i18n("Mount ID"),
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    setCentralWidget(m_table);

    refreshMountTable();
}

void MainWindow::refreshMountTable()
{
    if (!m_engine || !m_engine->mountRegistry()) {
        return;
    }
    MountRegistry *registry = m_engine->mountRegistry();
    const QVector<MountInfo> mounts = registry->listMounts();
    m_table->setRowCount(mounts.size());
    int row = 0;
    for (const MountInfo &mount : mounts) {
        const KioCapabilities caps = CapabilityResolver::resolve(mount.remoteUrl);
        const QString capsSummary = QStringLiteral("list=%1 read=%2 write=%3 open=%4")
                                        .arg(caps.listing)
                                        .arg(caps.reading)
                                        .arg(caps.writing)
                                        .arg(caps.opening);
        m_table->setItem(row, 0, new QTableWidgetItem(mount.name));
        m_table->setItem(row, 1, new QTableWidgetItem(mount.remoteUrl.toString()));
        m_table->setItem(row, 2, new QTableWidgetItem(absoluteMountPath(registry, mount)));
        m_table->setItem(row, 3, new QTableWidgetItem(mount.protocol));
        m_table->setItem(row, 4, new QTableWidgetItem(mountStateToString(mount.state)));
        m_table->setItem(row, 5, new QTableWidgetItem(capsSummary));
        m_table->setItem(row, 6, new QTableWidgetItem(mount.readOnly ? i18n("yes") : i18n("no")));
        m_table->setItem(row, 7, new QTableWidgetItem(mount.id.toString(QUuid::WithoutBraces)));
        ++row;
    }
}

void MainWindow::addMount()
{
    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Add mount"));
    auto *layout = new QFormLayout(&dialog);
    auto *nameEdit = new QLineEdit(&dialog);
    auto *urlEdit = new QLineEdit(&dialog);
    auto *userEdit = new QLineEdit(&dialog);
    auto *passwordEdit = new QLineEdit(&dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);
    auto *folderEdit = new QLineEdit(&dialog);
    layout->addRow(i18n("Name"), nameEdit);
    layout->addRow(i18n("Remote URL"), urlEdit);
    layout->addRow(i18n("Folder name under KIO"), folderEdit);
    layout->addRow(i18n("Username"), userEdit);
    layout->addRow(i18n("Password"), passwordEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    urlEdit->setPlaceholderText(QStringLiteral("file:///E:/pasta/  or  sftp://host/path/"));
    folderEdit->setPlaceholderText(QStringLiteral("PersonalServer"));

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QUrl url(urlEdit->text().trimmed());
    if (!url.isValid() || url.scheme().isEmpty()) {
        QMessageBox::warning(this, i18n("KIODrive"), i18n("Enter a valid remote URL."));
        return;
    }
    if (!ProtocolRegistry::isFilesystemLike(url.scheme())) {
        QMessageBox::warning(this, i18n("KIODrive"), i18n("Protocol is not available as a filesystem mount."));
        return;
    }

    MountRegistry *registry = m_engine->mountRegistry();
    if (!registry) {
        return;
    }

    MountInfo candidate;
    candidate.id = QUuid::createUuid();
    candidate.name = nameEdit->text().trimmed();
    candidate.localRelativePath = folderEdit->text().trimmed();
    candidate.remoteUrl = url;
    candidate.protocol = url.scheme();
    candidate.readOnly = false;
    candidate.state = MountState::Disabled;

    const ValidationResult validation =
        MountConflictDetector::validate(candidate, registry->listMounts(), registry->syncRootPath());
    if (!validation.ok) {
        QMessageBox::warning(this, i18n("KIODrive"), validation.message);
        return;
    }

    const std::optional<MountInfo> created = registry->createMount(candidate);
    if (!created) {
        QMessageBox::warning(this, i18n("KIODrive"), registry->lastError());
        return;
    }

    if (!userEdit->text().isEmpty() || !passwordEdit->text().isEmpty()) {
        Credential credential;
        credential.username = userEdit->text();
        credential.password = passwordEdit->text();
        if (m_engine->credentialBroker()) {
            m_engine->credentialBroker()->store(created->id, credential);
        }
    }

    registry->activateMount(created->id);
    registry->setMountState(created->id, MountState::Online);
    m_engine->materializeMountRoots();
    m_engine->onPlaceholdersRequested(created->localRelativePath);
    refreshMountTable();
}

void MainWindow::removeSelectedMount()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    const QUuid id = QUuid::fromString(m_table->item(row, 7)->text());
    const auto answer = QMessageBox::question(this,
                                              i18n("Remove mount"),
                                              i18n("Remove this mount from KIODrive? Remote content will not be deleted."),
                                              QMessageBox::Yes | QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_engine->mountRegistry()->removeMount(id);
    refreshMountTable();
}

void MainWindow::openSelectedInExplorer()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_table->item(row, 2)->text()));
}

void MainWindow::reconnectSelected()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    const QUuid id = QUuid::fromString(m_table->item(row, 7)->text());
    MountRegistry *registry = m_engine->mountRegistry();
    registry->activateMount(id);
    registry->setMountState(id, MountState::Online);
    const auto mount = registry->mount(id);
    if (mount) {
        m_engine->onPlaceholdersRequested(mount->localRelativePath);
    }
    refreshMountTable();
}

} // namespace KIODrive

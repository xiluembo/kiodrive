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

#ifndef KIODRIVE_SYNC_LOCAL_CHANGE_MONITOR_H
#define KIODRIVE_SYNC_LOCAL_CHANGE_MONITOR_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <atomic>
#include <memory>
#include <thread>

namespace KIODrive
{

enum class LocalChangeType {
    Created,
    Modified,
    Removed,
    Renamed,
    AttributeChanged,
};

inline QString localChangeTypeName(LocalChangeType type)
{
    switch (type) {
    case LocalChangeType::Created:
        return QStringLiteral("Created");
    case LocalChangeType::Modified:
        return QStringLiteral("Modified");
    case LocalChangeType::Removed:
        return QStringLiteral("Removed");
    case LocalChangeType::Renamed:
        return QStringLiteral("Renamed");
    case LocalChangeType::AttributeChanged:
        return QStringLiteral("AttributeChanged");
    }
    return QStringLiteral("Unknown");
}

struct LocalChange {
    LocalChangeType type;
    QString path;
    QString optionalNewPath;
};

/**
 * Watches the sync root for local edits. Suppresses provider-originated events.
 * Windows implementation uses ReadDirectoryChangesW.
 */
class LocalChangeMonitor : public QObject
{
    Q_OBJECT

public:
    explicit LocalChangeMonitor(QObject *parent = nullptr);
    ~LocalChangeMonitor() override;

    bool start(const QString &rootPath);
    void stop();
    void suppressPath(const QString &absolutePath);
    void unsuppressPath(const QString &absolutePath);

Q_SIGNALS:
    void changeDetected(const LocalChange &change);

private:
#ifdef Q_OS_WIN
    void watchLoop();
    void enqueueDebounced(LocalChangeType type, const QString &absolutePath, const QString &optionalNewPath = {});
    void flushDebounced(const QString &absolutePath);
#endif
    bool isSuppressed(const QString &absolutePath) const;

    QString m_rootPath;
    QSet<QString> m_suppressed;
    std::atomic_bool m_running{false};
#ifdef Q_OS_WIN
    void *m_directoryHandle = nullptr;
    void *m_stopEvent = nullptr;
    std::unique_ptr<std::thread> m_thread;
    QHash<QString, LocalChange> m_pending;
    QHash<QString, QTimer *> m_debounceTimers;
    QString m_pendingRenameOld;
    int m_debounceMs = 400;
#endif
};

} // namespace KIODrive

#endif // KIODRIVE_SYNC_LOCAL_CHANGE_MONITOR_H

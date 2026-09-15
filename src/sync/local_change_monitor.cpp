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

#include "local_change_monitor.h"

#include "common/logging.h"
#include "common/path_utils.h"

#include <QDir>
#include <QMetaObject>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace KIODrive
{

LocalChangeMonitor::LocalChangeMonitor(QObject *parent)
    : QObject(parent)
{
}

LocalChangeMonitor::~LocalChangeMonitor()
{
    stop();
}

bool LocalChangeMonitor::isSuppressed(const QString &absolutePath) const
{
    return m_suppressed.contains(PathUtils::normalizedPathKey(absolutePath));
}

void LocalChangeMonitor::suppressPath(const QString &absolutePath)
{
    m_suppressed.insert(PathUtils::normalizedPathKey(absolutePath));
}

void LocalChangeMonitor::unsuppressPath(const QString &absolutePath)
{
    m_suppressed.remove(PathUtils::normalizedPathKey(absolutePath));
}

bool LocalChangeMonitor::start(const QString &rootPath)
{
    stop();
    m_rootPath = rootPath;

#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(rootPath);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        qCWarning(KIODRIVE_SYNC_LOG) << "ReadDirectoryChangesW root open failed" << GetLastError();
        return false;
    }
    m_directoryHandle = handle;
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) {
        CloseHandle(handle);
        m_directoryHandle = nullptr;
        return false;
    }

    m_running = true;
    m_thread = std::make_unique<std::thread>([this]() { watchLoop(); });
    qCInfo(KIODRIVE_SYNC_LOG) << "LocalChangeMonitor started on" << rootPath;
    return true;
#else
    m_running = true;
    qCInfo(KIODRIVE_SYNC_LOG) << "LocalChangeMonitor stub started on" << rootPath;
    return true;
#endif
}

void LocalChangeMonitor::stop()
{
    m_running = false;
#ifdef Q_OS_WIN
    if (m_stopEvent) {
        SetEvent(static_cast<HANDLE>(m_stopEvent));
    }
    if (m_directoryHandle) {
        CancelIoEx(static_cast<HANDLE>(m_directoryHandle), nullptr);
    }
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    m_thread.reset();

    for (QTimer *timer : std::as_const(m_debounceTimers)) {
        timer->stop();
        timer->deleteLater();
    }
    m_debounceTimers.clear();
    m_pending.clear();

    if (m_directoryHandle) {
        CloseHandle(static_cast<HANDLE>(m_directoryHandle));
        m_directoryHandle = nullptr;
    }
    if (m_stopEvent) {
        CloseHandle(static_cast<HANDLE>(m_stopEvent));
        m_stopEvent = nullptr;
    }
#endif
}

#ifdef Q_OS_WIN

void LocalChangeMonitor::enqueueDebounced(LocalChangeType type, const QString &absolutePath, const QString &optionalNewPath)
{
    if (absolutePath.isEmpty() || isSuppressed(absolutePath)) {
        qCDebug(KIODRIVE_SYNC_LOG) << "FS event suppressed" << localChangeTypeName(type) << absolutePath;
        return;
    }

    qCInfo(KIODRIVE_SYNC_LOG) << "FS event queued" << localChangeTypeName(type) << absolutePath
                              << "debounceMs" << m_debounceMs;

    LocalChange change;
    change.type = type;
    change.path = absolutePath;
    change.optionalNewPath = optionalNewPath;
    m_pending.insert(absolutePath, change);

    QTimer *&timer = m_debounceTimers[absolutePath];
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, absolutePath]() { flushDebounced(absolutePath); });
    }
    timer->start(m_debounceMs);
}

void LocalChangeMonitor::flushDebounced(const QString &absolutePath)
{
    const auto it = m_pending.find(absolutePath);
    if (it == m_pending.end()) {
        return;
    }
    const LocalChange change = it.value();
    m_pending.erase(it);
    if (auto *timer = m_debounceTimers.take(absolutePath)) {
        timer->deleteLater();
    }
    if (isSuppressed(change.path)) {
        return;
    }
    qCInfo(KIODRIVE_SYNC_LOG) << "Local change" << localChangeTypeName(change.type) << change.path
                              << (change.optionalNewPath.isEmpty() ? QString() : QStringLiteral("-> ") + change.optionalNewPath);
    Q_EMIT changeDetected(change);
}

void LocalChangeMonitor::watchLoop()
{
    auto *dirHandle = static_cast<HANDLE>(m_directoryHandle);
    auto *stopEvent = static_cast<HANDLE>(m_stopEvent);
    alignas(FILE_NOTIFY_INFORMATION) BYTE buffer[64 * 1024];

    while (m_running.load()) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) {
            break;
        }

        const BOOL queued = ReadDirectoryChangesW(dirHandle,
                                                  buffer,
                                                  sizeof(buffer),
                                                  TRUE,
                                                  FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
                                                      | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE
                                                      | FILE_NOTIFY_CHANGE_CREATION,
                                                  nullptr,
                                                  &overlapped,
                                                  nullptr);
        if (!queued) {
            const DWORD err = GetLastError();
            CloseHandle(overlapped.hEvent);
            if (err == ERROR_NOTIFY_ENUM_DIR) {
                qCWarning(KIODRIVE_SYNC_LOG) << "ReadDirectoryChangesW overflow; continuing";
                continue;
            }
            if (!m_running.load()) {
                break;
            }
            qCWarning(KIODRIVE_SYNC_LOG) << "ReadDirectoryChangesW failed" << err;
            break;
        }

        HANDLE waits[2] = {overlapped.hEvent, stopEvent};
        const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(dirHandle, &overlapped);
            WaitForSingleObject(overlapped.hEvent, 2000);
            CloseHandle(overlapped.hEvent);
            break;
        }

        DWORD bytes = 0;
        if (!GetOverlappedResult(dirHandle, &overlapped, &bytes, FALSE) || bytes == 0) {
            CloseHandle(overlapped.hEvent);
            continue;
        }
        CloseHandle(overlapped.hEvent);

        auto *info = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(buffer);
        for (;;) {
            const QString relative = QString::fromWCharArray(info->FileName, static_cast<int>(info->FileNameLength / sizeof(WCHAR)));
            const QString absolute = QDir::fromNativeSeparators(QDir(m_rootPath).filePath(QDir::fromNativeSeparators(relative)));

            bool advance = true;
            switch (info->Action) {
            case FILE_ACTION_ADDED:
                qCInfo(KIODRIVE_SYNC_LOG) << "ReadDirectoryChanges FILE_ACTION_ADDED" << absolute;
                QMetaObject::invokeMethod(
                    this,
                    [this, absolute]() {
                        enqueueDebounced(LocalChangeType::Created, absolute);
                    },
                    Qt::QueuedConnection);
                break;
            case FILE_ACTION_REMOVED:
                qCInfo(KIODRIVE_SYNC_LOG) << "ReadDirectoryChanges FILE_ACTION_REMOVED" << absolute;
                QMetaObject::invokeMethod(
                    this,
                    [this, absolute]() {
                        enqueueDebounced(LocalChangeType::Removed, absolute);
                    },
                    Qt::QueuedConnection);
                break;
            case FILE_ACTION_RENAMED_OLD_NAME: {
                const QString oldPath = absolute;
                qCInfo(KIODRIVE_SYNC_LOG) << "ReadDirectoryChanges FILE_ACTION_RENAMED_OLD_NAME" << oldPath;
                QMetaObject::invokeMethod(
                    this,
                    [this, oldPath]() {
                        m_pendingRenameOld = oldPath;
                    },
                    Qt::QueuedConnection);
                break;
            }
            case FILE_ACTION_RENAMED_NEW_NAME: {
                const QString newPath = absolute;
                qCInfo(KIODRIVE_SYNC_LOG) << "ReadDirectoryChanges FILE_ACTION_RENAMED_NEW_NAME" << newPath;
                QMetaObject::invokeMethod(
                    this,
                    [this, newPath]() {
                        if (m_pendingRenameOld.isEmpty()) {
                            qCWarning(KIODRIVE_SYNC_LOG)
                                << "RENAMED_NEW_NAME without pending OLD_NAME" << newPath;
                            return;
                        }
                        LocalChange change;
                        change.type = LocalChangeType::Renamed;
                        change.path = m_pendingRenameOld;
                        change.optionalNewPath = newPath;
                        qCInfo(KIODRIVE_SYNC_LOG) << "Paired rename OLD->NEW" << change.path << "->"
                                                  << change.optionalNewPath;
                        m_pendingRenameOld.clear();
                        if (isSuppressed(change.path) || isSuppressed(change.optionalNewPath)) {
                            qCDebug(KIODRIVE_SYNC_LOG) << "Rename suppressed" << change.path << "->"
                                                       << change.optionalNewPath;
                            return;
                        }
                        qCInfo(KIODRIVE_SYNC_LOG) << "Local rename" << change.path << "->" << change.optionalNewPath;
                        Q_EMIT changeDetected(change);
                    },
                    Qt::QueuedConnection);
                break;
            }
            case FILE_ACTION_MODIFIED:
            default:
                qCInfo(KIODRIVE_SYNC_LOG) << "ReadDirectoryChanges FILE_ACTION_MODIFIED/other"
                                          << info->Action << absolute;
                QMetaObject::invokeMethod(
                    this,
                    [this, absolute]() {
                        enqueueDebounced(LocalChangeType::Modified, absolute);
                    },
                    Qt::QueuedConnection);
                break;
            }

            Q_UNUSED(advance);
            if (info->NextEntryOffset == 0) {
                break;
            }
            info = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(reinterpret_cast<BYTE *>(info) + info->NextEntryOffset);
        }
    }
}

#endif // Q_OS_WIN

} // namespace KIODrive

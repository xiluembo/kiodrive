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

#ifndef KIODRIVE_KIO_MOCK_KIO_BACKEND_H
#define KIODRIVE_KIO_MOCK_KIO_BACKEND_H

#include "kio_backend.h"

#include <QHash>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace KIODrive
{

/**
 * In-memory KIO backend for unit tests. Supports injectable latency and errors.
 */
class MockKioBackend : public KioBackend
{
    Q_OBJECT

public:
    explicit MockKioBackend(QObject *parent = nullptr);
    ~MockKioBackend() override;

    void setLatencyMs(int ms);
    int latencyMs() const;

    /** If set, the next operation fails with this code/message then clears. */
    void injectError(int code, const QString &message);
    void clearInjectedError();

    void seedFile(const QUrl &url, const QByteArray &content, qint64 mtime = 0);
    void seedDirectory(const QUrl &url, qint64 mtime = 0);
    bool exists(const QUrl &url) const;
    QByteArray fileContent(const QUrl &url) const;

    void listDir(const QUrl &url) override;
    void stat(const QUrl &url) override;
    void get(const QUrl &url, qint64 offset = 0, qint64 length = -1) override;
    void open(const QUrl &url) override;
    void seek(qint64 offset) override;
    void read(qint64 length) override;
    void closeFile() override;
    void rename(const QUrl &src, const QUrl &dest, bool overwrite = false) override;
    void del(const QUrl &url, bool isFile = true) override;
    void mkdir(const QUrl &url) override;
    void put(const QUrl &url, const QByteArray &data, bool overwrite = true) override;

private:
    struct Node {
        ItemKind kind = ItemKind::File;
        QByteArray content;
        qint64 mtime = 0;
        QHash<QString, std::shared_ptr<Node>> children;
    };

    using FinishFn = std::function<void(const BackendResult &)>;

    void schedule(FinishFn emitFn, const BackendResult &result);
    std::optional<BackendResult> takeInjectedError();
    QString pathKey(const QUrl &url) const;
    std::shared_ptr<Node> findNode(const QUrl &url) const;
    std::shared_ptr<Node> ensureParent(const QUrl &url);
    static QUrl parentUrl(const QUrl &url);
    static QString fileName(const QUrl &url);

    std::shared_ptr<Node> m_root;
    int m_latencyMs = 0;
    std::optional<std::pair<int, QString>> m_injectedError;

    QUrl m_openUrl;
    qint64 m_openOffset = 0;
    bool m_open = false;
};

} // namespace KIODrive

#endif // KIODRIVE_KIO_MOCK_KIO_BACKEND_H

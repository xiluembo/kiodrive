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

#include "mock_kio_backend.h"

#include "common/path_utils.h"

#include <KIO/Global>

#include <QTimer>

namespace KIODrive
{

MockKioBackend::MockKioBackend(QObject *parent)
    : KioBackend(parent)
    , m_root(std::make_shared<Node>())
{
    m_root->kind = ItemKind::Directory;
}

MockKioBackend::~MockKioBackend() = default;

void MockKioBackend::setLatencyMs(int ms)
{
    m_latencyMs = qMax(0, ms);
}

int MockKioBackend::latencyMs() const
{
    return m_latencyMs;
}

void MockKioBackend::injectError(int code, const QString &message)
{
    m_injectedError = std::make_pair(code, message);
}

void MockKioBackend::clearInjectedError()
{
    m_injectedError.reset();
}

std::optional<BackendResult> MockKioBackend::takeInjectedError()
{
    if (!m_injectedError) {
        return std::nullopt;
    }
    const auto err = *m_injectedError;
    m_injectedError.reset();
    return BackendResult::failure(err.first, err.second);
}

void MockKioBackend::schedule(FinishFn emitFn, const BackendResult &result)
{
    // Always defer so callers can connect before the signal fires.
    const int delay = qMax(0, m_latencyMs);
    QTimer::singleShot(delay, this, [emitFn, result]() {
        emitFn(result);
    });
}

QString MockKioBackend::pathKey(const QUrl &url) const
{
    return PathUtils::normalizeLexical(url.path());
}

QString MockKioBackend::fileName(const QUrl &url)
{
    const QString path = PathUtils::normalizeLexical(url.path());
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash < 0) {
        return path;
    }
    return path.mid(slash + 1);
}

QUrl MockKioBackend::parentUrl(const QUrl &url)
{
    QUrl parent = url;
    QString path = PathUtils::normalizeLexical(url.path());
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0) {
        parent.setPath(QStringLiteral("/"));
    } else {
        parent.setPath(path.left(slash));
    }
    return parent;
}

std::shared_ptr<MockKioBackend::Node> MockKioBackend::findNode(const QUrl &url) const
{
    QString path = pathKey(url);
    if (path.isEmpty() || path == QLatin1String("/")) {
        return m_root;
    }
    if (path.startsWith(QLatin1Char('/'))) {
        path = path.mid(1);
    }
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    std::shared_ptr<Node> node = m_root;
    for (const QString &part : parts) {
        const auto it = node->children.constFind(part);
        if (it == node->children.cend()) {
            return nullptr;
        }
        node = *it;
    }
    return node;
}

std::shared_ptr<MockKioBackend::Node> MockKioBackend::ensureParent(const QUrl &url)
{
    const QUrl parent = parentUrl(url);
    auto node = findNode(parent);
    if (!node) {
        // Create intermediate directories.
        QString path = pathKey(parent);
        if (path.startsWith(QLatin1Char('/'))) {
            path = path.mid(1);
        }
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        node = m_root;
        for (const QString &part : parts) {
            auto it = node->children.find(part);
            if (it == node->children.end()) {
                auto child = std::make_shared<Node>();
                child->kind = ItemKind::Directory;
                it = node->children.insert(part, child);
            }
            node = *it;
            if (node->kind != ItemKind::Directory) {
                return nullptr;
            }
        }
    }
    if (node->kind != ItemKind::Directory) {
        return nullptr;
    }
    return node;
}

void MockKioBackend::seedDirectory(const QUrl &url, qint64 mtime)
{
    if (pathKey(url) == QLatin1String("/") || pathKey(url).isEmpty()) {
        m_root->mtime = mtime;
        return;
    }
    auto parent = ensureParent(url);
    if (!parent) {
        return;
    }
    auto node = std::make_shared<Node>();
    node->kind = ItemKind::Directory;
    node->mtime = mtime;
    parent->children.insert(fileName(url), node);
}

void MockKioBackend::seedFile(const QUrl &url, const QByteArray &content, qint64 mtime)
{
    auto parent = ensureParent(url);
    if (!parent) {
        return;
    }
    auto node = std::make_shared<Node>();
    node->kind = ItemKind::File;
    node->content = content;
    node->mtime = mtime;
    parent->children.insert(fileName(url), node);
}

bool MockKioBackend::exists(const QUrl &url) const
{
    return findNode(url) != nullptr;
}

QByteArray MockKioBackend::fileContent(const QUrl &url) const
{
    auto node = findNode(url);
    if (!node || node->kind != ItemKind::File) {
        return {};
    }
    return node->content;
}

void MockKioBackend::listDir(const QUrl &url)
{
    if (auto err = takeInjectedError()) {
        schedule([this](const BackendResult &r) { Q_EMIT listDirFinished(r); }, *err);
        return;
    }

    auto node = findNode(url);
    BackendResult result;
    if (!node) {
        result = BackendResult::failure(KIO::ERR_DOES_NOT_EXIST, QStringLiteral("Not found"));
    } else if (node->kind != ItemKind::Directory) {
        result = BackendResult::failure(KIO::ERR_IS_FILE, QStringLiteral("Not a directory"));
    } else {
        result = BackendResult::success();
        for (auto it = node->children.cbegin(); it != node->children.cend(); ++it) {
            RemoteEntry entry;
            entry.name = it.key();
            entry.displayName = it.key();
            entry.kind = it.value()->kind;
            entry.size = it.value()->kind == ItemKind::File ? it.value()->content.size() : -1;
            entry.mtime = it.value()->mtime;
            result.entries.append(entry);
        }
    }
    schedule([this](const BackendResult &r) { Q_EMIT listDirFinished(r); }, result);
}

void MockKioBackend::stat(const QUrl &url)
{
    if (auto err = takeInjectedError()) {
        schedule([this](const BackendResult &r) { Q_EMIT statFinished(r); }, *err);
        return;
    }

    auto node = findNode(url);
    BackendResult result;
    if (!node) {
        result = BackendResult::failure(KIO::ERR_DOES_NOT_EXIST, QStringLiteral("Not found"));
    } else {
        result = BackendResult::success();
        result.stat.name = fileName(url);
        if (result.stat.name.isEmpty()) {
            result.stat.name = QStringLiteral("/");
        }
        result.stat.displayName = result.stat.name;
        result.stat.kind = node->kind;
        result.stat.size = node->kind == ItemKind::File ? node->content.size() : -1;
        result.stat.mtime = node->mtime;
    }
    schedule([this](const BackendResult &r) { Q_EMIT statFinished(r); }, result);
}

void MockKioBackend::get(const QUrl &url, qint64 offset, qint64 length)
{
    if (auto err = takeInjectedError()) {
        schedule([this](const BackendResult &r) { Q_EMIT getFinished(r); }, *err);
        return;
    }

    auto node = findNode(url);
    BackendResult result;
    if (!node || node->kind != ItemKind::File) {
        result = BackendResult::failure(KIO::ERR_DOES_NOT_EXIST, QStringLiteral("Not found"));
    } else {
        result = BackendResult::success();
        QByteArray data = node->content;
        if (offset > 0) {
            if (offset >= data.size()) {
                data.clear();
            } else {
                data = data.mid(static_cast<int>(offset));
            }
        }
        if (length >= 0 && data.size() > length) {
            data = data.left(static_cast<int>(length));
        }
        result.data = data;
    }
    schedule([this](const BackendResult &r) { Q_EMIT getFinished(r); }, result);
}

void MockKioBackend::open(const QUrl &url)
{
    if (auto err = takeInjectedError()) {
        schedule([this](const BackendResult &r) { Q_EMIT openFinished(r); }, *err);
        return;
    }
    auto node = findNode(url);
    BackendResult result;
    if (!node || node->kind != ItemKind::File) {
        result = BackendResult::failure(KIO::ERR_DOES_NOT_EXIST, QStringLiteral("Not found"));
    } else if (m_open) {
        result = BackendResult::failure(-1, QStringLiteral("Already open"));
    } else {
        m_open = true;
        m_openUrl = url;
        m_openOffset = 0;
        result = BackendResult::success();
    }
    schedule([this](const BackendResult &r) { Q_EMIT openFinished(r); }, result);
}

void MockKioBackend::seek(qint64 offset)
{
    if (auto err = takeInjectedError()) {
        schedule([this](const BackendResult &r) { Q_EMIT seekFinished(r); }, *err);
        return;
    }
    BackendResult result;
    if (!m_open) {
        result = BackendResult::failure(-1, QStringLiteral("No open file"));
    } else {
        m_openOffset = qMax<qint64>(0, offset);
        result = BackendResult::success();
    }
    schedule([this](const BackendResult &r) { Q_EMIT seekFinished(r); }, result);
}

void MockKioBackend::read(qint64 length)
{
    if (auto err = takeInjectedError()) {
        schedule([this](const BackendResult &r) { Q_EMIT readFinished(r); }, *err);
        return;
    }
    BackendResult result;
    if (!m_open) {
        result = BackendResult::failure(-1, QStringLiteral("No open file"));
    } else {
        auto node = findNode(m_openUrl);
        if (!node) {
            result = BackendResult::failure(KIO::ERR_DOES_NOT_EXIST, QStringLiteral("Not found"));
        } else {
            result = BackendResult::success();
            QByteArray data = node->content;
            if (m_openOffset >= data.size()) {
                result.data.clear();
            } else {
                data = data.mid(static_cast<int>(m_openOffset));
                if (length >= 0 && data.size() > length) {
                    data = data.left(static_cast<int>(length));
                }
                result.data = data;
                m_openOffset += data.size();
            }
        }
    }
    schedule([this](const BackendResult &r) { Q_EMIT readFinished(r); }, result);
}

void MockKioBackend::closeFile()
{
    m_open = false;
    m_openUrl = QUrl();
    m_openOffset = 0;
    schedule([this](const BackendResult &r) { Q_EMIT closeFinished(r); }, BackendResult::success());
}

void MockKioBackend::rename(const QUrl &src, const QUrl &dest, bool overwrite)
{
    if (auto err = takeInjectedError()) {
        schedule([this](const BackendResult &r) { Q_EMIT renameFinished(r); }, *err);
        return;
    }

    BackendResult result;
    auto srcNode = findNode(src);
    auto destParent = ensureParent(dest);
    if (!srcNode || !destParent) {
        result = BackendResult::failure(KIO::ERR_DOES_NOT_EXIST, QStringLiteral("Not found"));
    } else if (destParent->children.contains(fileName(dest)) && !overwrite) {
        result = BackendResult::failure(KIO::ERR_FILE_ALREADY_EXIST, QStringLiteral("Exists"));
    } else {
        auto srcParent = findNode(parentUrl(src));
        if (!srcParent) {
            result = BackendResult::failure(KIO::ERR_DOES_NOT_EXIST, QStringLiteral("Not found"));
        } else {
            auto node = srcParent->children.take(fileName(src));
            destParent->children.insert(fileName(dest), node);
            result = BackendResult::success();
        }
    }
    schedule([this](const BackendResult &r) { Q_EMIT renameFinished(r); }, result);
}

void MockKioBackend::del(const QUrl &url, bool isFile)
{
    Q_UNUSED(isFile);
    if (auto err = takeInjectedError()) {
        schedule([this](const BackendResult &r) { Q_EMIT delFinished(r); }, *err);
        return;
    }

    BackendResult result;
    auto parent = findNode(parentUrl(url));
    if (!parent || !parent->children.contains(fileName(url))) {
        result = BackendResult::failure(KIO::ERR_DOES_NOT_EXIST, QStringLiteral("Not found"));
    } else {
        parent->children.remove(fileName(url));
        result = BackendResult::success();
    }
    schedule([this](const BackendResult &r) { Q_EMIT delFinished(r); }, result);
}

void MockKioBackend::mkdir(const QUrl &url)
{
    if (auto err = takeInjectedError()) {
        schedule([this](const BackendResult &r) { Q_EMIT mkdirFinished(r); }, *err);
        return;
    }

    BackendResult result;
    if (findNode(url)) {
        result = BackendResult::failure(KIO::ERR_DIR_ALREADY_EXIST, QStringLiteral("Exists"));
    } else {
        auto parent = ensureParent(url);
        if (!parent) {
            result = BackendResult::failure(KIO::ERR_CANNOT_MKDIR, QStringLiteral("Cannot mkdir"));
        } else {
            auto node = std::make_shared<Node>();
            node->kind = ItemKind::Directory;
            parent->children.insert(fileName(url), node);
            result = BackendResult::success();
        }
    }
    schedule([this](const BackendResult &r) { Q_EMIT mkdirFinished(r); }, result);
}

void MockKioBackend::put(const QUrl &url, const QByteArray &data, bool overwrite)
{
    if (auto err = takeInjectedError()) {
        schedule([this](const BackendResult &r) { Q_EMIT putFinished(r); }, *err);
        return;
    }

    BackendResult result;
    if (findNode(url) && !overwrite) {
        result = BackendResult::failure(KIO::ERR_FILE_ALREADY_EXIST, QStringLiteral("Exists"));
    } else {
        seedFile(url, data);
        result = BackendResult::success();
    }
    schedule([this](const BackendResult &r) { Q_EMIT putFinished(r); }, result);
}

} // namespace KIODrive

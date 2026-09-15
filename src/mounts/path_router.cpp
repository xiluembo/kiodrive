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

#include "path_router.h"

#include "common/logging.h"
#include "common/path_utils.h"

#include <QStringList>

namespace KIODrive
{

PathRouter::PathRouter()
    : m_root(std::make_unique<Node>())
{
}

PathRouter::~PathRouter() = default;

QStringList PathRouter::splitKey(const QString &normalizedKey)
{
    QStringList parts = normalizedKey.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts;
}

PathRouter::Node *PathRouter::ensurePath(const QStringList &segments)
{
    Node *node = m_root.get();
    for (const QString &segment : segments) {
        auto it = node->children.find(segment);
        if (it == node->children.end()) {
            it = node->children.emplace(segment, std::make_unique<Node>()).first;
        }
        node = it->second.get();
    }
    return node;
}

const PathRouter::Node *PathRouter::walk(const QStringList &segments) const
{
    const Node *node = m_root.get();
    for (const QString &segment : segments) {
        const auto it = node->children.find(segment);
        if (it == node->children.end()) {
            return nullptr;
        }
        node = it->second.get();
    }
    return node;
}

bool PathRouter::subtreeHasMount(const Node *node) const
{
    if (!node) {
        return false;
    }
    if (node->mountId.has_value()) {
        return true;
    }
    for (const auto &entry : node->children) {
        if (subtreeHasMount(entry.second.get())) {
            return true;
        }
    }
    return false;
}

bool PathRouter::wouldOverlap(const QString &localRelativePath) const
{
    const QString key = PathUtils::normalizedPathKey(localRelativePath);
    if (key.isEmpty() || key == QLatin1String(".")) {
        return true;
    }

    const QStringList segments = splitKey(key);
    const Node *node = m_root.get();

    for (const QString &segment : segments) {
        if (node->mountId.has_value()) {
            // Candidate is under an existing mount.
            return true;
        }
        const auto it = node->children.find(segment);
        if (it == node->children.end()) {
            return false;
        }
        node = it->second.get();
    }

    // Exact path already mounted, or a descendant is mounted.
    return node->mountId.has_value() || subtreeHasMount(node);
}

bool PathRouter::registerMount(const QUuid &mountId, const QString &localRelativePath)
{
    if (mountId.isNull()) {
        return false;
    }
    if (wouldOverlap(localRelativePath)) {
        qCWarning(KIODRIVE_MOUNT_LOG) << "PathRouter rejected overlapping path" << localRelativePath;
        return false;
    }

    const QString key = PathUtils::normalizedPathKey(localRelativePath);
    const QStringList segments = splitKey(key);
    if (segments.isEmpty()) {
        return false;
    }

    Node *node = ensurePath(segments);
    node->mountId = mountId;
    ++m_count;
    return true;
}

bool PathRouter::removeMountRecursive(Node *node, const QUuid &mountId)
{
    if (!node) {
        return false;
    }

    bool removed = false;
    if (node->mountId == mountId) {
        node->mountId.reset();
        removed = true;
    }

    QStringList emptyKeys;
    for (auto it = node->children.begin(); it != node->children.end(); ++it) {
        if (removeMountRecursive(it->second.get(), mountId)) {
            removed = true;
        }
        if (!it->second->mountId.has_value() && it->second->children.empty()) {
            emptyKeys.append(it->first);
        }
    }
    for (const QString &key : emptyKeys) {
        node->children.erase(key);
    }
    return removed;
}

bool PathRouter::unregisterMount(const QUuid &mountId)
{
    if (mountId.isNull()) {
        return false;
    }
    if (!removeMountRecursive(m_root.get(), mountId)) {
        return false;
    }
    --m_count;
    if (m_count < 0) {
        m_count = 0;
    }
    return true;
}

std::optional<QUuid> PathRouter::findOwner(const QString &path) const
{
    const QString key = PathUtils::normalizedPathKey(path);
    const QStringList segments = splitKey(key);

    const Node *node = m_root.get();
    std::optional<QUuid> owner;

    for (const QString &segment : segments) {
        const auto it = node->children.find(segment);
        if (it == node->children.end()) {
            break;
        }
        node = it->second.get();
        if (node->mountId.has_value()) {
            owner = node->mountId;
        }
    }
    return owner;
}

void PathRouter::clear()
{
    m_root = std::make_unique<Node>();
    m_count = 0;
}

int PathRouter::size() const
{
    return m_count;
}

void PathRouter::collectMountIds(const Node *node, QVector<QUuid> *out) const
{
    if (!node || !out) {
        return;
    }
    if (node->mountId.has_value()) {
        out->append(*node->mountId);
    }
    for (const auto &entry : node->children) {
        collectMountIds(entry.second.get(), out);
    }
}

QVector<QUuid> PathRouter::mountIds() const
{
    QVector<QUuid> ids;
    collectMountIds(m_root.get(), &ids);
    return ids;
}

} // namespace KIODrive

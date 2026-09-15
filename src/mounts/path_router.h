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

#ifndef KIODRIVE_MOUNTS_PATH_ROUTER_H
#define KIODRIVE_MOUNTS_PATH_ROUTER_H

#include <QString>
#include <QUuid>
#include <QVector>

#include <map>
#include <memory>
#include <optional>

namespace KIODrive
{

/**
 * Trie of normalized local path keys → mount id.
 * Rejects overlapping mount paths (ancestor/descendant or identical).
 */
class PathRouter
{
public:
    PathRouter();
    ~PathRouter();

    PathRouter(const PathRouter &) = delete;
    PathRouter &operator=(const PathRouter &) = delete;

    /** Register @p localRelativePath for @p mountId. Returns false on overlap. */
    bool registerMount(const QUuid &mountId, const QString &localRelativePath);

    /** Remove mount by id. Returns false if unknown. */
    bool unregisterMount(const QUuid &mountId);

    /** Longest-prefix owner of @p path, or nullopt if none. */
    std::optional<QUuid> findOwner(const QString &path) const;

    /** True if @p localRelativePath would overlap any registered mount. */
    bool wouldOverlap(const QString &localRelativePath) const;

    void clear();
    int size() const;

    QVector<QUuid> mountIds() const;

private:
    struct Node {
        std::map<QString, std::unique_ptr<Node>> children;
        std::optional<QUuid> mountId;
    };

    static QStringList splitKey(const QString &normalizedKey);
    Node *ensurePath(const QStringList &segments);
    const Node *walk(const QStringList &segments) const;
    bool subtreeHasMount(const Node *node) const;
    bool removeMountRecursive(Node *node, const QUuid &mountId);
    void collectMountIds(const Node *node, QVector<QUuid> *out) const;

    std::unique_ptr<Node> m_root;
    int m_count = 0;
};

} // namespace KIODrive

#endif // KIODRIVE_MOUNTS_PATH_ROUTER_H

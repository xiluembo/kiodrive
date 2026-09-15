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

#include "schema.h"

namespace KIODrive
{

int Schema::currentVersion()
{
    return 1;
}

QStringList Schema::migrationSql(int fromVersion)
{
    QStringList statements;
    if (fromVersion < 1) {
        statements << QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_migrations ("
            "  version INTEGER PRIMARY KEY,"
            "  applied_at TEXT NOT NULL"
            ")");
        statements << QStringLiteral(
            "CREATE TABLE IF NOT EXISTS mounts ("
            "  id TEXT PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  local_relpath TEXT NOT NULL UNIQUE,"
            "  remote_url_canonical TEXT NOT NULL,"
            "  protocol TEXT NOT NULL,"
            "  mode TEXT NOT NULL DEFAULT 'readwrite',"
            "  state TEXT NOT NULL,"
            "  caps_json TEXT,"
            "  ttl_seconds INTEGER NOT NULL DEFAULT 30,"
            "  created_at TEXT NOT NULL,"
            "  updated_at TEXT NOT NULL"
            ")");
        statements << QStringLiteral(
            "CREATE TABLE IF NOT EXISTS items ("
            "  id TEXT PRIMARY KEY,"
            "  mount_id TEXT NOT NULL,"
            "  parent_id TEXT,"
            "  remote_path TEXT NOT NULL,"
            "  remote_name TEXT NOT NULL,"
            "  windows_name TEXT NOT NULL,"
            "  kind TEXT NOT NULL,"
            "  mime TEXT,"
            "  size INTEGER,"
            "  mtime INTEGER,"
            "  remote_inode TEXT,"
            "  revision_token TEXT,"
            "  revision_seq INTEGER NOT NULL DEFAULT 0,"
            "  sync_state TEXT NOT NULL,"
            "  pin_state TEXT,"
            "  hydrate_state TEXT,"
            "  metadata_version INTEGER NOT NULL DEFAULT 1,"
            "  FOREIGN KEY(mount_id) REFERENCES mounts(id)"
            ")");
        statements << QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_items_mount_remote "
            "ON items(mount_id, remote_path)");
        statements << QStringLiteral(
            "CREATE TABLE IF NOT EXISTS name_mappings ("
            "  item_id TEXT PRIMARY KEY,"
            "  remote_name TEXT NOT NULL,"
            "  windows_name TEXT NOT NULL,"
            "  reason TEXT NOT NULL,"
            "  FOREIGN KEY(item_id) REFERENCES items(id)"
            ")");
        statements << QStringLiteral(
            "CREATE TABLE IF NOT EXISTS operations ("
            "  id TEXT PRIMARY KEY,"
            "  mount_id TEXT,"
            "  item_id TEXT,"
            "  type TEXT NOT NULL,"
            "  state TEXT NOT NULL,"
            "  payload_json TEXT,"
            "  idempotency_key TEXT UNIQUE,"
            "  created_at TEXT NOT NULL,"
            "  updated_at TEXT NOT NULL"
            ")");
        statements << QStringLiteral(
            "CREATE TABLE IF NOT EXISTS conflicts ("
            "  id TEXT PRIMARY KEY,"
            "  item_id TEXT NOT NULL,"
            "  policy TEXT NOT NULL,"
            "  local_rev TEXT,"
            "  remote_rev TEXT,"
            "  created_at TEXT NOT NULL"
            ")");
        statements << QStringLiteral(
            "CREATE TABLE IF NOT EXISTS transfers ("
            "  id TEXT PRIMARY KEY,"
            "  mount_id TEXT,"
            "  item_id TEXT,"
            "  priority INTEGER NOT NULL DEFAULT 0,"
            "  progress REAL NOT NULL DEFAULT 0,"
            "  retry_count INTEGER NOT NULL DEFAULT 0,"
            "  state TEXT NOT NULL,"
            "  updated_at TEXT NOT NULL"
            ")");
        statements << QStringLiteral(
            "CREATE TABLE IF NOT EXISTS directory_ttls ("
            "  item_id TEXT PRIMARY KEY,"
            "  listed_at TEXT NOT NULL,"
            "  ttl_seconds INTEGER NOT NULL"
            ")");
    }
    return statements;
}

} // namespace KIODrive

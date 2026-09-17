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

#include "cloud_files_callbacks.h"
#include "hydration_manager.h"
#include "placeholder_manager.h"

#include "common/file_identity.h"
#include "common/logging.h"
#include "common/types.h"
#include "kio/kio_backend.h"
#include "metadata/metadata_store.h"
#include "mounts/mount_registry.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include <cstring>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif
#ifndef STATUS_CLOUD_FILE_REQUEST_CANCELLED
#define STATUS_CLOUD_FILE_REQUEST_CANCELLED ((NTSTATUS)0xC000A101L)
#endif

#ifndef CF_SIZE_OF_OP_PARAM
#define KIODRIVE_FIELD_SIZE(type, field) (sizeof(((type *)0)->field))
#define CF_SIZE_OF_OP_PARAM(field) (FIELD_OFFSET(CF_OPERATION_PARAMETERS, field) + KIODRIVE_FIELD_SIZE(CF_OPERATION_PARAMETERS, field))
#endif

namespace KIODrive
{

CloudFilesCallbacks::CloudFilesCallbacks(MetadataStore *store,
                                         MountRegistry *mounts,
                                         KioBackend *backend,
                                         PlaceholderManager *placeholders,
                                         HydrationManager *hydration,
                                         const QString &syncRootPath,
                                         QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_mounts(mounts)
    , m_backend(backend)
    , m_placeholders(placeholders)
    , m_hydration(hydration)
    , m_syncRootPath(QDir::cleanPath(syncRootPath))
{
}

void *CloudFilesCallbacks::callbackContext()
{
    return this;
}

PendingFetchRegistry *CloudFilesCallbacks::pendingFetches()
{
    return &m_pendingFetches;
}

#ifdef Q_OS_WIN

static quint64 requestKeyFor(CF_TRANSFER_KEY transferKey)
{
    static_assert(sizeof(transferKey) <= sizeof(quint64));
    quint64 requestKey = 0;
    std::memcpy(&requestKey, &transferKey, sizeof(transferKey));
    return requestKey;
}

const CF_CALLBACK_REGISTRATION *CloudFilesCallbacks::registrationTable()
{
    static CF_CALLBACK_REGISTRATION table[] = {
        {CF_CALLBACK_TYPE_FETCH_PLACEHOLDERS, &CloudFilesCallbacks::onFetchPlaceholders},
        {CF_CALLBACK_TYPE_CANCEL_FETCH_PLACEHOLDERS, &CloudFilesCallbacks::onCancelFetchPlaceholders},
        {CF_CALLBACK_TYPE_FETCH_DATA, &CloudFilesCallbacks::onFetchData},
        {CF_CALLBACK_TYPE_CANCEL_FETCH_DATA, &CloudFilesCallbacks::onCancelFetchData},
        {CF_CALLBACK_TYPE_NOTIFY_FILE_OPEN_COMPLETION, &CloudFilesCallbacks::onNotifyFileOpenCompletion},
        {CF_CALLBACK_TYPE_NOTIFY_FILE_CLOSE_COMPLETION, &CloudFilesCallbacks::onNotifyFileCloseCompletion},
        {CF_CALLBACK_TYPE_NOTIFY_DEHYDRATE, &CloudFilesCallbacks::onNotifyDehydrate},
        {CF_CALLBACK_TYPE_NOTIFY_DEHYDRATE_COMPLETION, &CloudFilesCallbacks::onNotifyDehydrateCompletion},
        {CF_CALLBACK_TYPE_NOTIFY_RENAME, &CloudFilesCallbacks::onNotifyRename},
        {CF_CALLBACK_TYPE_NOTIFY_RENAME_COMPLETION, &CloudFilesCallbacks::onNotifyRenameCompletion},
        {CF_CALLBACK_TYPE_NOTIFY_DELETE, &CloudFilesCallbacks::onNotifyDelete},
        {CF_CALLBACK_TYPE_NOTIFY_DELETE_COMPLETION, &CloudFilesCallbacks::onNotifyDeleteCompletion},
        CF_CALLBACK_REGISTRATION_END,
    };
    return table;
}

QString CloudFilesCallbacks::toSyncRelativePath(const QString &rawPath, bool *inScope) const
{
    if (inScope) {
        *inScope = false;
    }
    if (rawPath.isEmpty()) {
        return {};
    }

    QString path = QDir::fromNativeSeparators(rawPath.trimmed());
    // Extended device path: //?/C:/Users/... or //?/UNC/...
    if (path.startsWith(QLatin1String("//?/"), Qt::CaseInsensitive)) {
        path = path.mid(4);
    }
    while (path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\'))) {
        path = path.mid(1);
    }
    path = QDir::cleanPath(path);

    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(m_syncRootPath));
    if (root.isEmpty()) {
        return path;
    }

    auto stripPrefix = [](const QString &full, const QString &prefix) -> QString {
        if (prefix.isEmpty()) {
            return {};
        }
        if (full.compare(prefix, Qt::CaseInsensitive) == 0) {
            return QString(); // sync root itself
        }
        const QString withSlash = prefix + QLatin1Char('/');
        if (full.startsWith(withSlash, Qt::CaseInsensitive)) {
            return QDir::cleanPath(full.mid(withSlash.size()));
        }
        return QString(); // not under prefix — caller distinguishes via sentinel
    };

    // Match full path including drive (C:/Users/.../KIO/...).
    if (path.compare(root, Qt::CaseInsensitive) == 0
        || path.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive)) {
        if (inScope) {
            *inScope = true;
        }
        return stripPrefix(path, root);
    }

    // CFAPI often passes volume-relative paths without the drive letter.
    QString rootVol = root;
    if (rootVol.size() >= 2 && rootVol.at(1) == QLatin1Char(':')) {
        rootVol = rootVol.mid(2);
        while (rootVol.startsWith(QLatin1Char('/'))) {
            rootVol = rootVol.mid(1);
        }
    }
    if (!rootVol.isEmpty()
        && (path.compare(rootVol, Qt::CaseInsensitive) == 0
            || path.startsWith(rootVol + QLatin1Char('/'), Qt::CaseInsensitive))) {
        if (inScope) {
            *inScope = true;
        }
        return stripPrefix(path, rootVol);
    }

    // Already sync-relative (e.g. "kiopasta/w/xis"), or sync-root folder name prefix.
    const QString rootName = QFileInfo(root).fileName();
    const bool looksAbsolute = path.size() >= 2 && path.at(1) == QLatin1Char(':');
    if (!looksAbsolute) {
        if (!rootName.isEmpty() && path.compare(rootName, Qt::CaseInsensitive) == 0) {
            if (inScope) {
                *inScope = true;
            }
            return {};
        }
        if (!rootName.isEmpty() && path.startsWith(rootName + QLatin1Char('/'), Qt::CaseInsensitive)) {
            if (inScope) {
                *inScope = true;
            }
            return QDir::cleanPath(path.mid(rootName.size() + 1));
        }
        // Treat as already relative to the sync root.
        if (inScope) {
            *inScope = true;
        }
        return path;
    }

    qCDebug(KIODRIVE_CFAPI_LOG) << "Path outside sync root" << rawPath << "root" << root;
    return {};
}

QString CloudFilesCallbacks::relativePathFromCallback(CONST CF_CALLBACK_INFO *info) const
{
    if (!info || !info->NormalizedPath) {
        return {};
    }
    return toSyncRelativePath(QString::fromWCharArray(info->NormalizedPath));
}

QVector<PlaceholderCreateRequest> CloudFilesCallbacks::mountRootPlaceholders() const
{
    QVector<PlaceholderCreateRequest> items;
    if (!m_mounts) {
        return items;
    }

    QSet<QString> seen;
    for (const MountInfo &mount : m_mounts->listMounts()) {
        const QString folder = mount.localRelativePath.section(QLatin1Char('/'), 0, 0);
        if (folder.isEmpty() || seen.contains(folder)) {
            continue;
        }
        seen.insert(folder);

        PlaceholderCreateRequest req;
        req.fileName = folder;
        req.isDirectory = true;
        req.fileSize = 0;
        req.identity.mountId = mount.id;
        req.identity.itemId = mount.id;
        req.identity.revisionSeq = 1;
        items.push_back(req);
    }
    return items;
}

void CloudFilesCallbacks::transferPlaceholders(CF_CONNECTION_KEY connectionKey,
                                               CF_TRANSFER_KEY transferKey,
                                               const QVector<PlaceholderCreateRequest> &items,
                                               NTSTATUS status)
{
    CF_OPERATION_INFO opInfo{};
    opInfo.StructSize = sizeof(opInfo);
    opInfo.Type = CF_OPERATION_TYPE_TRANSFER_PLACEHOLDERS;
    opInfo.ConnectionKey = connectionKey;
    opInfo.TransferKey = transferKey;

    QVector<CF_PLACEHOLDER_CREATE_INFO> infos;
    QVector<QByteArray> identities;
    QVector<QString> names;
    infos.reserve(items.size());
    identities.reserve(items.size());
    names.reserve(items.size());

    for (const PlaceholderCreateRequest &item : items) {
        names.push_back(item.fileName);
        identities.push_back(item.identity.serialize());

        CF_PLACEHOLDER_CREATE_INFO info{};
        info.RelativeFileName = reinterpret_cast<LPCWSTR>(names.last().utf16());
        info.FileIdentity = identities.last().constData();
        info.FileIdentityLength = static_cast<DWORD>(identities.last().size());
        info.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;
        info.FsMetadata.FileSize.QuadPart = item.isDirectory ? 0 : item.fileSize;
        info.FsMetadata.BasicInfo.FileAttributes =
            item.isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        infos.push_back(info);
    }

    CF_OPERATION_PARAMETERS params{};
    params.ParamSize = CF_SIZE_OF_OP_PARAM(TransferPlaceholders);
    params.TransferPlaceholders.Flags = CF_OPERATION_TRANSFER_PLACEHOLDERS_FLAG_DISABLE_ON_DEMAND_POPULATION;
    params.TransferPlaceholders.CompletionStatus = status;
    params.TransferPlaceholders.PlaceholderTotalCount.QuadPart = items.size();
    params.TransferPlaceholders.PlaceholderCount = static_cast<DWORD>(infos.size());
    params.TransferPlaceholders.PlaceholderArray = infos.isEmpty() ? nullptr : infos.data();
    params.TransferPlaceholders.EntriesProcessed = 0;

    const HRESULT hr = CfExecute(&opInfo, &params);
    if (FAILED(hr)) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "CfExecute TRANSFER_PLACEHOLDERS failed" << Qt::hex
                                      << static_cast<quint32>(hr);
    } else {
        qCInfo(KIODRIVE_CFAPI_LOG) << "Transferred" << items.size() << "placeholders status" << Qt::hex
                                  << static_cast<quint32>(status);
    }
}

void CloudFilesCallbacks::transferData(CF_CONNECTION_KEY connectionKey,
                                       CF_TRANSFER_KEY transferKey,
                                       NTSTATUS status,
                                       quint64 offset,
                                       quint64 length,
                                       const QByteArray &data)
{
    CF_OPERATION_INFO opInfo{};
    opInfo.StructSize = sizeof(opInfo);
    opInfo.Type = CF_OPERATION_TYPE_TRANSFER_DATA;
    opInfo.ConnectionKey = connectionKey;
    opInfo.TransferKey = transferKey;

    CF_OPERATION_PARAMETERS params{};
    params.ParamSize = CF_SIZE_OF_OP_PARAM(TransferData);
    params.TransferData.CompletionStatus = status;
    params.TransferData.Buffer = data.isEmpty() ? nullptr : data.constData();
    params.TransferData.Offset.QuadPart = static_cast<LONGLONG>(offset);
    params.TransferData.Length.QuadPart = static_cast<LONGLONG>(length);

    const HRESULT hr = CfExecute(&opInfo, &params);
    if (FAILED(hr)) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "CfExecute TRANSFER_DATA failed" << Qt::hex << static_cast<quint32>(hr)
                                      << "status" << Qt::hex << static_cast<quint32>(status) << "bytes" << data.size();
    } else {
        qCInfo(KIODRIVE_CFAPI_LOG) << "TRANSFER_DATA ok offset" << offset << "bytes" << data.size();
    }
}

bool CloudFilesCallbacks::resolveHydrationTarget(const QString &relativePath,
                                                 const QByteArray &fileIdentity,
                                                 FileIdentityV1 *identity) const
{
    if (!identity) {
        return false;
    }
    if (!fileIdentity.isEmpty() && FileIdentityV1::deserialize(fileIdentity, identity) && !identity->itemId.isNull()) {
        return true;
    }
    if (!m_mounts || !m_store) {
        return false;
    }
    const auto ownerId = m_mounts->router().findOwner(relativePath);
    if (!ownerId) {
        return false;
    }
    const auto mount = m_mounts->mount(*ownerId);
    if (!mount) {
        return false;
    }

    QString remote = mount->remoteUrl.path();
    QString relInside = relativePath;
    if (relInside.startsWith(mount->localRelativePath)) {
        relInside = relInside.mid(mount->localRelativePath.size());
        if (relInside.startsWith(QLatin1Char('/'))) {
            relInside = relInside.mid(1);
        }
    }
    if (!relInside.isEmpty()) {
        if (!remote.endsWith(QLatin1Char('/'))) {
            remote += QLatin1Char('/');
        }
        remote += relInside;
    }

    const auto item = m_store->itemByRemotePath(mount->id, remote);
    if (!item) {
        return false;
    }
    identity->mountId = mount->id;
    identity->itemId = item->id;
    identity->revisionSeq = item->revisionSeq;
    return true;
}

void CloudFilesCallbacks::processFetchPlaceholders(const QString &relativePath,
                                                   quint64 requestKey,
                                                   CF_CONNECTION_KEY connectionKey,
                                                   CF_TRANSFER_KEY transferKey)
{
    qCInfo(KIODRIVE_CFAPI_LOG) << "FETCH_PLACEHOLDERS" << relativePath;

    if (m_pendingFetches.isCancelled(PendingFetchKind::Placeholders, requestKey)) {
        m_pendingFetches.clear(PendingFetchKind::Placeholders, requestKey);
        transferPlaceholders(connectionKey, transferKey, {}, STATUS_CLOUD_FILE_REQUEST_CANCELLED);
        return;
    }

    if (relativePath.isEmpty()) {
        const QVector<PlaceholderCreateRequest> mounts = mountRootPlaceholders();
        m_pendingFetches.clear(PendingFetchKind::Placeholders, requestKey);
        transferPlaceholders(connectionKey, transferKey, mounts, STATUS_SUCCESS);
        return;
    }

    QVector<PlaceholderCreateRequest> items;
    Q_EMIT placeholdersFetchRequested(relativePath, &items);

    if (m_pendingFetches.isCancelled(PendingFetchKind::Placeholders, requestKey)) {
        m_pendingFetches.clear(PendingFetchKind::Placeholders, requestKey);
        transferPlaceholders(connectionKey, transferKey, {}, STATUS_CLOUD_FILE_REQUEST_CANCELLED);
        return;
    }

    m_pendingFetches.clear(PendingFetchKind::Placeholders, requestKey);
    transferPlaceholders(connectionKey, transferKey, items, STATUS_SUCCESS);
}

void CloudFilesCallbacks::processFetchData(const QString &relativePath,
                                           quint64 offset,
                                           quint64 length,
                                           const QByteArray &fileIdentity,
                                           quint64 requestKey,
                                           CF_CONNECTION_KEY connectionKey,
                                           CF_TRANSFER_KEY transferKey)
{
    qCInfo(KIODRIVE_CFAPI_LOG) << "FETCH_DATA" << relativePath << offset << length;
    Q_EMIT hydrationRequested(relativePath, offset, length);

    if (m_pendingFetches.isCancelled(PendingFetchKind::Data, requestKey)) {
        m_pendingFetches.clear(PendingFetchKind::Data, requestKey);
        transferData(connectionKey, transferKey, STATUS_CLOUD_FILE_REQUEST_CANCELLED, offset, length, {});
        return;
    }

    FileIdentityV1 identity;
    if (!resolveHydrationTarget(relativePath, fileIdentity, &identity)) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "FETCH_DATA: unknown placeholder" << relativePath;
        m_pendingFetches.clear(PendingFetchKind::Data, requestKey);
        transferData(connectionKey, transferKey, STATUS_UNSUCCESSFUL, offset, length, {});
        return;
    }
    if (!m_hydration) {
        m_pendingFetches.clear(PendingFetchKind::Data, requestKey);
        transferData(connectionKey, transferKey, STATUS_UNSUCCESSFUL, offset, length, {});
        return;
    }

    HydrationRequest request;
    request.operationId = QUuid::createUuid();
    request.identity = identity;
    request.requiredOffset = offset;
    request.requiredLength = length;
    request.absolutePath = QDir(m_syncRootPath).filePath(relativePath);

    Q_EMIT hydrationStarted(request.absolutePath);

    m_hydration->hydrate(
        request,
        {},
        [this, connectionKey, transferKey, offset, length, requestKey, absolutePath = request.absolutePath](
            bool ok, const QByteArray &data, const QString &error) {
            Q_EMIT hydrationFinished(absolutePath);
            if (m_pendingFetches.isCancelled(PendingFetchKind::Data, requestKey)) {
                m_pendingFetches.clear(PendingFetchKind::Data, requestKey);
                transferData(connectionKey, transferKey, STATUS_CLOUD_FILE_REQUEST_CANCELLED, offset, length, {});
                return;
            }
            m_pendingFetches.clear(PendingFetchKind::Data, requestKey);
            if (!ok || static_cast<quint64>(data.size()) != length) {
                qCWarning(KIODRIVE_CFAPI_LOG) << "Hydration failed" << error;
                transferData(connectionKey, transferKey, STATUS_UNSUCCESSFUL, offset, length, {});
                return;
            }
            transferData(connectionKey, transferKey, STATUS_SUCCESS, offset, length, data);
        });
}

void CALLBACK CloudFilesCallbacks::onFetchPlaceholders(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params)
{
    Q_UNUSED(params);
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    if (!self) {
        return;
    }
    const QString relative = self->relativePathFromCallback(info);
    const CF_CONNECTION_KEY connectionKey = info->ConnectionKey;
    const CF_TRANSFER_KEY transferKey = info->TransferKey;
    const quint64 requestKey = requestKeyFor(transferKey);
    self->m_pendingFetches.begin(PendingFetchKind::Placeholders, relative, requestKey);

    QMetaObject::invokeMethod(
        self,
        [self, relative, requestKey, connectionKey, transferKey]() {
            self->processFetchPlaceholders(relative, requestKey, connectionKey, transferKey);
        },
        Qt::QueuedConnection);
}

void CALLBACK CloudFilesCallbacks::onFetchData(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params)
{
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    if (!self) {
        return;
    }

    const QString relative = self->relativePathFromCallback(info);
    const quint64 offset = static_cast<quint64>(params->FetchData.RequiredFileOffset.QuadPart);
    const quint64 length = static_cast<quint64>(params->FetchData.RequiredLength.QuadPart);
    const CF_CONNECTION_KEY connectionKey = info->ConnectionKey;
    const CF_TRANSFER_KEY transferKey = info->TransferKey;
    const quint64 requestKey = requestKeyFor(transferKey);
    QByteArray identity;
    if (info->FileIdentity && info->FileIdentityLength > 0) {
        identity = QByteArray(static_cast<const char *>(info->FileIdentity), static_cast<int>(info->FileIdentityLength));
    }
    self->m_pendingFetches.begin(PendingFetchKind::Data, relative, requestKey);

    QMetaObject::invokeMethod(
        self,
        [self, relative, offset, length, identity, requestKey, connectionKey, transferKey]() {
            self->processFetchData(relative, offset, length, identity, requestKey, connectionKey, transferKey);
        },
        Qt::QueuedConnection);
}

void CALLBACK CloudFilesCallbacks::onCancelFetchData(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params)
{
    Q_UNUSED(params);
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    if (!self) {
        return;
    }
    const QString relative = self->relativePathFromCallback(info);
    const quint64 requestKey = requestKeyFor(info->TransferKey);
    qCInfo(KIODRIVE_CFAPI_LOG) << "CANCEL_FETCH_DATA" << relative;
    self->m_pendingFetches.markCancelled(PendingFetchKind::Data, requestKey);
}

void CALLBACK CloudFilesCallbacks::onCancelFetchPlaceholders(CONST CF_CALLBACK_INFO *info,
                                                             CONST CF_CALLBACK_PARAMETERS *params)
{
    Q_UNUSED(params);
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    if (!self) {
        return;
    }
    const QString relative = self->relativePathFromCallback(info);
    const quint64 requestKey = requestKeyFor(info->TransferKey);
    qCInfo(KIODRIVE_CFAPI_LOG) << "CANCEL_FETCH_PLACEHOLDERS" << relative;
    self->m_pendingFetches.markCancelled(PendingFetchKind::Placeholders, requestKey);
}

void CALLBACK CloudFilesCallbacks::onNotifyFileOpenCompletion(CONST CF_CALLBACK_INFO *info,
                                                              CONST CF_CALLBACK_PARAMETERS *params)
{
    Q_UNUSED(params);
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    if (!self) {
        return;
    }
    const QString relative = self->relativePathFromCallback(info);
    QMetaObject::invokeMethod(
        self,
        [self, relative]() {
            qCInfo(KIODRIVE_CFAPI_LOG) << "NOTIFY_FILE_OPEN_COMPLETION" << relative;
            Q_EMIT self->fileOpened(relative);
        },
        Qt::QueuedConnection);
}

void CALLBACK CloudFilesCallbacks::onNotifyFileCloseCompletion(CONST CF_CALLBACK_INFO *info,
                                                               CONST CF_CALLBACK_PARAMETERS *params)
{
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    if (!self) {
        return;
    }
    const QString relative = self->relativePathFromCallback(info);
    const bool deleted = (params->CloseCompletion.Flags & CF_CALLBACK_CLOSE_COMPLETION_FLAG_DELETED) != 0;
    QMetaObject::invokeMethod(
        self,
        [self, relative, deleted]() {
            qCInfo(KIODRIVE_CFAPI_LOG) << "NOTIFY_FILE_CLOSE_COMPLETION" << relative << "deleted=" << deleted;
            if (!deleted) {
                Q_EMIT self->fileClosed(relative);
            }
        },
        Qt::QueuedConnection);
}

void CALLBACK CloudFilesCallbacks::onNotifyDehydrate(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params)
{
    Q_UNUSED(params);
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    const QString relative = self ? self->relativePathFromCallback(info) : QString();

    CF_OPERATION_INFO opInfo{};
    opInfo.StructSize = sizeof(opInfo);
    opInfo.Type = CF_OPERATION_TYPE_ACK_DEHYDRATE;
    opInfo.ConnectionKey = info->ConnectionKey;
    opInfo.TransferKey = info->TransferKey;
    CF_OPERATION_PARAMETERS opParams{};
    opParams.ParamSize = CF_SIZE_OF_OP_PARAM(AckDehydrate);
    opParams.AckDehydrate.CompletionStatus = STATUS_SUCCESS;
    CfExecute(&opInfo, &opParams);

    if (self) {
        QMetaObject::invokeMethod(
            self,
            [self, relative]() {
                qCInfo(KIODRIVE_CFAPI_LOG) << "NOTIFY_DEHYDRATE" << relative;
                Q_EMIT self->dehydrateRequested(relative);
            },
            Qt::QueuedConnection);
    }
}

void CALLBACK CloudFilesCallbacks::onNotifyDehydrateCompletion(CONST CF_CALLBACK_INFO *info,
                                                               CONST CF_CALLBACK_PARAMETERS *params)
{
    Q_UNUSED(params);
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    if (!self) {
        return;
    }
    const QString relative = self->relativePathFromCallback(info);
    QMetaObject::invokeMethod(
        self,
        [self, relative]() {
            qCInfo(KIODRIVE_CFAPI_LOG) << "NOTIFY_DEHYDRATE_COMPLETION" << relative;
            Q_EMIT self->dehydrateCompleted(relative);
        },
        Qt::QueuedConnection);
}

void CALLBACK CloudFilesCallbacks::onNotifyRename(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params)
{
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));

    const auto flags = params->Rename.Flags;
    const bool isDirectory = (flags & CF_CALLBACK_RENAME_FLAG_IS_DIRECTORY) != 0;
    const bool sourceInScope = (flags & CF_CALLBACK_RENAME_FLAG_SOURCE_IN_SCOPE) != 0;
    const bool targetInScope = (flags & CF_CALLBACK_RENAME_FLAG_TARGET_IN_SCOPE) != 0;

    bool sourceParsedInScope = false;
    const QString source =
        self && info->NormalizedPath
            ? self->toSyncRelativePath(QString::fromWCharArray(info->NormalizedPath), &sourceParsedInScope)
            : QString();

    bool targetParsedInScope = false;
    QString target;
    if (self && params->Rename.TargetPath) {
        target = self->toSyncRelativePath(QString::fromWCharArray(params->Rename.TargetPath), &targetParsedInScope);
    }

    // ACK authorizes the local filesystem rename only — it does not sync remotely.
    CF_OPERATION_INFO opInfo{};
    opInfo.StructSize = sizeof(opInfo);
    opInfo.Type = CF_OPERATION_TYPE_ACK_RENAME;
    opInfo.ConnectionKey = info->ConnectionKey;
    opInfo.TransferKey = info->TransferKey;
    CF_OPERATION_PARAMETERS opParams{};
    opParams.ParamSize = CF_SIZE_OF_OP_PARAM(AckRename);
    opParams.AckRename.Flags = CF_OPERATION_ACK_RENAME_FLAG_NONE;
    opParams.AckRename.CompletionStatus = STATUS_SUCCESS;
    const HRESULT hr = CfExecute(&opInfo, &opParams);
    if (FAILED(hr)) {
        qCWarning(KIODRIVE_CFAPI_LOG) << "CfExecute ACK_RENAME failed" << Qt::hex << static_cast<quint32>(hr);
    }

    if (self) {
        QMetaObject::invokeMethod(
            self,
            [self, source, target, isDirectory, sourceInScope, targetInScope, sourceParsedInScope, targetParsedInScope]() {
                qCInfo(KIODRIVE_CFAPI_LOG) << "NOTIFY_RENAME" << source << "->" << target << "isDir" << isDirectory
                                           << "sourceInScope" << sourceInScope << "targetInScope" << targetInScope
                                           << "sourceParsedInScope" << sourceParsedInScope << "targetParsedInScope"
                                           << targetParsedInScope;
                Q_EMIT self->renameRequested(source, target, isDirectory, sourceInScope, targetInScope);
            },
            Qt::QueuedConnection);
    }
}

void CALLBACK CloudFilesCallbacks::onNotifyRenameCompletion(CONST CF_CALLBACK_INFO *info,
                                                            CONST CF_CALLBACK_PARAMETERS *params)
{
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    if (!self) {
        return;
    }

    // After completion: NormalizedPath is the final path; SourcePath is the old path.
    bool targetInScope = false;
    const QString target = self->toSyncRelativePath(
        info->NormalizedPath ? QString::fromWCharArray(info->NormalizedPath) : QString(), &targetInScope);

    bool sourceInScope = false;
    QString source;
    if (params->RenameCompletion.SourcePath) {
        source = self->toSyncRelativePath(QString::fromWCharArray(params->RenameCompletion.SourcePath),
                                          &sourceInScope);
    }

    // Completion has no IS_DIRECTORY flag; infer from the final path on disk when possible.
    const QString absTarget = QDir(self->m_syncRootPath).filePath(target);
    const bool isDirectory = targetInScope && QFileInfo(absTarget).isDir();

    QMetaObject::invokeMethod(
        self,
        [self, source, target, isDirectory, sourceInScope, targetInScope]() {
            qCInfo(KIODRIVE_CFAPI_LOG) << "NOTIFY_RENAME_COMPLETION" << source << "->" << target << "isDir"
                                       << isDirectory << "sourceInScope" << sourceInScope << "targetInScope"
                                       << targetInScope;
            Q_EMIT self->renameCompleted(source, target, isDirectory, sourceInScope, targetInScope);
        },
        Qt::QueuedConnection);
}

void CALLBACK CloudFilesCallbacks::onNotifyDelete(CONST CF_CALLBACK_INFO *info, CONST CF_CALLBACK_PARAMETERS *params)
{
    Q_UNUSED(params);
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    const QString relative = self ? self->relativePathFromCallback(info) : QString();

    CF_OPERATION_INFO opInfo{};
    opInfo.StructSize = sizeof(opInfo);
    opInfo.Type = CF_OPERATION_TYPE_ACK_DELETE;
    opInfo.ConnectionKey = info->ConnectionKey;
    opInfo.TransferKey = info->TransferKey;
    CF_OPERATION_PARAMETERS opParams{};
    opParams.ParamSize = CF_SIZE_OF_OP_PARAM(AckDelete);
    opParams.AckDelete.CompletionStatus = STATUS_SUCCESS;
    CfExecute(&opInfo, &opParams);

    if (self) {
        QMetaObject::invokeMethod(
            self,
            [self, relative]() {
                qCInfo(KIODRIVE_CFAPI_LOG) << "NOTIFY_DELETE" << relative;
                Q_EMIT self->deleteRequested(relative);
            },
            Qt::QueuedConnection);
    }
}

void CALLBACK CloudFilesCallbacks::onNotifyDeleteCompletion(CONST CF_CALLBACK_INFO *info,
                                                            CONST CF_CALLBACK_PARAMETERS *params)
{
    Q_UNUSED(params);
    auto *self = static_cast<CloudFilesCallbacks *>(const_cast<void *>(info->CallbackContext));
    if (!self) {
        return;
    }
    const QString relative = self->relativePathFromCallback(info);
    QMetaObject::invokeMethod(
        self,
        [self, relative]() {
            qCInfo(KIODRIVE_CFAPI_LOG) << "NOTIFY_DELETE_COMPLETION" << relative;
            Q_EMIT self->deleteCompleted(relative);
        },
        Qt::QueuedConnection);
}

#endif // Q_OS_WIN

} // namespace KIODrive

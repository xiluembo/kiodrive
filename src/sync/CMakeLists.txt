# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

add_library(kiodrive_sync STATIC
    transfer_manager.cpp
    local_change_monitor.cpp
    remote_reconciler.cpp
    conflict_resolver.cpp
    operation_journal.cpp
    sync_engine_core.cpp
)

target_link_libraries(kiodrive_sync
    PUBLIC
        kiodrive_common
        kiodrive_metadata
        kiodrive_mounts
        kiodrive_kio
        kiodrive_cloudfiles
        Qt6::Core
)

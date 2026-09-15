# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

add_executable(KIODrive.SyncEngine
    main.cpp
)

target_link_libraries(KIODrive.SyncEngine
    PRIVATE
        kiodrive_sync
        Qt6::Core
)

install(TARGETS KIODrive.SyncEngine ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

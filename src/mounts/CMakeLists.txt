# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

add_library(kiodrive_mounts STATIC
    path_router.cpp
    mount_conflict_detector.cpp
    mount_registry.cpp
)

target_link_libraries(kiodrive_mounts
    PUBLIC
        kiodrive_common
        kiodrive_metadata
)

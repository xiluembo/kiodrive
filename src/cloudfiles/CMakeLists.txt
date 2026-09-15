# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

add_library(kiodrive_cloudfiles STATIC
    sync_root_manager.cpp
    placeholder_manager.cpp
    cloud_files_callbacks.cpp
    hydration_manager.cpp
    pending_fetch_registry.cpp
)

target_link_libraries(kiodrive_cloudfiles
    PUBLIC
        kiodrive_common
        kiodrive_metadata
        kiodrive_mounts
        kiodrive_kio
        Qt6::Core
)

if(WIN32)
    # Cloud Files API import library from the Windows SDK.
    target_link_libraries(kiodrive_cloudfiles PUBLIC cldapi)
    target_compile_definitions(kiodrive_cloudfiles PUBLIC KIODRIVE_HAS_CFAPI=1)
endif()

# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

add_library(kiodrive_metadata STATIC
    metadata_store.cpp
    schema.cpp
)

target_link_libraries(kiodrive_metadata
    PUBLIC
        kiodrive_common
        Qt6::Sql
)

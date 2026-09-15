# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

add_library(kiodrive_common STATIC
    error_mapper.cpp
    file_identity.cpp
    logging.cpp
    path_utils.cpp
    types.cpp
)

target_include_directories(kiodrive_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(kiodrive_common
    PUBLIC
        Qt6::Core
        KF6::I18n
)

if(WIN32)
    target_link_libraries(kiodrive_common PUBLIC ole32 uuid)
endif()

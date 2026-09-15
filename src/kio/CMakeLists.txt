# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

add_library(kiodrive_kio STATIC
    protocol_registry.cpp
    capability_resolver.cpp
    credential_broker.cpp
    kio_backend.cpp
    mock_kio_backend.cpp
    name_mapper.cpp
)

target_link_libraries(kiodrive_kio
    PUBLIC
        kiodrive_common
        KF6::KIOCore
        Qt6::Network
)

if(WIN32)
    target_link_libraries(kiodrive_kio PUBLIC Advapi32)
endif()

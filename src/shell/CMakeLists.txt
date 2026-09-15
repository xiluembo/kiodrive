# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

add_executable(KIODrive.Shell
    main.cpp
    shell_proxy.cpp
)

target_link_libraries(KIODrive.Shell
    PRIVATE
        kiodrive_common
        Qt6::Core
)

if(WIN32)
    target_link_libraries(KIODrive.Shell PRIVATE ole32 oleaut32 uuid)
endif()

install(TARGETS KIODrive.Shell ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

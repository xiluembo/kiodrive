# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

add_executable(KIODrive.Gui
    main.cpp
    main_window.cpp
    tray_controller.cpp
)

target_link_libraries(KIODrive.Gui
    PRIVATE
        kiodrive_sync
        kiodrive_kio
        Qt6::Widgets
        KF6::I18n
        KF6::WidgetsAddons
        KF6::IconThemes
)

install(TARGETS KIODrive.Gui ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

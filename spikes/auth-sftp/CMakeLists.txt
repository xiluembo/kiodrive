# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

cmake_minimum_required(VERSION 3.16)
project(KIODriveAuthSpike LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(KF6 REQUIRED COMPONENTS KIO CoreAddons)

add_executable(kiodrive_spike_auth_sftp main.cpp)
target_link_libraries(kiodrive_spike_auth_sftp Qt6::Core KF6::KIOCore KF6::CoreAddons)

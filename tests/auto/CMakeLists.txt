# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# This file is part of KIODrive.
# See LICENSE / LICENSES/LGPL-3.0-or-later.txt.

ecm_add_tests(
    test_file_identity.cpp
    test_path_router.cpp
    test_mount_conflicts.cpp
    test_name_mapper.cpp
    test_mock_kio.cpp
    test_metadata_store.cpp
    LINK_LIBRARIES
        kiodrive_common
        kiodrive_metadata
        kiodrive_mounts
        kiodrive_kio
        Qt6::Test
)

ecm_add_test(
    test_pending_fetch_registry.cpp
    LINK_LIBRARIES
        kiodrive_cloudfiles
        Qt6::Test
)

ecm_add_test(
    test_transfer_data_contract.cpp
    LINK_LIBRARIES
        kiodrive_cloudfiles
        Qt6::Test
)

ecm_add_test(
    test_hydration_manager.cpp
    LINK_LIBRARIES
        kiodrive_cloudfiles
        kiodrive_kio
        kiodrive_metadata
        kiodrive_mounts
        Qt6::Test
)

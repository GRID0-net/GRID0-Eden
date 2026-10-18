# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(tests
    common/bit_field.cpp
    common/cityhash.cpp
    common/container_hash.cpp
    common/fibers.cpp
    common/host_memory.cpp
    common/param_package.cpp
    common/range_map.cpp
    common/ring_buffer.cpp
    common/scratch_buffer.cpp
    common/undefined_fix.cpp
    common/unique_function.cpp
    core/core_timing.cpp
    core/internal_network/network.cpp
    video_core/memory_tracker.cpp
    input_common/calibration_configuration_job.cpp
)

create_target_directory_groups(tests)

target_link_libraries(tests PRIVATE common core input_common video_core)
target_link_libraries(tests PRIVATE ${PLATFORM_LIBRARIES} Catch2::Catch2WithMain Threads::Threads)

add_test(NAME tests COMMAND tests)

add_executable(tests_grid0
    common/subpage_memory.cpp
    video_core/hdr_uploads.cpp
    core/private_server.cpp
    core/socket_address.cpp
    core/npln_startup.cpp
    core/message_batch.cpp
    core/eventfd.cpp
    ../core/hle/service/sockets/private_server.cpp
)
target_link_libraries(tests_grid0 PRIVATE common Catch2::Catch2WithMain Threads::Threads)
add_test(NAME grid0_foundations COMMAND tests_grid0)

# needed for vma
if (NOT MSVC)
    target_compile_options(tests PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-conversion>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-variable>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-parameter>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-missing-field-initializers>)
endif()

add_executable(tests_config_file
    common/config_file.cpp
    ../frontend_common/config_file.cpp
)
target_include_directories(tests_config_file PRIVATE ..)
target_link_libraries(tests_config_file PRIVATE Catch2::Catch2WithMain SimpleIni::SimpleIni)
add_test(NAME config_file_preservation COMMAND tests_config_file)

if (ENABLE_WEB_SERVICE)
    add_executable(tests_grid0_https
        core/private_server_https.cpp
        core/deferred_poll_waker.cpp
        core/message_batch_network.cpp
    )
    target_include_directories(tests_grid0_https PRIVATE ..)
    target_link_libraries(tests_grid0_https PRIVATE
        core Catch2::Catch2WithMain httplib::httplib OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    add_test(NAME grid0_https_trust COMMAND tests_grid0_https)
    set_tests_properties(grid0_https_trust PROPERTIES TIMEOUT 20)
endif()

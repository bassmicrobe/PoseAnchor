cmake_minimum_required(VERSION 3.24)

project(PoseAnchor VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

option(POSE_ANCHOR_BUILD_DRIVER "Build the Windows x64 SteamVR driver" ${WIN32})

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(pose_anchor_core STATIC
    src/core/tracker_filter.cpp
)
target_include_directories(pose_anchor_core PUBLIC include)
target_compile_features(pose_anchor_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(pose_anchor_core PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(pose_anchor_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(pose_anchor_tests tests/tracker_filter_tests.cpp)
    target_link_libraries(pose_anchor_tests PRIVATE pose_anchor_core)
    target_compile_features(pose_anchor_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(pose_anchor_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
    add_test(NAME pose_anchor_core COMMAND pose_anchor_tests)

    # A manual release-mode microbenchmark. It is intentionally not a CTest so
    # machine load cannot make correctness CI flaky.
    add_executable(pose_anchor_benchmark tests/filter_benchmark.cpp)
    target_link_libraries(pose_anchor_benchmark PRIVATE pose_anchor_core)
    target_compile_features(pose_anchor_benchmark PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(pose_anchor_benchmark PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
endif()

if(POSE_ANCHOR_BUILD_DRIVER)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The PoseAnchor SteamVR driver currently requires Windows x64")
    endif()
    if(NOT MSVC)
        message(FATAL_ERROR "The SteamVR-loadable driver requires the Visual Studio 2022 MSVC toolchain")
    endif()

    FetchContent_Declare(
        openvr
        GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
        GIT_TAG 0924064316de3effbcd1acf1e309182a2deb1c05
        GIT_SHALLOW FALSE
        SOURCE_SUBDIR _pose_anchor_headers_only
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies statically" FORCE)
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(openvr minhook)

    add_library(pose_anchor_openvr_headers INTERFACE)
    target_include_directories(pose_anchor_openvr_headers SYSTEM INTERFACE
        "${openvr_SOURCE_DIR}/headers"
    )
    add_library(OpenVR::Headers ALIAS pose_anchor_openvr_headers)
    add_library(pose_anchor_openvr_api UNKNOWN IMPORTED)
    set_target_properties(pose_anchor_openvr_api PROPERTIES
        IMPORTED_LOCATION "${openvr_SOURCE_DIR}/lib/win64/openvr_api.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${openvr_SOURCE_DIR}/headers"
    )
    add_library(OpenVR::API ALIAS pose_anchor_openvr_api)
    add_library(MinHook::MinHook ALIAS minhook)

    if(BUILD_TESTING)
        add_executable(pose_anchor_pose_adapter_tests
            tests/pose_adapter_tests.cpp
            src/driver/pose_adapter.cpp
        )
        target_include_directories(pose_anchor_pose_adapter_tests PRIVATE src/driver)
        target_link_libraries(pose_anchor_pose_adapter_tests PRIVATE
            pose_anchor_core
            OpenVR::Headers
        )
        target_compile_features(pose_anchor_pose_adapter_tests PRIVATE cxx_std_20)
        target_compile_options(pose_anchor_pose_adapter_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
        )
        add_test(NAME pose_anchor_pose_adapter COMMAND pose_anchor_pose_adapter_tests)
    endif()

    add_library(pose_anchor_driver SHARED
        src/driver/device_registry.cpp
        src/driver/driver_main.cpp
        src/driver/hooks.cpp
        src/driver/pose_adapter.cpp
        src/driver/server_provider.cpp
    )
    target_include_directories(pose_anchor_driver PRIVATE src/driver)
    target_link_libraries(pose_anchor_driver PRIVATE
        pose_anchor_core
        OpenVR::Headers
        OpenVR::API
        MinHook::MinHook
    )
    target_compile_features(pose_anchor_driver PRIVATE cxx_std_20)
    target_compile_definitions(pose_anchor_driver PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
        POSE_ANCHOR_VERSION="${PROJECT_VERSION}"
    )
    if(MSVC)
        target_compile_options(pose_anchor_driver PRIVATE
            /W4 /permissive- /EHsc /utf-8
            /wd4100 /wd4127
        )
    else()
        target_compile_options(pose_anchor_driver PRIVATE
            -Wall -Wextra -Wpedantic -Wno-unused-parameter
        )
    endif()
    set_target_properties(pose_anchor_driver PROPERTIES
        OUTPUT_NAME driver_pose_anchor
        PREFIX ""
    )

    set(POSE_ANCHOR_PACKAGE_DIR "${CMAKE_BINARY_DIR}/pose_anchor")
    set(POSE_ANCHOR_BINARY_DIR "${POSE_ANCHOR_PACKAGE_DIR}/bin/win64")
    set_target_properties(pose_anchor_driver PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${POSE_ANCHOR_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${POSE_ANCHOR_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${POSE_ANCHOR_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${POSE_ANCHOR_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${POSE_ANCHOR_BINARY_DIR}"
    )

    add_custom_command(TARGET pose_anchor_driver POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${POSE_ANCHOR_PACKAGE_DIR}/resources/settings"
                "${POSE_ANCHOR_PACKAGE_DIR}/licenses"
                "${POSE_ANCHOR_PACKAGE_DIR}/scripts"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/driver/pose_anchor/driver.vrdrivermanifest"
                "${POSE_ANCHOR_PACKAGE_DIR}/driver.vrdrivermanifest"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/driver/pose_anchor/resources/settings/default.vrsettings"
                "${POSE_ANCHOR_PACKAGE_DIR}/resources/settings/default.vrsettings"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/PACKAGE_README.md"
                "${POSE_ANCHOR_PACKAGE_DIR}/README.md"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/LICENSE"
                "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
                "${POSE_ANCHOR_PACKAGE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${openvr_SOURCE_DIR}/LICENSE"
                "${POSE_ANCHOR_PACKAGE_DIR}/licenses/OpenVR-LICENSE.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${minhook_SOURCE_DIR}/LICENSE.txt"
                "${POSE_ANCHOR_PACKAGE_DIR}/licenses/MinHook-LICENSE.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/licenses/OpenVR-SpaceCalibrator-LICENSE.txt"
                "${POSE_ANCHOR_PACKAGE_DIR}/licenses/OpenVR-SpaceCalibrator-LICENSE.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/licenses/Inno-Setup-LICENSE.txt"
                "${POSE_ANCHOR_PACKAGE_DIR}/licenses/Inno-Setup-LICENSE.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/scripts/install.ps1"
                "${CMAKE_SOURCE_DIR}/scripts/uninstall.ps1"
                "${CMAKE_SOURCE_DIR}/scripts/steamvr-path.ps1"
                "${POSE_ANCHOR_PACKAGE_DIR}/scripts"
        VERBATIM
    )
endif()

set(yakkai_scene_vendor_root "${CMAKE_CURRENT_SOURCE_DIR}/vendor/upstream_debug")
set(yakkai_scene_glslang_root "${yakkai_scene_vendor_root}/third_party/glslang")
set(yakkai_scene_qml_output_dir "${CMAKE_BINARY_DIR}/qml/io/team7/scene")
set(yakkai_scene_wallpaper_import_dir
    "${CMAKE_SOURCE_DIR}/wallpapers/io.team7.yakkai/contents/imports/io/team7/scene"
)

set(YAKKAI_SCENE_USE_VENDORED_BACKEND OFF)

if(YAKKAI_ENABLE_VENDORED_SCENE_BACKEND)
    find_package(Vulkan QUIET)
    find_package(Threads QUIET)
    find_library(yakkai_scene_lz4_library NAMES lz4)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
    endif()

    if(Vulkan_FOUND AND Threads_FOUND AND yakkai_scene_lz4_library)
        if(EXISTS "${yakkai_scene_glslang_root}/CMakeLists.txt")
            set(BUILD_EXTERNAL OFF CACHE BOOL "Disable glslang external dependency builds" FORCE)
            set(GLSLANG_TESTS OFF CACHE BOOL "Disable glslang tests" FORCE)
            set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "Disable glslang install rules" FORCE)
            set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "Disable glslang CLI binaries" FORCE)
            set(ALLOW_EXTERNAL_SPIRV_TOOLS ON CACHE BOOL "Allow system SPIRV-Tools if needed" FORCE)
            set(ENABLE_OPT OFF CACHE BOOL "Disable spirv-opt for the vendored glslang build" FORCE)

            if(NOT TARGET glslang::glslang)
                add_subdirectory(
                    "${yakkai_scene_glslang_root}"
                    "${CMAKE_CURRENT_BINARY_DIR}/third_party/glslang"
                )
            endif()
        endif()

        if(TARGET glslang::glslang AND TARGET glslang::SPIRV AND TARGET glslang::glslang-default-resource-limits)
        file(GLOB_RECURSE yakkai_scene_vendor_sources CONFIGURE_DEPENDS
            "${yakkai_scene_vendor_root}/qml_helper/*.cpp"
            "${yakkai_scene_vendor_root}/src/*.cpp"
        )
        file(GLOB_RECURSE yakkai_scene_vendor_headers CONFIGURE_DEPENDS
            "${yakkai_scene_vendor_root}/qml_helper/*.h"
            "${yakkai_scene_vendor_root}/qml_helper/*.hpp"
            "${yakkai_scene_vendor_root}/src/*.h"
            "${yakkai_scene_vendor_root}/src/*.hpp"
        )

        list(APPEND yakkai_scene_vendor_sources
            "${yakkai_scene_vendor_root}/third_party/glad/src/glad.c"
            "${yakkai_scene_vendor_root}/third_party/SPIRV-Reflect/spirv_reflect.c"
            "${yakkai_scene_vendor_root}/third_party/quickjs/quickjs.c"
            "${yakkai_scene_vendor_root}/third_party/quickjs/libregexp.c"
            "${yakkai_scene_vendor_root}/third_party/quickjs/libunicode.c"
            "${yakkai_scene_vendor_root}/third_party/quickjs/cutils.c"
            "${yakkai_scene_vendor_root}/third_party/quickjs/dtoa.c"
        )
        foreach(_qjs_src
            "${yakkai_scene_vendor_root}/third_party/quickjs/quickjs.c"
            "${yakkai_scene_vendor_root}/third_party/quickjs/libregexp.c"
            "${yakkai_scene_vendor_root}/third_party/quickjs/libunicode.c"
            "${yakkai_scene_vendor_root}/third_party/quickjs/cutils.c"
            "${yakkai_scene_vendor_root}/third_party/quickjs/dtoa.c"
        )
            set_source_files_properties("${_qjs_src}" PROPERTIES
                COMPILE_OPTIONS "-Wno-all;-Wno-extra;-fvisibility=hidden"
                COMPILE_DEFINITIONS "CONFIG_VERSION=\"2025-04-26\""
            )
        endforeach()
        set(yakkai_scene_vendor_include_dirs
            "${yakkai_scene_vendor_root}"
            "${yakkai_scene_vendor_root}/src/Audio/include"
            "${yakkai_scene_vendor_root}/src/Fs/include"
            "${yakkai_scene_vendor_root}/src/Looper/include"
            "${yakkai_scene_vendor_root}/src/Particle/include"
            "${yakkai_scene_vendor_root}/src/RenderGraph/include"
            "${yakkai_scene_vendor_root}/src/Scene/include"
            "${yakkai_scene_vendor_root}/src/Timer/include"
            "${yakkai_scene_vendor_root}/src/Utils/include"
            "${yakkai_scene_vendor_root}/src/Vulkan/include"
            "${yakkai_scene_vendor_root}/src/VulkanRender/include"
            "${yakkai_scene_vendor_root}/third_party"
            "${yakkai_scene_vendor_root}/third_party/Eigen"
            "${yakkai_scene_vendor_root}/third_party/glad/include"
            "${yakkai_scene_vendor_root}/third_party/glslang"
            "${yakkai_scene_vendor_root}/third_party/nlohmann/include"
            "${yakkai_scene_vendor_root}/third_party/SPIRV-Reflect"
            "${yakkai_scene_vendor_root}/third_party/SPIRV-Reflect/include"
            "${yakkai_scene_vendor_root}/third_party/quickjs"
        )

        foreach(yakkai_scene_vendor_header IN LISTS yakkai_scene_vendor_headers)
            get_filename_component(yakkai_scene_vendor_header_dir "${yakkai_scene_vendor_header}" DIRECTORY)
            list(APPEND yakkai_scene_vendor_include_dirs "${yakkai_scene_vendor_header_dir}")
        endforeach()
        list(REMOVE_DUPLICATES yakkai_scene_vendor_include_dirs)

        add_library(yakkai_scene_vendor STATIC
            ${yakkai_scene_vendor_sources}
        )

        set_target_properties(yakkai_scene_vendor PROPERTIES
            AUTOMOC ON
            POSITION_INDEPENDENT_CODE ON
        )

        target_compile_features(yakkai_scene_vendor PRIVATE cxx_std_20)

        target_include_directories(yakkai_scene_vendor
            PUBLIC
                ${yakkai_scene_vendor_include_dirs}
            PRIVATE
                "${yakkai_scene_vendor_root}/src/Audio"
        )

        target_link_libraries(yakkai_scene_vendor
            PUBLIC
                Qt6::Core
                Qt6::Gui
                Qt6::Quick
                Vulkan::Vulkan
                glslang::SPIRV
                glslang::glslang
                glslang::glslang-default-resource-limits
                Threads::Threads
                "${yakkai_scene_lz4_library}"
                ${CMAKE_DL_LIBS}
        )

        if(FFMPEG_FOUND)
            target_link_libraries(yakkai_scene_vendor PUBLIC PkgConfig::FFMPEG)
            target_compile_definitions(yakkai_scene_vendor PUBLIC YAKKAI_HAS_FFMPEG=1)
            message(STATUS "Paper scene backend: FFmpeg video texture support enabled")
        else()
            message(STATUS "Paper scene backend: FFmpeg not found, video textures disabled")
        endif()

        add_executable(yakkai_scene_policy_tests
            tests/test_scene_policies.cpp
        )

        target_compile_features(yakkai_scene_policy_tests PRIVATE cxx_std_20)

        target_include_directories(yakkai_scene_policy_tests
            PRIVATE
                ${yakkai_scene_vendor_include_dirs}
        )

        target_link_libraries(yakkai_scene_policy_tests
            PRIVATE
                yakkai_scene_vendor
        )

        set(YAKKAI_SCENE_USE_VENDORED_BACKEND ON)
        else()
            message(WARNING "Vendored Yakkai scene backend disabled because vendored glslang targets were not created.")
        endif()
    else()
        message(WARNING "Vendored Yakkai scene backend disabled because Vulkan, Threads, or liblz4 was not found.")
    endif()
endif()

qt_add_qml_module(yakkai_scene_backend
    URI io.team7.scene
    VERSION 1.0
    OUTPUT_DIRECTORY "${yakkai_scene_qml_output_dir}"
    SOURCES
        src/MprisMediaPayload.cpp
        src/MprisMediaPayload.hpp
        src/YakkaiMprisMediaSource.cpp
        src/YakkaiMprisMediaSource.hpp
        src/YakkaiSceneViewer.cpp
        src/YakkaiSceneViewer.hpp
)

target_link_libraries(yakkai_scene_backend
    PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

if(YAKKAI_SCENE_USE_VENDORED_BACKEND)
    target_link_libraries(yakkai_scene_backend
        PRIVATE
            yakkai_scene_vendor
    )
endif()

target_include_directories(yakkai_scene_backend
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(yakkai_scene_backend
    PRIVATE
        YAKKAI_SCENE_USE_VENDORED_BACKEND=$<BOOL:${YAKKAI_SCENE_USE_VENDORED_BACKEND}>
)

target_compile_features(yakkai_scene_backend PRIVATE cxx_std_20)

add_executable(yakkai_mpris_media_payload_tests
    src/MprisMediaPayload.cpp
    tests/test_mpris_media_payload.cpp
)

target_compile_features(yakkai_mpris_media_payload_tests PRIVATE cxx_std_20)

target_include_directories(yakkai_mpris_media_payload_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(yakkai_mpris_media_payload_tests
    PRIVATE
        Qt6::Core
)

add_test(NAME yakkai_mpris_media_payload_tests COMMAND yakkai_mpris_media_payload_tests)

add_executable(yakkai_mpris_media_source_tests
    src/MprisMediaPayload.cpp
    src/YakkaiMprisMediaSource.cpp
    tests/test_mpris_media_source.cpp
)

target_compile_features(yakkai_mpris_media_source_tests PRIVATE cxx_std_20)

target_compile_definitions(yakkai_mpris_media_source_tests
    PRIVATE
        YAKKAI_ENABLE_MPRIS_SOURCE_TEST_API=1
)

target_include_directories(yakkai_mpris_media_source_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(yakkai_mpris_media_source_tests
    PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Qml
        Qt6::Test
)

add_test(NAME yakkai_mpris_media_source_tests COMMAND yakkai_mpris_media_source_tests)

if(TARGET yakkai_scene_backendplugin)
    # The staged wallpaper package keeps the plugin and backing library together.
    set_target_properties(yakkai_scene_backendplugin PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        BUILD_RPATH "\$ORIGIN"
        INSTALL_RPATH "\$ORIGIN"
        INSTALL_RPATH_USE_LINK_PATH OFF
    )
endif()

add_custom_command(
    TARGET yakkai_scene_backend
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:yakkai_scene_backend>"
        "${yakkai_scene_qml_output_dir}/$<TARGET_FILE_NAME:yakkai_scene_backend>"
    COMMENT "Copying the Yakkai scene backend library into the QML module directory"
)

add_custom_command(
    TARGET yakkai_scene_backendplugin
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${yakkai_scene_wallpaper_import_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${yakkai_scene_qml_output_dir}/qmldir"
        "${yakkai_scene_wallpaper_import_dir}/qmldir"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${yakkai_scene_qml_output_dir}/yakkai_scene_backend.qmltypes"
        "${yakkai_scene_wallpaper_import_dir}/yakkai_scene_backend.qmltypes"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:yakkai_scene_backendplugin>"
        "${yakkai_scene_wallpaper_import_dir}/$<TARGET_FILE_NAME:yakkai_scene_backendplugin>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${yakkai_scene_qml_output_dir}/$<TARGET_FILE_NAME:yakkai_scene_backend>"
        "${yakkai_scene_wallpaper_import_dir}/$<TARGET_FILE_NAME:yakkai_scene_backend>"
    COMMENT "Staging the Yakkai scene QML module into the wallpaper package imports directory"
)

add_custom_target(yakkai_stage_wallyakkai_scene_import
    DEPENDS yakkai_scene_backendplugin
)

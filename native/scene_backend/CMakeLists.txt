set(paper_scene_vendor_root "${CMAKE_CURRENT_SOURCE_DIR}/vendor/upstream_debug")
set(paper_scene_glslang_root "${paper_scene_vendor_root}/third_party/glslang")
set(paper_scene_qml_output_dir "${CMAKE_BINARY_DIR}/qml/io/papercompany/scene")
set(paper_scene_wallpaper_import_dir
    "${CMAKE_SOURCE_DIR}/wallpapers/io.papercompany.gradient/contents/imports/io/papercompany/scene"
)

set(PAPER_SCENE_USE_VENDORED_BACKEND OFF)

if(PAPER_ENABLE_VENDORED_SCENE_BACKEND)
    find_package(Vulkan QUIET)
    find_package(Threads QUIET)
    find_library(paper_scene_lz4_library NAMES lz4)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
    endif()

    if(Vulkan_FOUND AND Threads_FOUND AND paper_scene_lz4_library)
        if(EXISTS "${paper_scene_glslang_root}/CMakeLists.txt")
            set(BUILD_EXTERNAL OFF CACHE BOOL "Disable glslang external dependency builds" FORCE)
            set(GLSLANG_TESTS OFF CACHE BOOL "Disable glslang tests" FORCE)
            set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "Disable glslang install rules" FORCE)
            set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "Disable glslang CLI binaries" FORCE)
            set(ALLOW_EXTERNAL_SPIRV_TOOLS ON CACHE BOOL "Allow system SPIRV-Tools if needed" FORCE)
            set(ENABLE_OPT OFF CACHE BOOL "Disable spirv-opt for the vendored glslang build" FORCE)

            if(NOT TARGET glslang::glslang)
                add_subdirectory(
                    "${paper_scene_glslang_root}"
                    "${CMAKE_CURRENT_BINARY_DIR}/third_party/glslang"
                )
            endif()
        endif()

        if(TARGET glslang::glslang AND TARGET glslang::SPIRV AND TARGET glslang::glslang-default-resource-limits)
        file(GLOB_RECURSE paper_scene_vendor_sources CONFIGURE_DEPENDS
            "${paper_scene_vendor_root}/qml_helper/*.cpp"
            "${paper_scene_vendor_root}/src/*.cpp"
        )
        file(GLOB_RECURSE paper_scene_vendor_headers CONFIGURE_DEPENDS
            "${paper_scene_vendor_root}/qml_helper/*.h"
            "${paper_scene_vendor_root}/qml_helper/*.hpp"
            "${paper_scene_vendor_root}/src/*.h"
            "${paper_scene_vendor_root}/src/*.hpp"
        )

        list(APPEND paper_scene_vendor_sources
            "${paper_scene_vendor_root}/third_party/glad/src/glad.c"
            "${paper_scene_vendor_root}/third_party/SPIRV-Reflect/spirv_reflect.c"
        )
        set(paper_scene_vendor_include_dirs
            "${paper_scene_vendor_root}"
            "${paper_scene_vendor_root}/src/Audio/include"
            "${paper_scene_vendor_root}/src/Fs/include"
            "${paper_scene_vendor_root}/src/Looper/include"
            "${paper_scene_vendor_root}/src/Particle/include"
            "${paper_scene_vendor_root}/src/RenderGraph/include"
            "${paper_scene_vendor_root}/src/Scene/include"
            "${paper_scene_vendor_root}/src/Timer/include"
            "${paper_scene_vendor_root}/src/Utils/include"
            "${paper_scene_vendor_root}/src/Vulkan/include"
            "${paper_scene_vendor_root}/src/VulkanRender/include"
            "${paper_scene_vendor_root}/third_party"
            "${paper_scene_vendor_root}/third_party/Eigen"
            "${paper_scene_vendor_root}/third_party/glad/include"
            "${paper_scene_vendor_root}/third_party/glslang"
            "${paper_scene_vendor_root}/third_party/nlohmann/include"
            "${paper_scene_vendor_root}/third_party/SPIRV-Reflect"
            "${paper_scene_vendor_root}/third_party/SPIRV-Reflect/include"
        )

        foreach(paper_scene_vendor_header IN LISTS paper_scene_vendor_headers)
            get_filename_component(paper_scene_vendor_header_dir "${paper_scene_vendor_header}" DIRECTORY)
            list(APPEND paper_scene_vendor_include_dirs "${paper_scene_vendor_header_dir}")
        endforeach()
        list(REMOVE_DUPLICATES paper_scene_vendor_include_dirs)

        add_library(papercompany_scene_vendor STATIC
            ${paper_scene_vendor_sources}
        )

        set_target_properties(papercompany_scene_vendor PROPERTIES
            AUTOMOC ON
            POSITION_INDEPENDENT_CODE ON
        )

        target_compile_features(papercompany_scene_vendor PRIVATE cxx_std_20)

        target_include_directories(papercompany_scene_vendor
            PUBLIC
                ${paper_scene_vendor_include_dirs}
            PRIVATE
                "${paper_scene_vendor_root}/src/Audio"
        )

        target_link_libraries(papercompany_scene_vendor
            PUBLIC
                Qt6::Core
                Qt6::Gui
                Qt6::Quick
                Vulkan::Vulkan
                glslang::SPIRV
                glslang::glslang
                glslang::glslang-default-resource-limits
                Threads::Threads
                "${paper_scene_lz4_library}"
                ${CMAKE_DL_LIBS}
        )

        if(FFMPEG_FOUND)
            target_link_libraries(papercompany_scene_vendor PUBLIC PkgConfig::FFMPEG)
            target_compile_definitions(papercompany_scene_vendor PUBLIC PAPER_HAS_FFMPEG=1)
            message(STATUS "Paper scene backend: FFmpeg video texture support enabled")
        else()
            message(STATUS "Paper scene backend: FFmpeg not found, video textures disabled")
        endif()

        set(PAPER_SCENE_USE_VENDORED_BACKEND ON)
        else()
            message(WARNING "Vendored Paper Company scene backend disabled because vendored glslang targets were not created.")
        endif()
    else()
        message(WARNING "Vendored Paper Company scene backend disabled because Vulkan, Threads, or liblz4 was not found.")
    endif()
endif()

qt_add_qml_module(papercompany_scene_backend
    URI io.papercompany.scene
    VERSION 1.0
    OUTPUT_DIRECTORY "${paper_scene_qml_output_dir}"
    SOURCES
        src/PaperSceneViewer.cpp
        src/PaperSceneViewer.hpp
)

target_link_libraries(papercompany_scene_backend
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

if(PAPER_SCENE_USE_VENDORED_BACKEND)
    target_link_libraries(papercompany_scene_backend
        PRIVATE
            papercompany_scene_vendor
    )
endif()

target_include_directories(papercompany_scene_backend
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(papercompany_scene_backend
    PRIVATE
        PAPER_SCENE_USE_VENDORED_BACKEND=$<BOOL:${PAPER_SCENE_USE_VENDORED_BACKEND}>
)

target_compile_features(papercompany_scene_backend PRIVATE cxx_std_20)

if(TARGET papercompany_scene_backendplugin)
    # The staged wallpaper package keeps the plugin and backing library together.
    set_target_properties(papercompany_scene_backendplugin PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        BUILD_RPATH "\$ORIGIN"
        INSTALL_RPATH "\$ORIGIN"
        INSTALL_RPATH_USE_LINK_PATH OFF
    )
endif()

add_custom_command(
    TARGET papercompany_scene_backend
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:papercompany_scene_backend>"
        "${paper_scene_qml_output_dir}/$<TARGET_FILE_NAME:papercompany_scene_backend>"
    COMMENT "Copying the Paper Company scene backend library into the QML module directory"
)

add_custom_command(
    TARGET papercompany_scene_backendplugin
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${paper_scene_wallpaper_import_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${paper_scene_qml_output_dir}/qmldir"
        "${paper_scene_wallpaper_import_dir}/qmldir"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${paper_scene_qml_output_dir}/papercompany_scene_backend.qmltypes"
        "${paper_scene_wallpaper_import_dir}/papercompany_scene_backend.qmltypes"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:papercompany_scene_backendplugin>"
        "${paper_scene_wallpaper_import_dir}/$<TARGET_FILE_NAME:papercompany_scene_backendplugin>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${paper_scene_qml_output_dir}/$<TARGET_FILE_NAME:papercompany_scene_backend>"
        "${paper_scene_wallpaper_import_dir}/$<TARGET_FILE_NAME:papercompany_scene_backend>"
    COMMENT "Staging the Paper Company scene QML module into the wallpaper package imports directory"
)

add_custom_target(papercompany_stage_wallpaper_scene_import
    DEPENDS papercompany_scene_backendplugin
)

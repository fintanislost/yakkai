qt_add_executable(paper_scene_harness
    src/main.cpp
)

target_link_libraries(paper_scene_harness
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

target_compile_features(paper_scene_harness PRIVATE cxx_std_20)

target_compile_definitions(paper_scene_harness
    PRIVATE
        PAPER_SCENE_HARNESS_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
        PAPER_SCENE_HARNESS_BUILD_QML_IMPORT_DIR="${CMAKE_BINARY_DIR}/qml"
)

if(TARGET papercompany_scene_backend)
    add_dependencies(paper_scene_harness papercompany_scene_backend)
endif()

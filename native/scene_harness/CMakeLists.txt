qt_add_executable(yakkai_scene_harness
    src/main.cpp
    src/PuppetSimulationOption.cpp
    src/ScenePropertiesOption.cpp
)

target_link_libraries(yakkai_scene_harness
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::WebEngineQuick
)

target_compile_features(yakkai_scene_harness PRIVATE cxx_std_20)

target_compile_definitions(yakkai_scene_harness
    PRIVATE
        YAKKAI_SCENE_HARNESS_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
        YAKKAI_SCENE_HARNESS_BUILD_QML_IMPORT_DIR="${CMAKE_BINARY_DIR}/qml"
)

if(TARGET yakkai_scene_backend)
    add_dependencies(yakkai_scene_harness yakkai_scene_backend)
endif()

if(TARGET yakkai_scene_backendplugin)
    add_dependencies(yakkai_scene_harness yakkai_scene_backendplugin)
endif()

add_executable(yakkai_scene_harness_tests
    tests/test_capture_gate.cpp
    src/PuppetSimulationOption.cpp
    src/ScenePropertiesOption.cpp
)

target_include_directories(yakkai_scene_harness_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(yakkai_scene_harness_tests
    PRIVATE
        Qt6::Core
)

target_compile_features(yakkai_scene_harness_tests PRIVATE cxx_std_20)

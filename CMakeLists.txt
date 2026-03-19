cmake_minimum_required(VERSION 3.24)

project(PaperCompany LANGUAGES C CXX)

option(PAPER_BUILD_SCENE_BACKEND "Build the Paper Company scene backend scaffold" ON)
option(PAPER_BUILD_SCENE_HARNESS "Build the standalone scene harness app" ON)
option(
    PAPER_ENABLE_VENDORED_SCENE_BACKEND
    "Build the repo-owned native Wallpaper Engine scene backend when dependencies are available"
    ON
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick)

qt_standard_project_setup(REQUIRES 6.6)

add_subdirectory(native)

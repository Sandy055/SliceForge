cmake_minimum_required(VERSION 3.16)
project(SliceForge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── CLI Executable (no Qt dependency) ──
add_executable(sliceforge src/main_cli.cpp)
target_include_directories(sliceforge PRIVATE src)
target_link_libraries(sliceforge PRIVATE pthread)

# ── Tests ──
add_executable(sliceforge_tests tests/test_all.cpp)
target_include_directories(sliceforge_tests PRIVATE src)
target_link_libraries(sliceforge_tests PRIVATE pthread)

# ── Qt GUI (optional — only if Qt6 is found) ──
find_package(Qt6 QUIET COMPONENTS Quick Widgets)
if(Qt6_FOUND)
    message(STATUS "Qt6 found — building GUI")
    add_executable(sliceforge_gui src/ui/main.cpp)
    target_include_directories(sliceforge_gui PRIVATE src)
    target_link_libraries(sliceforge_gui PRIVATE Qt6::Quick Qt6::Widgets pthread)
else()
    message(STATUS "Qt6 not found — skipping GUI build (CLI still available)")
endif()

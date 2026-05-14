cmake_minimum_required(VERSION 3.16)
project(checker VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(CHECKER_BUILD_EXAMPLES "Build checker examples" ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

add_library(checker_testlib INTERFACE)
target_include_directories(checker_testlib INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/shared>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/checker>)
set_target_properties(checker_testlib PROPERTIES EXPORT_NAME testlib)
add_library(checker::testlib ALIAS checker_testlib)

add_library(checker_imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(checker_imgui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/checker/imgui>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/checker/imgui/backends>)
target_link_libraries(checker_imgui PUBLIC glfw OpenGL::GL)
target_compile_features(checker_imgui PUBLIC cxx_std_17)
set_target_properties(checker_imgui PROPERTIES EXPORT_NAME imgui)
add_library(checker::imgui ALIAS checker_imgui)

add_library(checker
    src/core/case_io.cpp
    src/core/script_parser.cpp
    src/core/stress.cpp
    src/core/subprocess.cpp
    src/dispatch.cpp
    src/gui/case_detail.cpp
    src/gui/gui_build.cpp
    src/gui/gui_canvas.cpp
    src/gui/gui_cases.cpp
    src/gui/gui_main.cpp
    src/gui/gui_stress.cpp)
target_include_directories(checker PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(checker PUBLIC checker_imgui)
target_compile_features(checker PUBLIC cxx_std_17)
add_library(checker::checker ALIAS checker)

include(cmake/checker_helpers.cmake)

if(CHECKER_BUILD_EXAMPLES)
    add_subdirectory(examples/sort_uniq)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(checker_unit tests/unit/mock_stress_test.cpp)
    target_link_libraries(checker_unit PRIVATE checker::checker)
    add_test(NAME checker_unit COMMAND checker_unit)
endif()

install(TARGETS checker checker_testlib checker_imgui
    EXPORT checker-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY third_party/imgui/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/checker/imgui
    FILES_MATCHING PATTERN "*.h")
install(FILES shared/testlib.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/checker)
install(FILES cmake/checker_helpers.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/checker)
install(EXPORT checker-targets
    NAMESPACE checker::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/checker)

configure_package_config_file(
    cmake/checker-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/checker-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/checker)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/checker-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/checker-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/checker-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/checker)

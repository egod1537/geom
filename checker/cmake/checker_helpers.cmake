# Usage:
#   checker_add_stress_test(my_algo_stress
#       GENERATORS gen_basic gen_edge
#       SOURCES    src/stress_test.cpp
#       GENERATOR_DIR src
#       SCRIPT     tests/gen.script
#       CASES_DIR  tests/cases
#       LINK       my_algo::my_algo)
function(checker_add_stress_test target)
    cmake_parse_arguments(GS "" "SCRIPT;CASES_DIR;GENERATOR_DIR" "GENERATORS;SOURCES;LINK" ${ARGN})

    if(NOT GS_GENERATORS)
        message(FATAL_ERROR "checker_add_stress_test(${target}) requires GENERATORS")
    endif()
    if(NOT GS_SOURCES)
        message(FATAL_ERROR "checker_add_stress_test(${target}) requires SOURCES")
    endif()

    if(NOT GS_SCRIPT)
        set(GS_SCRIPT "tests/gen.script")
    endif()
    if(NOT GS_CASES_DIR)
        set(GS_CASES_DIR "tests/cases")
    endif()
    if(NOT GS_GENERATOR_DIR)
        set(GS_GENERATOR_DIR "tests/gen")
    endif()

    get_filename_component(script_abs "${GS_SCRIPT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(cases_abs "${GS_CASES_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(generator_dir_abs "${GS_GENERATOR_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

    set(gen_paths_dir "${CMAKE_CURRENT_BINARY_DIR}/checker/${target}")
    set(gen_paths_content "#pragma once\n")
    string(APPEND gen_paths_content "#include <string_view>\n\n")
    string(APPEND gen_paths_content "namespace checker::generated {\n")
    string(APPEND gen_paths_content "inline const char* gen_path(std::string_view name) {\n")
    string(APPEND gen_paths_content "    using namespace std::string_view_literals;\n")

    foreach(gen IN LISTS GS_GENERATORS)
        set(gen_tgt "${target}_${gen}")
        add_executable("${gen_tgt}" "${generator_dir_abs}/${gen}.cpp")
        target_link_libraries("${gen_tgt}" PRIVATE checker::testlib)
        string(APPEND gen_paths_content
            "    if (name == \"${gen}\"sv) return \"$<TARGET_FILE:${gen_tgt}>\";\n")
    endforeach()

    string(APPEND gen_paths_content "    return nullptr;\n")
    string(APPEND gen_paths_content "}\n")
    string(APPEND gen_paths_content "inline const char* gen_source_path(std::string_view name) {\n")
    string(APPEND gen_paths_content "    using namespace std::string_view_literals;\n")

    foreach(gen IN LISTS GS_GENERATORS)
        string(APPEND gen_paths_content
            "    if (name == \"${gen}\"sv) return \"${generator_dir_abs}/${gen}.cpp\";\n")
    endforeach()

    string(APPEND gen_paths_content "    return nullptr;\n")
    string(APPEND gen_paths_content "}\n")
    string(APPEND gen_paths_content "inline constexpr const char* script_path = \"${script_abs}\";\n")
    string(APPEND gen_paths_content "inline constexpr const char* cases_dir = \"${cases_abs}\";\n")
    string(APPEND gen_paths_content "} // namespace checker::generated\n")

    file(GENERATE OUTPUT "${gen_paths_dir}/gen_paths.h"
         CONTENT "${gen_paths_content}")

    add_executable("${target}" ${GS_SOURCES})
    target_link_libraries("${target}" PRIVATE
        checker::checker
        ${GS_LINK})
    target_include_directories("${target}" PRIVATE "${gen_paths_dir}")

    foreach(gen IN LISTS GS_GENERATORS)
        add_dependencies("${target}" "${target}_${gen}")
    endforeach()
endfunction()

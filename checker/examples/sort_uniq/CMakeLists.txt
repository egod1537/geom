add_library(sort_uniq INTERFACE)
target_include_directories(sort_uniq INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_library(sort_uniq::sort_uniq ALIAS sort_uniq)

checker_add_stress_test(sort_uniq_stress
    GENERATORS gen_random
    SOURCES    tests/stress_test.cpp
    LINK       sort_uniq::sort_uniq)

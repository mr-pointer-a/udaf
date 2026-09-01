# tests/proptest - RapidCheck 属性测试
# 自动发现边界条件，弥补手写单测的盲点。
# 启用方式：cmake -DUDAF_ENABLE_PROPTEST=ON

enable_testing()

find_package(rapidcheck CONFIG REQUIRED)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

function(add_proptest name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE
        rapidcheck
        rapidcheck_gtest
        GTest::gtest
        GTest::gtest_main
        udaf::udaf_core
        udaf::udaf_crypto
        udaf::udaf_platform_fs
    )
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "proptest")
endfunction()

add_proptest(test_proptest_result test_result_props.cpp)
add_proptest(test_proptest_buffer test_buffer_props.cpp)
add_proptest(test_proptest_psk     test_psk_props.cpp)

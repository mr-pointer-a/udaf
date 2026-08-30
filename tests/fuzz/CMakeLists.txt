# tests/fuzz 模糊测试
add_executable(udaf_fuzz fuzz_main.cpp)
target_link_libraries(udaf_fuzz
    PRIVATE
        udaf::udaf_crypto
        udaf::udaf_ability_a_discovery)
target_compile_features(udaf_fuzz PRIVATE cxx_std_20)

add_test(
    NAME udaf_fuzz_smoke
    COMMAND udaf_fuzz 1000
)
set_tests_properties(udaf_fuzz_smoke PROPERTIES
    LABELS "fuzz"
    TIMEOUT 60
)
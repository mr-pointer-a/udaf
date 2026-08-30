add_executable(udaf_e2e udaf_e2e.cpp)
target_link_libraries(udaf_e2e PRIVATE
    udaf::udaf_sdk
    udaf::udaf_audit
    udaf::udaf_ability_a_registry)
target_compile_features(udaf_e2e PRIVATE cxx_std_20)
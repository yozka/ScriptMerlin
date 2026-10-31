add_executable(mln_tests test_engine.cpp)
target_link_libraries(mln_tests PRIVATE ScriptMerlin GTest::gtest_main)
target_compile_definitions(mln_tests PRIVATE "MLN_TESTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
if(SME_ENABLE_ASSERTS STREQUAL "ON")
    target_compile_definitions(mln_tests PRIVATE SME_TEST_ASSERTS_ENABLED=1)
elseif(SME_ENABLE_ASSERTS STREQUAL "OFF")
    target_compile_definitions(mln_tests PRIVATE SME_TEST_ASSERTS_ENABLED=0)
else()
    target_compile_definitions(mln_tests PRIVATE
        $<$<CONFIG:Debug>:SME_TEST_ASSERTS_ENABLED=1>
        $<$<NOT:$<CONFIG:Debug>>:SME_TEST_ASSERTS_ENABLED=0>
    )
endif()
add_test(NAME mln_tests COMMAND mln_tests)

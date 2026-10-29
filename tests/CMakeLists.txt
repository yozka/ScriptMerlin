add_executable(mln_tests test_engine.cpp)
target_link_libraries(mln_tests PRIVATE ScriptMerlin GTest::gtest_main)
target_compile_definitions(mln_tests PRIVATE "MLN_TESTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
add_test(NAME mln_tests COMMAND mln_tests)

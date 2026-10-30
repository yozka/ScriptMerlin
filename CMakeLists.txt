cmake_minimum_required(VERSION 3.20)

project(ScriptMerlin VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SME_EXAMPLE "Пример использования" ON)
option(SME_TEST "Юнит-тесты" ON)

set(mln_headers
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sme/sme.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sme/sme_engine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sme/sme_error.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sme/sme_filesystem.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sme/sme_host.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sme/sme_logging.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sme/sme_value.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sme/smelib_default.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sme/smelib_gs.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sme/smelib_math.h
)

set(mln_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_value.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_lexer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_preprocess.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_compiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_machine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/smelib_filesystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/smelib_default.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/smelib_gs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/smelib_math.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_ast.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_compiler.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_lexer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_machine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_parser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_preprocess.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sme_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/smelib_filesystem.h
)

add_library(ScriptMerlin ${mln_sources} ${mln_headers})
add_library(ScriptMerlin::ScriptMerlin ALIAS ScriptMerlin)

target_include_directories(ScriptMerlin
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(ScriptMerlin PUBLIC cxx_std_20)
set_target_properties(ScriptMerlin PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    EXPORT_NAME ScriptMerlin
)

if(MSVC)
    target_compile_options(ScriptMerlin PRIVATE /W4)
else()
    target_compile_options(ScriptMerlin PRIVATE -Wall -Wextra -Wpedantic)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${mln_sources} ${mln_headers})

install(TARGETS ScriptMerlin
    EXPORT ScriptMerlinTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ScriptMerlinTargets
    FILE ScriptMerlinTargets.cmake
    NAMESPACE ScriptMerlin::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ScriptMerlin
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ScriptMerlinConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ScriptMerlinConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ScriptMerlin
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ScriptMerlinConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ScriptMerlinConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ScriptMerlinConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ScriptMerlin
)

if(SME_EXAMPLE)
    add_subdirectory(console)
endif()

if(SME_TEST)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    add_subdirectory(tests)
endif()

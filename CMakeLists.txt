# CMakeLists.txt for Lua C++ project
# Modern CMake build system for C++23 Lua implementation
cmake_minimum_required(VERSION 3.20)

project(Lua
    VERSION 5.5.0
    DESCRIPTION "Lua programming language - C++ implementation"
    LANGUAGES CXX
)

# C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(LUA_BUILD_TESTS "Build with test mode enabled" ON)
option(LUA_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LUA_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(LUA_ENABLE_LTO "Enable Link Time Optimization" OFF)
option(LUA_BUILD_SHARED "Build shared library in addition to static" OFF)

# Platform detection
if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

# Compiler warning flags (common to GCC and Clang)
set(WARNING_FLAGS
    -Werror
    -Wfatal-errors
    -Wextra
    -Wshadow
    -Wundef
    -Wwrite-strings
    -Wredundant-decls
    -Wdisabled-optimization
    -Wdouble-promotion
    -Wmissing-declarations
    -Wconversion
    -Wstrict-overflow=2
)

# GCC-specific warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND WARNING_FLAGS
        -Wlogical-op
        -Wno-aggressive-loop-optimizations
        -Wno-invalid-offsetof
    )
endif()

# Optimization and platform flags
set(OPTIMIZE_FLAGS
    -O2
    -fno-stack-protector
    -fno-common
    -march=native
)

# Test mode definitions
if(LUA_BUILD_TESTS)
    add_compile_definitions(
        LUA_USER_H="ltests.h"
        LUAI_ASSERT
    )
endif()

# Platform definitions
if(LINUX)
    add_compile_definitions(LUA_USE_LINUX)
endif()

# Sanitizer options
if(LUA_ENABLE_ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

if(LUA_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# Link Time Optimization
if(LUA_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Source files for Lua library
set(LUA_CORE_SOURCES
    lapi.cpp
    lcode.cpp
    lctype.cpp
    ldebug.cpp
    ldo.cpp
    ldump.cpp
    lfunc.cpp
    lgc.cpp
    llex.cpp
    lmem.cpp
    lobject.cpp
    lopcodes.cpp
    lparser.cpp
    lstate.cpp
    lstring.cpp
    ltable.cpp
    ltm.cpp
    lundump.cpp
    lvm.cpp
    lzio.cpp
)

# Test source (only in test mode)
if(LUA_BUILD_TESTS)
    list(APPEND LUA_CORE_SOURCES ltests.cpp)
endif()

set(LUA_AUX_SOURCES
    lauxlib.cpp
)

set(LUA_LIB_SOURCES
    lbaselib.cpp
    ldblib.cpp
    liolib.cpp
    lmathlib.cpp
    loslib.cpp
    ltablib.cpp
    lstrlib.cpp
    lutf8lib.cpp
    loadlib.cpp
    lcorolib.cpp
    linit.cpp
)

# Public headers
set(LUA_PUBLIC_HEADERS
    lua.h
    luaconf.h
    lualib.h
    lauxlib.h
)

# Static library target
add_library(liblua_static STATIC
    ${LUA_CORE_SOURCES}
    ${LUA_AUX_SOURCES}
    ${LUA_LIB_SOURCES}
)

# Set output name to liblua.a (remove double lib prefix)
set_target_properties(liblua_static PROPERTIES
    OUTPUT_NAME lua
    PUBLIC_HEADER "${LUA_PUBLIC_HEADERS}"
)

target_compile_options(liblua_static PRIVATE
    -Wall
    ${WARNING_FLAGS}
    ${OPTIMIZE_FLAGS}
)

target_link_libraries(liblua_static PUBLIC
    m      # Math library
    dl     # Dynamic linking library (for loadlib)
)

# Export symbols for dynamic loading
target_link_options(liblua_static PUBLIC
    -Wl,-E
)

target_include_directories(liblua_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/lua>
)

# Optional shared library
if(LUA_BUILD_SHARED)
    add_library(liblua_shared SHARED
        ${LUA_CORE_SOURCES}
        ${LUA_AUX_SOURCES}
        ${LUA_LIB_SOURCES}
    )

    set_target_properties(liblua_shared PROPERTIES
        OUTPUT_NAME lua
        VERSION ${PROJECT_VERSION}
        SOVERSION 5.5
        PUBLIC_HEADER "${LUA_PUBLIC_HEADERS}"
    )

    target_compile_options(liblua_shared PRIVATE
        -Wall
        ${WARNING_FLAGS}
        ${OPTIMIZE_FLAGS}
    )

    target_link_libraries(liblua_shared PUBLIC m dl)
    target_link_options(liblua_shared PUBLIC -Wl,-E)

    target_include_directories(liblua_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include/lua>
    )
endif()

# Lua interpreter executable
add_executable(lua
    lua.cpp
)

target_link_libraries(lua PRIVATE
    liblua_static
)

target_compile_options(lua PRIVATE
    -Wall
    ${WARNING_FLAGS}
    ${OPTIMIZE_FLAGS}
)

# Install targets
include(GNUInstallDirs)

install(TARGETS liblua_static lua
    EXPORT LuaTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lua
)

if(LUA_BUILD_SHARED)
    install(TARGETS liblua_shared
        EXPORT LuaTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lua
    )
endif()

# Export targets for use by other CMake projects
install(EXPORT LuaTargets
    FILE LuaTargets.cmake
    NAMESPACE Lua::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lua
)

# Create package config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LuaConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/LuaConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lua
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LuaConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/LuaConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/LuaConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lua
)

# Testing with CTest
if(LUA_BUILD_TESTS)
    enable_testing()

    add_test(
        NAME LuaTestSuite
        COMMAND lua all.lua
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/testes
    )

    set_tests_properties(LuaTestSuite PROPERTIES
        TIMEOUT 120
        PASS_REGULAR_EXPRESSION "final OK"
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Lua ${PROJECT_VERSION} Configuration Summary:")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Test Mode: ${LUA_BUILD_TESTS}")
message(STATUS "  Build Shared Library: ${LUA_BUILD_SHARED}")
message(STATUS "  AddressSanitizer: ${LUA_ENABLE_ASAN}")
message(STATUS "  UBSanitizer: ${LUA_ENABLE_UBSAN}")
message(STATUS "  LTO: ${LUA_ENABLE_LTO}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")

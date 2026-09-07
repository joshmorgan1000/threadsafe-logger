cmake_minimum_required(VERSION 3.16)
project(logging
    VERSION 0.1.0
    DESCRIPTION "Consolidated C++ logging utilities"
    LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# --- Options ---------------------------------------------------------------
option(LOGGING_BUILD_TESTS "Build the logging library's tests" OFF)
set(LOGGING_LOG_LEVEL 0 CACHE STRING "Compile-time minimum log level (0=trace, 5=raw)")
set_property(CACHE LOGGING_LOG_LEVEL PROPERTY STRINGS 0 1 2 3 4 5)
if(NOT "${LOGGING_LOG_LEVEL}" MATCHES "^[0-5]$")
    message(FATAL_ERROR "LOGGING_LOG_LEVEL must be an integer from 0 through 5")
endif()
# --- Library ---------------------------------------------------------------
add_library(logging STATIC
    src/logging.cpp
)
add_library(logging::logging ALIAS logging)
target_include_directories(logging
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(logging PUBLIC cxx_std_20)
target_compile_definitions(logging
    PUBLIC
        LOGGING_LOG_LEVEL=${LOGGING_LOG_LEVEL}
)
set_target_properties(logging PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_EXTENSIONS OFF
)
if(APPLE OR UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(logging PUBLIC Threads::Threads)
endif()
# --- Install ---------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS logging
    EXPORT loggingTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT loggingTargets
    FILE loggingTargets.cmake
    NAMESPACE logging::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/logging
)
# --- Tests -----------------------------------------------------------------
if(LOGGING_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

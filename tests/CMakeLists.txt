file(GLOB LOGGING_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
foreach(LOGGING_TEST_SOURCE IN LISTS LOGGING_TEST_SOURCES)
    get_filename_component(LOGGING_TEST_NAME "${LOGGING_TEST_SOURCE}" NAME_WE)
    add_executable(${LOGGING_TEST_NAME} "${LOGGING_TEST_SOURCE}")
    target_link_libraries(${LOGGING_TEST_NAME} PRIVATE logging::logging)
    add_test(NAME ${LOGGING_TEST_NAME} COMMAND ${LOGGING_TEST_NAME})
endforeach()
add_executable(demo_logo
    "${CMAKE_CURRENT_SOURCE_DIR}/demo_logo.cpp")
target_link_libraries(demo_logo PRIVATE logging::logging)

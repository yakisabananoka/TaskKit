# Search for test source files
file(GLOB TEST_SOURCES "*.cpp")

# Exit with warning if no source files are found
if(NOT TEST_SOURCES)
    message(STATUS "No test source files found in tests/. Skipping test build.")
    return()
endif()

# Test framework selection (Google Test or Catch2)
# Option 1: Google Test (using FetchContent)
option(USE_GTEST "Use Google Test framework" ON)

if(USE_GTEST)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )
    # Windows configuration
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Create test executable
    add_executable(TaskKit_tests ${TEST_SOURCES})

    # Link library and test framework
    target_link_libraries(TaskKit_tests
        PRIVATE
            TaskKit
            GTest::gtest_main
    )

    # Register tests
    include(GoogleTest)
    gtest_discover_tests(TaskKit_tests)

else()
    # Option 2: Use Catch2
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.0
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(TaskKit_tests ${TEST_SOURCES})

    target_link_libraries(TaskKit_tests
        PRIVATE
            TaskKit
            Catch2::Catch2WithMain
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(TaskKit_tests)
endif()

# Set output directory
set_target_properties(TaskKit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)

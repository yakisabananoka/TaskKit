# Create interface library for header-only library
add_library(TaskKit INTERFACE)

# Include directory settings
target_include_directories(TaskKit INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Require C++ standard
target_compile_features(TaskKit INTERFACE cxx_std_20)

# Install configuration (optional)
install(TARGETS TaskKit
    EXPORT TaskKitTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/TaskKit
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

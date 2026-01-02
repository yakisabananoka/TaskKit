# Search for sample program source files
file(GLOB SAMPLE_SOURCES "*.cpp")

# Exit with warning if no source files are found
if(NOT SAMPLE_SOURCES)
    message(STATUS "No sample source files found in sample/. Skipping sample build.")
    return()
endif()

# Create executable for each sample file
foreach(sample_file ${SAMPLE_SOURCES})
    # Get the file name without extension
    get_filename_component(sample_name ${sample_file} NAME_WE)

    # Create executable
    add_executable(${sample_name} ${sample_file})

    # Link TaskKit library
    target_link_libraries(${sample_name} PRIVATE TaskKit)

    # Set output directory
    set_target_properties(${sample_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/samples
    )
endforeach()

cmake_minimum_required(VERSION 3.16)

# Project definition
project(VirtualMemorySystem
        VERSION 1.0.0
        DESCRIPTION "User-mode virtual memory management system"
        LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
 set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler-specific options
if(MSVC)
 # MSVC specific flags
 add_compile_options(/W3 /Zi /TC)
 add_link_options(/DEBUG)
else()
 # GCC/Clang flags
 add_compile_options(-Wall -Wextra -g)
endif()

# Include directories
include_directories(include)

# Source files
set(SOURCES
        src/util.c
        src/disk.c
        src/init.c
        src/pages.c
        src/vm.c
)

# Header files (for IDE support)
set(HEADERS
        include/util.h
        include/disk.h
        include/init.h
        include/pages.h
        include/vm.h
        include/macros.h
)

# Create the executable
add_executable(vm ${SOURCES} ${HEADERS})

# Link required libraries
if(WIN32)
 target_link_libraries(vm advapi32)

 # Add onecore.lib if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE is enabled
 # Uncomment the next line if you enable that feature
 # target_link_libraries(vm onecore)
endif()

# Set target properties
set_target_properties(vm PROPERTIES
        OUTPUT_NAME "vm"
        DEBUG_POSTFIX "_debug"
)

# Add compile definitions for debug builds
target_compile_definitions(vm PRIVATE
        $<$<CONFIG:Debug>:DBG=1>
        $<$<CONFIG:Release>:DBG=0>
)

# Optional: Add support for multiple VA to same page
option(SUPPORT_MULTIPLE_VA "Enable support for multiple VA to same page" OFF)
if(SUPPORT_MULTIPLE_VA)
 target_compile_definitions(vm PRIVATE SUPPORT_MULTIPLE_VA_TO_SAME_PAGE=1)
 if(WIN32)
  target_link_libraries(vm onecore)
 endif()
else()
 target_compile_definitions(vm PRIVATE SUPPORT_MULTIPLE_VA_TO_SAME_PAGE=0)
endif()

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Support Multiple VA: ${SUPPORT_MULTIPLE_VA}")

# Custom targets for convenience
add_custom_target(run
        COMMAND vm
        DEPENDS vm
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running the virtual memory system"
)

# Install target (optional)
install(TARGETS vm
        RUNTIME DESTINATION bin
)

# Package configuration (optional)
set(CPACK_PACKAGE_NAME "VirtualMemorySystem")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
include(CPack)
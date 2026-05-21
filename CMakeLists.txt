cmake_minimum_required(VERSION 3.16)

# Project definition
project(VirtualMemorySystem
        VERSION 1.0.0
        DESCRIPTION "User-mode virtual memory management system"
        LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to Release for performance unless overridden (single-config generators only)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
 set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
 set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Compiler-specific options.
# Note: CMake already injects sensible per-config defaults for MSVC
# (Debug: /MDd /Zi /Ob0 /Od /RTC1 ; Release: /MD /O2 /Ob2 /DNDEBUG).
# We only ADD extras here so we don't fight the defaults.
if(MSVC)
 # Always-on
 add_compile_options(/W3 /TC)
 # Release-only extras for max throughput
 add_compile_options("$<$<CONFIG:Release>:/Oi>")  # intrinsics
 add_compile_options("$<$<CONFIG:Release>:/Ot>")  # favor speed
 add_compile_options("$<$<CONFIG:Release>:/GL>")  # whole-program opt (pairs with /LTCG)
 # Linker
 add_link_options("$<$<CONFIG:Debug>:/DEBUG>")
 add_link_options("$<$<CONFIG:Release>:/LTCG>")
 add_link_options("$<$<CONFIG:Release>:/OPT:REF>")
 add_link_options("$<$<CONFIG:Release>:/OPT:ICF>")
 add_link_options("$<$<CONFIG:RelWithDebInfo>:/DEBUG>")
 add_link_options("$<$<CONFIG:RelWithDebInfo>:/OPT:REF>")
 add_link_options("$<$<CONFIG:RelWithDebInfo>:/OPT:ICF>")
else()
 add_compile_options(-Wall -Wextra)
 add_compile_options("$<$<CONFIG:Debug>:-g>")
 add_compile_options("$<$<CONFIG:Debug>:-O0>")
 add_compile_options("$<$<CONFIG:Release>:-O3>")
 add_compile_options("$<$<CONFIG:Release>:-DNDEBUG>")
 add_compile_options("$<$<CONFIG:RelWithDebInfo>:-O2>")
 add_compile_options("$<$<CONFIG:RelWithDebInfo>:-g>")
 add_compile_options("$<$<CONFIG:RelWithDebInfo>:-DNDEBUG>")
endif()

# Link-time optimization for non-Debug builds when the toolchain supports it
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)

# Include directories
include_directories(include)

# Source files
set(SOURCES
        src/disk/disk.c
        src/initialization/init.c
        src/initialization/init_threads.c
        src/threads/trimmer_thread.c
        src/threads/user_thread.c
        src/threads/writer_thread.c
        src/threads/zero_thread.c
        src/utils/page_utils.c
        src/utils/pte_utils.c
        src/utils/thread_utils.c
        src/vm/vm.c
        src/threads/statistics.c
        src/utils/statistics_utils.c
        src/utils/random_utils.c
        src/debug.c
        src/threads/user_free.c
        src/threads/scheduler_thread.c

)

# Header files (for IDE support)
set(HEADERS
        include/disk/disk.h
        include/initialization/init.h
        include/initialization/init_threads.h
        include/threads/trimmer_thread.h
        include/threads/user_thread.h
        include/threads/writer_thread.h
        include/threads/zero_thread.h
        include/utils/page_utils.h
        include/utils/pte_utils.h
        include/utils/thread_utils.h
        include/variables/globals.h
        include/variables/macros.h
        include/variables/structures.h
        include/vm.h
        include/threads/statistics.h
        include/utils/statistics_utils.h
        include/utils/random_utils.h
        include/threads/user_free.h
        src/threads/ager_thread.c
        src/utils/pte_regions_utils.c
        include/utils/pte_regions_utils.h
        include/threads/ager_thread.h
        include/threads/scheduler_thread.h
)

# Create the executable
add_executable(vm ${SOURCES} ${HEADERS})

if(ipo_ok)
 set_property(TARGET vm PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
 set_property(TARGET vm PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()

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
# Package configuration (optional)
set(CPACK_PACKAGE_NAME "VirtualMemorySystem")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
include(CPack)
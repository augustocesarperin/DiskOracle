cmake_minimum_required(VERSION 3.10)
project(DiskOracle C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

# Add /FS flag for MSVC to serialize PDB writes globally
if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /FS")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FS") # Though not a C++ project, good practice if mixed
endif()

set(EXECUTABLE_NAME diskoracle)

include_directories(include)


set(MAIN_SOURCES
    src/main.c
   
    src/smart.c
    src/surface.c
    src/info.c
    src/predict.c
    src/report.c
    src/logging.c
    src/smart_ata.c
    src/nvme_export.c
    src/nvme_alerts.c
    src/nvme_benchmark.c
    src/nvme_cache.c
)

# Conditionally add the correct PAL implementation for the main executable
if(WIN32)
    list(APPEND MAIN_SOURCES src/pal_windows.c)
elseif(UNIX AND NOT APPLE)
    list(APPEND MAIN_SOURCES src/pal_linux.c)
elseif(APPLE)
    list(APPEND MAIN_SOURCES src/pal_macos.c)
else()
    message(FATAL_ERROR "Unsupported platform. No PAL implementation available for main executable.")
endif()

add_executable(${EXECUTABLE_NAME} ${MAIN_SOURCES})

# Platform-specific libraries for the main executable
if(WIN32)
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE ws2_32 setupapi)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE pthread udev)
elseif(APPLE)
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE pthread CoreFoundation IOKit)
endif()

# Installation
install(TARGETS ${EXECUTABLE_NAME} DESTINATION bin)

# Test executables
add_executable(test_smart tests/test_smart.c src/smart.c src/smart_ata.c)

# Add platform-specific PAL source and link libraries for test_smart
if(WIN32)
    target_sources(test_smart PRIVATE src/pal_windows.c)
    target_link_libraries(test_smart PRIVATE ws2_32 setupapi)
elseif(UNIX AND NOT APPLE)
    target_sources(test_smart PRIVATE src/pal_linux.c)
    target_link_libraries(test_smart PRIVATE pthread udev)
elseif(APPLE)
    target_sources(test_smart PRIVATE src/pal_macos.c)
    target_link_libraries(test_smart PRIVATE pthread CoreFoundation IOKit)
else()
    message(WARNING "test_smart may not compile or link correctly on this platform due to missing PAL or specific libraries.")
endif()

# For test_surface, we need to conditionally add the correct PAL as well.
# It also needs smart.c because the PAL functions (e.g. pal_get_smart in pal_windows.c) call smart_read.
set(TEST_SURFACE_SOURCES tests/test_surface.c src/surface.c src/smart.c)
if(WIN32)
    list(APPEND TEST_SURFACE_SOURCES src/pal_windows.c)
    # No specific libs needed for pal_windows.c for surface test typically
elseif(UNIX AND NOT APPLE) 
    list(APPEND TEST_SURFACE_SOURCES src/pal_linux.c)
    # target_link_libraries(test_surface udev) # if pal_linux.c for surface needs it
elseif(APPLE) 
    list(APPEND TEST_SURFACE_SOURCES src/pal_macos.c)
    target_link_libraries(test_surface CoreFoundation IOKit) # pal_macos.c needs these
else()
    message(WARNING "test_surface may not compile correctly on this platform due to missing PAL.")
endif()
add_executable(test_surface ${TEST_SURFACE_SOURCES})

# Conditional linking for test_surface if its PAL part needs it (example for Apple shown above if uncommented)
if(APPLE AND ("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin"))
    target_link_libraries(test_surface PRIVATE CoreFoundation IOKit)
endif()

add_executable(test_predict tests/test_predict.c src/predict.c)

# TODO: Consider refactoring PAL and library additions into a function or macro
# if more test executables require this pattern to reduce duplication.

# Test executable for report.c
add_executable(run_report_tests
    tests/test_report.c
    tests/mock_pal.c
    src/report.c
    src/smart.c
    src/predict.c
    src/logging.c
)

# Add UNIT_TESTING definition for report.c to use mocks
target_compile_definitions(run_report_tests PRIVATE UNIT_TESTING)

# Include directories for tests
target_include_directories(run_report_tests PRIVATE
    include
    src
    tests # For acutest.h
)

# Link libraries if needed (e.g., for logging or other dependencies)
# target_link_libraries(run_report_tests PRIVATE some_library)
add_test(NAME RunReportTests COMMAND run_report_tests)

# Ensure the main application is built before tests if tests depend on it
# add_dependencies(run_report_tests hd-check)

# Installation (optional)
# install(TARGETS hd-check DESTINATION bin)

# Test suite for pal_windows.c NVMe parsing logic (Windows only)
if(WIN32)
    add_executable(run_pal_windows_tests tests/test_pal_windows.c src/pal_windows.c)
    # If pal_windows.c (and thus the test) needs common utility functions from other .c files from src/ 
    # that are not pulled in via headers alone, they might need to be added here.
    # For example, if pal_windows.c used a utility from a hypothetical `src/common_utils.c`,
    # that would need to be part of the compilation for this test if not part of hdguardian_lib.
    # For now, assuming pal_windows.c + its direct includes are sufficient for this parsing function.
    add_test(NAME RunPalWindowsTests COMMAND run_pal_windows_tests)
endif()

# Test suite for pal_linux.c NVMe parsing logic (Linux only)
if(UNIX AND NOT APPLE)
    add_executable(run_pal_linux_tests tests/test_pal_linux.c)
    # tests/test_pal_linux.c includes src/pal_linux.c directly, so it doesn't need to be listed as a source here.
    # It might need target_link_libraries if pal_linux.c itself has external dependencies not covered by its own includes
    # when being compiled as part of this test executable (e.g. udev, pthread if used directly in the parser or its immediate helpers).
    # For now, assuming the direct include and its stubs for non-Linux parts are sufficient for compilation.
    # However, pal_linux.c does use <endian.h>, which is standard. And <linux/nvme_ioctl.h> for the real struct definition.
    # When compiling tests/test_pal_linux.c (which includes src/pal_linux.c) on a Linux system,
    # all necessary headers should be available.
    # Link libraries that pal_linux.c depends on, like udev or pthread, if the tested code paths require them.
    # The parser itself is pure C, but other parts of pal_linux.c might pull these in.
    # For NVME_IOCTL_ADMIN_CMD and related, no specific library linking is usually needed beyond libc.
    add_test(NAME RunPalLinuxTests COMMAND run_pal_linux_tests)
endif()


if(APPLE)
    add_executable(run_pal_macos_tests tests/test_pal_macos.c)
    target_link_libraries(run_pal_macos_tests PRIVATE "-framework CoreFoundation" "-framework IOKit")
    add_test(NAME RunPalMacOSTests COMMAND run_pal_macos_tests)
endif()

# Test for smart.c S.M.A.R.T. logic (both ATA and NVMe data injection)
add_executable(run_smart_tests
    tests/test_smart.c
    src/smart.c
    src/smart_ata.c
    src/predict.c # predict.c is a dependency for smart_get_health_summary
    tests/mock_pal.c # For mocked pal_get_smart
)
target_include_directories(run_smart_tests PRIVATE include tests) # For smart.h, pal.h (mocked), acutest.h
target_compile_definitions(run_smart_tests PRIVATE UNIT_TESTING) # To enable mock usage in smart.c if it checks
add_test(NAME RunSmartLogicTests COMMAND run_smart_tests)

# Test suite for surface.c scan logic (using mocked pal_get_device_size)
add_executable(run_surface_tests
    tests/test_surface.c
    src/surface.c
    tests/mock_pal_surface.c
)
# surface.c uses standard C and OS-specific file I/O calls.
# The only PAL function it uses, pal_get_device_size, is mocked.
# No specific platform PAL sources (pal_windows.c, etc.) or special libraries
# should be needed for this test executable beyond standard system libraries.
add_test(NAME RunSurfaceScanTests COMMAND run_surface_tests)

# Test suite for info.c display logic
add_executable(run_info_tests
    tests/test_info.c
    src/info.c                 
    tests/mock_pal.c           
    src/smart.c                # Dependency for info.c (smart_read, smart_get_health_summary)
    src/predict.c              # Dependency for smart.c (smart_get_health_summary -> predict_failure)
    src/logging.c              # Dependency for info.c or its deps
)

target_include_directories(run_info_tests PRIVATE
    include                    
    src                        # If any src files include others from src directly (e.g. for internal helpers)
    tests                      # For acutest.h, mock_pal.h
)

target_compile_definitions(run_info_tests PRIVATE UNIT_TESTING)
add_test(NAME RunInfoDisplayTests COMMAND run_info_tests)

# Test suite for predict.c logic (data injection)
add_executable(run_predict_tests
    tests/test_predict.c
    src/predict.c
    src/smart.c # Dependency for predict_failure -> smart_get_health_summary (if used)
    tests/mock_pal.c # For pal_get_smart used by smart_read
)
target_include_directories(run_predict_tests PRIVATE include tests) # For predict.h, smart.h, pal.h (mocked), acutest.h
target_compile_definitions(run_predict_tests PRIVATE UNIT_TESTING)
add_test(NAME RunPredictLogicTests COMMAND run_predict_tests)

# If other src files (smart.c, logging.c) also get mocking macros, their test targets would need UNIT_TESTING too.
# For test_smart which compiles src/smart.c:
target_compile_definitions(test_smart PRIVATE UNIT_TESTING)
# For test_surface which compiles src/surface.c and src/smart.c:
target_compile_definitions(test_surface PRIVATE UNIT_TESTING)

if(WIN32)
    target_compile_definitions(run_pal_windows_tests PRIVATE UNIT_TESTING)
endif()
if(UNIX AND NOT APPLE)
    target_compile_definitions(run_pal_linux_tests PRIVATE UNIT_TESTING)
endif()
if(APPLE)
    target_compile_definitions(run_pal_macos_tests PRIVATE UNIT_TESTING)
endif()

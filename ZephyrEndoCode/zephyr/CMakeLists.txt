cmake_minimum_required(VERSION 3.13.1)
set(DTC_OVERLAY_FILE "nucleo_f446re.overlay")

# next line saves intermediate files for debugging
set(CMAKE_C_FLAGS "-save-temps=obj")
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(ZephyrSampleCode)

# Add include directory so common.h can be found
target_include_directories(app PRIVATE ../include)

FILE(GLOB app_sources ../src/*.c)
# Exclude backup files and alternate mains (avoid duplicate main())
list(FILTER app_sources EXCLUDE REGEX ".*\\.backup$")
list(FILTER app_sources EXCLUDE REGEX ".*\\.good$")
list(FILTER app_sources EXCLUDE REGEX ".*main_python\\.c$")
target_sources(app PRIVATE ${app_sources})
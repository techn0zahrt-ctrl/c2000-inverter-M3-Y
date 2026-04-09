# C2000 build platform

option(FLASH_BUILD "Build for flash (default is RAM)" OFF)

set(CMAKE_CXX_STANDARD
    14
    CACHE INTERNAL "")

set(CMAKE_C_STANDARD
    11
    CACHE INTERNAL "")

# Set all of our common platform specific compiler options, defines and include
# paths
add_library(platform_options INTERFACE)
if(FLASH_BUILD)
    target_compile_definitions(platform_options INTERFACE C2000 CPU1 _FLASH)
else()
    target_compile_definitions(platform_options INTERFACE C2000 CPU1)
endif()
target_include_directories(platform_options INTERFACE
    "${CMAKE_SOURCE_DIR}/platform/c2000/include"
    "${CMAKE_SOURCE_DIR}/platform/c2000/inverter_drivers")
# Debug vs Release flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building in DEBUG mode")

    target_compile_options(
      platform_options
      INTERFACE -ml
                -mt
                --cla_support=cla1
                --float_support=fpu32
                --fp_mode=relaxed
                --printf_support=full
                --emit_warnings_as_errors
                --tmu_support=tmu0
                --vcu_support=vcu2
                --advice:performance=all
                --symdebug:dwarf
                --opt_level=off)

else()
    message(STATUS "Building in RELEASE mode")

    target_compile_options(
      platform_options
      INTERFACE -ml
                -mt
                --cla_support=cla1
                --float_support=fpu32
                --fp_mode=relaxed
                --printf_support=full
                --emit_warnings_as_errors
                --tmu_support=tmu0
                --vcu_support=vcu2
                --advice:performance=all
                --symdebug:dwarf
                -O4)
endif()
if(FLASH_BUILD)
    set(LINKER_CMD "${CMAKE_SOURCE_DIR}/common-build-options/c2000/2837xD_FLASH_lnk_cpu1.cmd")
else()
    set(LINKER_CMD "${CMAKE_SOURCE_DIR}/common-build-options/c2000/2837xD_RAM_lnk_cpu1.cmd")
endif()

target_link_options(
  platform_options
  INTERFACE
  -ml
  -mt
  --cla_support=cla1
  --float_support=fpu32
  --tmu_support=tmu0
  --vcu_support=vcu2
  --stack_size=0x800
  --heap_size=0x1000
  --warn_sections
  --reread_libs
  --diag_wrap=off
  --display_error_number
  --rom_model
  -m "${CMAKE_PROJECT_NAME}.map"     # Creates <projectname>.map in build dir
  --mapfile_contents=all             # Most detailed output (sections, symbols, etc.)
  "${LINKER_CMD}")

# Special library to add last
add_library(link_last INTERFACE)
target_link_libraries(link_last INTERFACE -llibc.a)

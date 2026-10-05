# Pull in our platform specific options
add_subdirectory(${PLATFORM})

# Set all of our common compiler options, defines and include paths
add_library(global_options INTERFACE)
set(CONTROL "SINE" CACHE STRING "Motor control algorithm: SINE or FOC")
set_property(CACHE CONTROL PROPERTY STRINGS SINE FOC)
if(NOT CONTROL MATCHES "^(SINE|FOC)$")
    message(FATAL_ERROR "CONTROL must be SINE or FOC")
endif()
target_compile_definitions(global_options INTERFACE "CONTROL=CTRL_${CONTROL}")
target_compile_definitions(global_options INTERFACE "CTRL_SINE=1")
target_compile_definitions(global_options INTERFACE "CTRL_FOC=0")
target_link_libraries(global_options INTERFACE platform_options)

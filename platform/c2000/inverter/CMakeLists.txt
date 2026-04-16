# Main inverter application
add_executable(inverter inverter_main.cpp json_print.cpp canlogger.cpp)
target_link_libraries(inverter global_options portablesine libopeninv
                      inverter_drivers device_support link_last)
target_link_options(inverter PRIVATE --map_file=/tmp/inverter.map)
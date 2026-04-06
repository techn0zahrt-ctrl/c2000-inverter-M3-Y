# Build a test CAN bus program that runs on the model 3/Y inverter
# works
add_executable(cantest cantest.cpp)
target_link_libraries(cantest global_options device_support link_last)

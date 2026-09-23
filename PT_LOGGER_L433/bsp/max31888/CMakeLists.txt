cmake_minimum_required(VERSION 3.10)
project(max31888
        VERSION 0.2.0
        LANGUAGES C CXX ASM)
add_library(max31888 STATIC EXCLUDE_FROM_ALL)

target_sources(max31888 
        PRIVATE 
        MAX31888.cpp)

target_include_directories(max31888 
    INTERFACE 
        .
)
target_link_libraries(max31888 PUBLIC mbed-core-flags onewire) # This is needed so that the library can access Mbed and OneWire includes



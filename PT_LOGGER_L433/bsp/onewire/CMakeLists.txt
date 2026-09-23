cmake_minimum_required(VERSION 3.10)
project(onewire
        VERSION 0.0.1
        LANGUAGES C CXX ASM)
add_library(onewire STATIC EXCLUDE_FROM_ALL)

target_sources(onewire 
        PRIVATE 
        OneWire.cpp)

target_include_directories(onewire
    INTERFACE 
        .
)
target_link_libraries(onewire PUBLIC mbed-core-flags) # This is needed so that the library can access Mbed includes



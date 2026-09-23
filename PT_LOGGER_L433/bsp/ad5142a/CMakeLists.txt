cmake_minimum_required(VERSION 3.10)
project(ad5142a
        VERSION 0.0.1
        LANGUAGES C CXX ASM)

add_library(ad5142a STATIC EXCLUDE_FROM_ALL)
target_sources(ad5142a 
        PRIVATE 
        AD5142A.cpp)
target_include_directories(ad5142a INTERFACE .)

target_link_libraries(ad5142a PUBLIC mbed-core-flags) # This is needed so that the library can access Mbed includes



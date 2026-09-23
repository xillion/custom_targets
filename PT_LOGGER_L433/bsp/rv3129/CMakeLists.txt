cmake_minimum_required(VERSION 3.10)
project(rv3129
        VERSION 0.1.4
        LANGUAGES C CXX ASM)
add_library(rv3129 STATIC EXCLUDE_FROM_ALL)

target_sources(rv3129
        PRIVATE 
        RV3129.cpp)

target_include_directories(rv3129
    INTERFACE 
        .
)
target_link_libraries(rv3129 PUBLIC mbed-core-flags) # mbed-core-flags is needed so that the library can access Mbed includes



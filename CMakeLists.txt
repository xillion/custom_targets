# PT_LOGGER_L433 --------------------------------------------------------------
if("${MBED_TARGET}" STREQUAL "PT_LOGGER_L433")
	mbed_disable_mcu_target_file(mbed-stm32l433xc system_clock.c)
	# mbed_disable_mcu_target_file(mbed-stm32l4 analogin_device.c)
	# mbed_disable_mcu_target_file(mbed-stm USBDevice.cpp)
	# mbed_disable_mcu_target_file(mbed-stm analogin_api.c)
endif()

add_library(mbed-pt-logger-l433 INTERFACE)
target_sources(mbed-pt-logger-l433 
	INTERFACE 
		PT_LOGGER_L433/PeripheralPins.c
		PT_LOGGER_L433/system_clock_new.c
		PT_LOGGER_L433/boot_init.cpp
		# PT_LOGGER_L433/analogin_device.c
		# PT_LOGGER_L433/analogin_api.c
		# PT_LOGGER_L433/USBDevice.cpp
)

target_include_directories(mbed-pt-logger-l433 INTERFACE PT_LOGGER_L433)
target_link_libraries(mbed-pt-logger-l433 INTERFACE mbed-stm32l433xc)


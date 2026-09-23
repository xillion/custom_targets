# In this file you can specify the upload method configuration for your custom target(s).
# See here for the list of parameters that can be set:
# https://github.com/mbed-ce/mbed-os/wiki/Upload-Methods

if(MBED_TARGET STREQUAL "PT_LOGGER_L433")

	# Config options for JLINK
	# -------------------------------------------------------------

	set(JLINK_UPLOAD_ENABLED TRUE)
	set(JLINK_CPU_NAME STM32L433CC)
	set(JLINK_CLOCK_SPEED 1000)
	set(JLINK_UPLOAD_INTERFACE SWD)

	# Enable STM32Cube upload method
	set(STM32CUBE_UPLOAD_ENABLED TRUE)
	set(STM32CUBE_CONNECT_COMMAND -c port=SWD reset=HWrst)
	set(STM32CUBE_GDBSERVER_ARGS --swd)

	# Enable Mbed upload method
	set(MBED_UPLOAD_ENABLED TRUE)

	# Config options for stlink
	# -------------------------------------------------------------
	set(STLINK_UPLOAD_ENABLED TRUE)
	set(STLINK_LOAD_ADDRESS 0x8000000)
	set(STLINK_ARGS --connect-under-reset)

	# Config options for PYOCD
	# -------------------------------------------------------------
	set(PYOCD_UPLOAD_ENABLED TRUE)
	set(PYOCD_TARGET_NAME stm32l433cc)
	set(PYOCD_CLOCK_SPEED 1000k)

	# Config options for OPENOCD
	# -------------------------------------------------------------
	set(OPENOCD_UPLOAD_ENABLED TRUE)
	set(OPENOCD_CHIP_CONFIG_COMMANDS
		-f ${OpenOCD_SCRIPT_DIR}/board/st_nucleo_l4.cfg)

	# Default is STM32CUBE
	set(UPLOAD_METHOD_DEFAULT STM32CUBE)

endif()

if(MBED_TARGET STREQUAL "NUCLEO_L476RG")

	# Enable STM32Cube upload method
	set(STM32CUBE_UPLOAD_ENABLED TRUE)
	set(STM32CUBE_CONNECT_COMMAND -c port=SWD reset=HWrst)
	set(STM32CUBE_GDBSERVER_ARGS --swd)

	# Enable Mbed upload method
	set(MBED_UPLOAD_ENABLED TRUE)

	# Config options for stlink
	# -------------------------------------------------------------
	set(STLINK_UPLOAD_ENABLED TRUE)
	set(STLINK_LOAD_ADDRESS 0x8000000)
	set(STLINK_ARGS --connect-under-reset)

	# Config options for PYOCD
	# -------------------------------------------------------------
	set(PYOCD_UPLOAD_ENABLED TRUE)
	set(PYOCD_TARGET_NAME stm32l433cc)
	set(PYOCD_CLOCK_SPEED 1000k)

	# Config options for OPENOCD
	# -------------------------------------------------------------
	set(OPENOCD_UPLOAD_ENABLED TRUE)
	set(OPENOCD_CHIP_CONFIG_COMMANDS
		-f ${OpenOCD_SCRIPT_DIR}/board/st_nucleo_l4.cfg)

	# Default is STM32CUBE
	set(UPLOAD_METHOD_DEFAULT STM32CUBE)

endif()


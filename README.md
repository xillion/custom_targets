# custom_targets

Custom mbed-os CE target definition for PT_LOGGER_L433 (STM32L433), shared as
a git submodule between `pt-logger` and `pt-logger-test` so both firmware
variants stay on the same target definition, linker script, clock config and
on-board component drivers instead of drifting apart.

```
custom_targets/
├── custom_targets.json5          # target definition (memory banks, inherits MCU_STM32L433xC)
├── CMakeLists.txt                 # mbed-pt-logger-l433 interface target
├── CustomUploadMethods.cmake      # JLink/STM32Cube/STLink/PyOCD/OpenOCD upload config
├── upload_method_cfg/
└── PT_LOGGER_L433/
    ├── PinNames.h, PeripheralPins.c, *.ioc
    ├── stm32l4_dual_flash_bank.ld  # linker script (dual-bank flash, custom boot stack size)
    ├── system_clock.c              # stock mbed-os clock init (disabled; kept for reference)
    ├── system_clock_new.c          # active SetSysClock() override (USE_PLL_MSI + dynamic reboot-to-freq)
    ├── boot_context.h              # BootContext struct shared with SystemInit()/handle_reboot()
    └── bsp/                        # on-board component drivers
        ├── ad5142a/                # digital potentiometer
        ├── max31888/               # 1-Wire temperature sensor
        ├── onewire/                # 1-Wire bus driver (max31888 dependency)
        └── rv3129/                 # I2C RTC
```

Consumed as a submodule at `custom_targets/` by the firmware repos; each `bsp/<lib>`
is added via `add_subdirectory(custom_targets/PT_LOGGER_L433/bsp/<lib>)` in the
firmware's top-level `CMakeLists.txt`, after `mbed-os` (the drivers link against
`mbed-core-flags`, which mbed-os defines).

#pragma once

// MCU/board bring-up: DFU-загрузчик, контекст перезагрузки (boot_ctx),
// SystemInit(). Общий для pt-logger и pt-logger-test.

#include <cstdint>
#include "boot_context.h"

extern std::uint32_t boot_reason;

void reboot_into_dfu();
void check_reset_reason();
bool is_boot_context_valid();
void reboot_with_context(uint32_t reason, uint32_t fw_addr = 0, uint32_t fw_size = 0);

#pragma once

#include <stdint.h>

#define BOOT_CTX_MAGIC 0xAA55AA55
#define REASON_NORMAL  0x00000000
#define REASON_DFU     0xDECAFBAD
#define REASON_NEW_CLK 0xCAFE0001 // Флаг, что мы загружаемся с кастомными частотами


// Упакованная структура настроек клока (8 байт)
struct ClockSettingsRAM {
    uint8_t msi_range;
    uint8_t pll_m;
    uint8_t pll_n;
    uint8_t pll_r;
    uint16_t ahb_div;
    uint8_t apb1_div;
    uint8_t apb2_div;
};

// Главная структура контекста перезагрузки
struct BootContext {
    uint32_t boot_reason;      
    uint32_t fw_update_addr;   
    uint32_t fw_update_size;   
    uint32_t fw_expected_crc;
    struct ClockSettingsRAM clock_config;
    uint32_t magic_crc;        
};

// Размещаем в SRAM2
#ifdef __cplusplus
extern "C" {
#endif
    extern volatile struct BootContext boot_ctx;
#ifdef __cplusplus
}
#endif
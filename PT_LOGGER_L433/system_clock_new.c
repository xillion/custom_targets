/* mbed Microcontroller Library
 * SPDX-License-Identifier: BSD-3-Clause
 ******************************************************************************
 *
 * Copyright (c) 2015-2021 STMicroelectronics.
 * All rights reserved.
 *
 * This software component is licensed by ST under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************
 */

/**
  * This file configures the system clock depending on config from targets.json:
  *-----------------------------------------------------------------------------
  * System clock source | 1- USE_PLL_HSE_EXTC (external clock)
  *                     | 2- USE_PLL_HSE_XTAL (external xtal)
  *                     | 3- USE_PLL_HSI (internal 16 MHz)
  *                     | 4- USE_PLL_MSI (internal 100kHz to 48 MHz)
  *-----------------------------------------------------------------------------
  * SYSCLK(MHz)         | 80
  * AHBCLK (MHz)        | 80
  * APB1CLK (MHz)       | 80
  * APB2CLK (MHz)       | 80
  * USB capable         | YES
  *-----------------------------------------------------------------------------
**/

#include "stm32l4xx.h"
#include "mbed_error.h"
#include "boot_context.h"

// clock source is selected with CLOCK_SOURCE in json config
#define USE_PLL_HSE_EXTC 0x8 // Use external clock (OSC_IN)
#define USE_PLL_HSE_XTAL 0x4 // Use external xtal (OSC_IN/OSC_OUT)
#define USE_PLL_HSI      0x2 // Use HSI internal clock
#define USE_PLL_MSI      0x1 // Use MSI internal clock

#define DEBUG_MCO        (0) // Output the MCO on PA8 for debugging (0=OFF, 1=SYSCLK, 2=HSE, 3=HSI, 4=MSI)

extern volatile struct BootContext boot_ctx;
#define REASON_NEW_CLK 0xCAFE0001

#if ( ((CLOCK_SOURCE) & USE_PLL_HSE_XTAL) || ((CLOCK_SOURCE) & USE_PLL_HSE_EXTC) )
uint8_t SetSysClock_PLL_HSE(uint8_t bypass);
#endif /* ((CLOCK_SOURCE) & USE_PLL_HSE_XTAL) || ((CLOCK_SOURCE) & USE_PLL_HSE_EXTC) */

#if ((CLOCK_SOURCE) & USE_PLL_HSI)
uint8_t SetSysClock_PLL_HSI(void);
#endif /* ((CLOCK_SOURCE) & USE_PLL_HSI) */

#if ((CLOCK_SOURCE) & USE_PLL_MSI)
uint8_t SetSysClock_PLL_MSI(void);
uint8_t SetSysClock_Dynamic(void);
#endif /* ((CLOCK_SOURCE) & USE_PLL_MSI) */


/**
  * @brief  Configures the System clock source, PLL Multiplier and Divider factors,
  *               AHB/APBx prescalers and Flash settings
  * @note   This function is called in mbed_sdk_init() function (targets/TARGET_STM/mbed_overrides.c)
  *         and after each deepsleep period in hal_deepsleep() (targets/TARGET_STM/sleep.c)
  * @param  None
  * @retval None
  */

MBED_WEAK void SetSysClock(void)
{

    if (boot_ctx.boot_reason == REASON_NEW_CLK) {
        if (SetSysClock_Dynamic() == 1) {
            return; // Успешно загрузились с кастомными чатотами
        } else {
            error("SetSysClock_Dynamic failed\n");
        }
    }

#if ((CLOCK_SOURCE) & USE_PLL_HSE_EXTC)
    /* 1- Try to start with HSE and external clock */
    if (SetSysClock_PLL_HSE(1) == 0)
#endif
    {
#if ((CLOCK_SOURCE) & USE_PLL_HSE_XTAL)
        /* 2- If fail try to start with HSE and external xtal */
        if (SetSysClock_PLL_HSE(0) == 0)
#endif
        {
#if ((CLOCK_SOURCE) & USE_PLL_HSI)
            /* 3- If fail start with HSI clock */
            if (SetSysClock_PLL_HSI() == 0)
#endif
            {
#if ((CLOCK_SOURCE) & USE_PLL_MSI)
                /* 4- If fail start with MSI clock */
                if (SetSysClock_PLL_MSI() == 0)
#endif
                {
                    {
                        error("SetSysClock failed\n");
                    }
                }
            }
        }
    }

    // Output clock on MCO1 pin(PA8) for debugging purpose
#if DEBUG_MCO == 1
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_SYSCLK, RCC_MCODIV_1);
#endif
}

#if ( ((CLOCK_SOURCE) & USE_PLL_HSE_XTAL) || ((CLOCK_SOURCE) & USE_PLL_HSE_EXTC) )
/******************************************************************************/
/*            PLL (clocked by HSE) used as System clock source                */
/******************************************************************************/
MBED_WEAK uint8_t SetSysClock_PLL_HSE(uint8_t bypass)
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_PeriphCLKInitTypeDef RCC_PeriphClkInit = {0};

    // Enable HSE oscillator and activate PLL with HSE as source
    RCC_OscInitStruct.OscillatorType        = RCC_OSCILLATORTYPE_HSE;
    if (bypass == 0) {
        RCC_OscInitStruct.HSEState            = RCC_HSE_ON; // External xtal on OSC_IN/OSC_OUT
    } else {
        RCC_OscInitStruct.HSEState            = RCC_HSE_BYPASS; // External clock on OSC_IN
    }
    RCC_OscInitStruct.PLL.PLLSource         = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLState          = RCC_PLL_ON;
#if HSE_VALUE==8000000
    RCC_OscInitStruct.PLL.PLLM              = 1; // VCO input clock = 8 MHz (8 MHz / 1)
#else
#error Unsupported externall clock value, check HSE_VALUE define
#endif
    RCC_OscInitStruct.PLL.PLLN              = 20; // VCO output clock = 160 MHz (8 MHz * 20)
    RCC_OscInitStruct.PLL.PLLP              = 7;
    RCC_OscInitStruct.PLL.PLLQ              = 2;
    RCC_OscInitStruct.PLL.PLLR              = 2;  // PLL clock = 80 MHz (160 MHz / 2)

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        return 0; // FAIL
    }

    // Select PLL clock as system clock source and configure the HCLK, PCLK1 and PCLK2 clocks dividers
    RCC_ClkInitStruct.ClockType      = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK; // 80 MHz
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;         // 80 MHz
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;           // 80 MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;           // 80 MHz
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        return 0; // FAIL
    }

#if DEVICE_USBDEVICE
    RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    RCC_PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLLSAI1;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_HSE;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1N = 12;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV2;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_48M2CLK;
    if (HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphClkInit) != HAL_OK) {
        return 0; // FAIL
    }
#endif /* DEVICE_USBDEVICE */

    // Output clock on MCO1 pin(PA8) for debugging purpose
#if DEBUG_MCO == 2
    if (bypass == 0) {
        HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_2);    // 4 MHz
    } else {
        HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_1);    // 8 MHz
    }
#endif

    return 1; // OK
}
#endif /* ((CLOCK_SOURCE) & USE_PLL_HSE_XTAL) || ((CLOCK_SOURCE) & USE_PLL_HSE_EXTC) */

#if ((CLOCK_SOURCE) & USE_PLL_HSI)
/******************************************************************************/
/*            PLL (clocked by HSI) used as System clock source                */
/******************************************************************************/
uint8_t SetSysClock_PLL_HSI(void)
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_PeriphCLKInitTypeDef RCC_PeriphClkInit = {0};

    // Enable HSI oscillator and activate PLL with HSI as source
    RCC_OscInitStruct.OscillatorType       = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState             = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue  = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState         = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource        = RCC_PLLSOURCE_HSI; // 16 MHz
    RCC_OscInitStruct.PLL.PLLM             = 2;  // VCO input clock = 8 MHz (16 MHz / 2)
    RCC_OscInitStruct.PLL.PLLN             = 20; // VCO output clock = 160 MHz (8 MHz * 20)
    RCC_OscInitStruct.PLL.PLLP             = 7;
    RCC_OscInitStruct.PLL.PLLQ             = 2;
    RCC_OscInitStruct.PLL.PLLR             = 2;  // PLL clock = 80 MHz (160 MHz / 2)
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        return 0; // FAIL
    }

    // Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2 clocks dividers
    RCC_ClkInitStruct.ClockType      = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK; // 80 MHz
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;         // 80 MHz
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;           // 80 MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;           // 80 MHz
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        return 0; // FAIL
    }

#if DEVICE_USBDEVICE
    RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    RCC_PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLLSAI1;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_HSI;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1M = 2;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1N = 12;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV2;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
    RCC_PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_48M2CLK;
    if (HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphClkInit) != HAL_OK) {
        return 0; // FAIL
    }
#endif /* DEVICE_USBDEVICE */

    // Output clock on MCO1 pin(PA8) for debugging purpose
#if DEBUG_MCO == 3
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI, RCC_MCODIV_1); // 16 MHz
#endif

    return 1; // OK
}
#endif /* ((CLOCK_SOURCE) & USE_PLL_HSI) */

#if ((CLOCK_SOURCE) & USE_PLL_MSI)
/******************************************************************************/
/*            PLL (clocked by MSI) used as System clock source                */
/******************************************************************************/
MBED_WEAK uint8_t SetSysClock_PLL_MSI(void)
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

#if MBED_CONF_TARGET_LSE_AVAILABLE
    // Enable LSE Oscillator to automatically calibrate the MSI clock
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_NONE; // No PLL update
#if MBED_CONF_TARGET_LSE_BYPASS
    RCC_OscInitStruct.LSEState       = RCC_LSE_BYPASS;
#else
    RCC_OscInitStruct.LSEState       = RCC_LSE_ON;
#endif
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        return 0; // FAIL
    }
#endif /* MBED_CONF_TARGET_LSE_AVAILABLE */

    /* Enable MSI Oscillator and activate PLL with MSI as source */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState            = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.MSIClockRange       = RCC_MSIRANGE_11; /* 48 MHz */
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM            = 6;    /* 8 MHz */
    RCC_OscInitStruct.PLL.PLLN            = 40;   /* 320 MHz */
    RCC_OscInitStruct.PLL.PLLP            = 7;    /* 45 MHz */
    RCC_OscInitStruct.PLL.PLLQ            = 4;    /* 80 MHz */
    RCC_OscInitStruct.PLL.PLLR            = 4;    /* 80 MHz */
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        return 0; // FAIL
    }

#if MBED_CONF_TARGET_LSE_AVAILABLE
    /* Enable MSI Auto-calibration through LSE */
    HAL_RCCEx_EnableMSIPLLMode();
#endif /* MBED_CONF_TARGET_LSE_AVAILABLE */

#if DEVICE_USBDEVICE
    /* Select MSI output as USB clock source */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_MSI; /* 48 MHz */
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
#endif /* DEVICE_USBDEVICE */

    // Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2 clocks dividers
    RCC_ClkInitStruct.ClockType      = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK; /* 80 MHz */
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;         /* 80 MHz */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;           /* 80 MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;           /* 80 MHz */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        return 0; // FAIL
    }

    // Output clock on MCO1 pin(PA8) for debugging purpose
#if DEBUG_MCO == 4
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_MSI, RCC_MCODIV_2); // 2 MHz
#endif

    return 1; // OK
}


static uint32_t map_msi_range(uint8_t range) { return (range << 4); } // В STM32L4 смещение RANGE ровно на 4 бита
static uint32_t map_pll_m(uint8_t m) { return (m - 1); } // HAL: 1->0, 2->1 ... 8->7
static uint32_t map_pll_r(uint8_t r) {
    if (r == 2) return RCC_PLLR_DIV2;
    if (r == 4) return RCC_PLLR_DIV4;
    if (r == 6) return RCC_PLLR_DIV6;
    return RCC_PLLR_DIV8;
}
static uint32_t map_ahb_div(uint16_t div) {
    switch(div) {
        case 512: return RCC_SYSCLK_DIV512;
        case 256: return RCC_SYSCLK_DIV256;
        case 128: return RCC_SYSCLK_DIV128;
        case 64:  return RCC_SYSCLK_DIV64;
        case 16:  return RCC_SYSCLK_DIV16;
        case 8:   return RCC_SYSCLK_DIV8;
        case 4:   return RCC_SYSCLK_DIV4;
        case 2:   return RCC_SYSCLK_DIV2;
        default:  return RCC_SYSCLK_DIV1;
    }
}
static uint32_t map_apb_div(uint8_t div) {
    switch(div) {
        case 16: return RCC_HCLK_DIV16;
        case 8:  return RCC_HCLK_DIV8;
        case 4:  return RCC_HCLK_DIV4;
        case 2:  return RCC_HCLK_DIV2;
        default: return RCC_HCLK_DIV1;
    }
}

// Динамический расчет задержек флеша (для Voltage Scale 1)
static uint32_t calc_flash_latency(uint32_t sysclk_mhz) {
    if (sysclk_mhz <= 16) return FLASH_LATENCY_0;
    if (sysclk_mhz <= 32) return FLASH_LATENCY_1;
    if (sysclk_mhz <= 48) return FLASH_LATENCY_2;
    if (sysclk_mhz <= 64) return FLASH_LATENCY_3;
    return FLASH_LATENCY_4;
}

uint8_t SetSysClock_Dynamic(void)
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    // Читаем из SRAM2 (с защитой от мусора)
    uint8_t msi_r = boot_ctx.clock_config.msi_range;
    uint8_t pll_m = boot_ctx.clock_config.pll_m;
    uint8_t pll_n = boot_ctx.clock_config.pll_n;
    uint8_t pll_r = boot_ctx.clock_config.pll_r;

    // Вычисляем будущую частоту SYSCLK (VCO_IN * N / R), предполагая что MSI=48МГц 
    // Для универсальности, если десктоп передает реальный msi_range, нужно считать частоту в МГц:
    uint32_t msi_freq_hz[] = {100000, 200000, 400000, 800000, 1000000, 2000000, 4000000, 8000000, 16000000, 24000000, 32000000, 48000000};
    uint32_t sysclk_mhz = (msi_freq_hz[msi_r] / 1000000) / pll_m * pll_n / pll_r;

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) return 0;

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI | RCC_OSCILLATORTYPE_HSI48;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON; // USB clock

    RCC_OscInitStruct.MSIState            = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.MSIClockRange       = map_msi_range(msi_r);
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM            = map_pll_m(pll_m);
    RCC_OscInitStruct.PLL.PLLN            = pll_n;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV7; 
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR            = map_pll_r(pll_r);

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) return 0;

    // USB Clock setup
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);

    // Apply dividers
    RCC_ClkInitStruct.ClockType      = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK; 
    RCC_ClkInitStruct.AHBCLKDivider  = map_ahb_div(boot_ctx.clock_config.ahb_div);         
    RCC_ClkInitStruct.APB1CLKDivider = map_apb_div(boot_ctx.clock_config.apb1_div);           
    RCC_ClkInitStruct.APB2CLKDivider = map_apb_div(boot_ctx.clock_config.apb2_div);           

    uint32_t flash_lat = calc_flash_latency(sysclk_mhz);
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, flash_lat) != HAL_OK) return 0;

    return 1;
}

#endif /* ((CLOCK_SOURCE) & USE_PLL_MSI) */
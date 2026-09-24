#include "boot_init.h"
#include "mbed.h"
#include "nvic_addr.h"

#define SYSTEM_BOOTLOADER_ADDRESS 0x1FFF0000

std::uint32_t boot_reason __attribute__((section(".crash_data_ram")));
volatile struct BootContext boot_ctx __attribute__((section(".m_crash_data_ram")));

void reboot_into_dfu()
{
    boot_reason = 0xDECAFBAD;
    NVIC_SystemReset();
}

void check_reset_reason() {
    reset_reason_t reason = ResetReason::get();
    if (reason == RESET_REASON_SOFTWARE) {
        printf("Reboot was triggered by a software reset.\r\n");
    }
}

bool is_boot_context_valid() {
    uint32_t expected_crc = BOOT_CTX_MAGIC ^ boot_ctx.boot_reason ^ boot_ctx.fw_update_addr ^ boot_ctx.fw_update_size;
    return (boot_ctx.magic_crc == expected_crc);
}

void reboot_with_context(uint32_t reason, uint32_t fw_addr, uint32_t fw_size) {
    boot_ctx.boot_reason = reason;
    boot_ctx.fw_update_addr = fw_addr;
    boot_ctx.fw_update_size = fw_size;

    // Считаем простую контрольную сумму (или просто пишем magic + инверсию)
    boot_ctx.magic_crc = BOOT_CTX_MAGIC ^ reason ^ fw_addr ^ fw_size;

    __DSB(); // Ждем завершения операций с памятью
    NVIC_SystemReset();
}

void SystemInit(void)
{
    std::uint32_t conBootloadAddress = SYSTEM_BOOTLOADER_ADDRESS;
    void (*SysMemBootJump)(void);
    if(boot_reason == 0xDECAFBAD){
        boot_reason = 0;
        SysMemBootJump = (void (*)(void)) (*((std::uint32_t *) (conBootloadAddress + 4)));
        __set_MSP(*(std::uint32_t *)conBootloadAddress);

        SYSCFG->MEMRMP = 0x01;

        SysMemBootJump();

        while (true) {
        }
    }

    SCB->VTOR = NVIC_FLASH_VECTOR_ADDRESS; // MBED

    #if defined(USER_VECT_TAB_ADDRESS)
    /* Configure the Vector Table location -------------------------------------*/
    SCB->VTOR = VECT_TAB_BASE_ADDRESS | VECT_TAB_OFFSET;
    #endif

    /* FPU settings ------------------------------------------------------------*/
    #if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << 20U)|(3UL << 22U));  /* set CP10 and CP11 Full Access */
    #endif

    /* Reset the RCC clock configuration to the default reset state ------------*/
    /* Set MSION bit */
    RCC->CR |= RCC_CR_MSION;

    /* Reset CFGR register */
    RCC->CFGR = 0x00000000U;

    /* Reset HSEON, CSSON , HSION, and PLLON bits */
    RCC->CR &= 0xEAF6FFFFU;

    /* Reset PLLCFGR register */
    RCC->PLLCFGR = 0x00001000U;

    /* Reset HSEBYP bit */
    RCC->CR &= 0xFFFBFFFFU;

    /* Disable all interrupts */
    RCC->CIER = 0x00000000U;
}

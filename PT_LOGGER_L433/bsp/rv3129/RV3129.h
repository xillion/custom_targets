/**
 * @file RV3129.h
 * @brief Mbed driver for Micro Crystal RV-3129-C3 DTCXO RTC module
 *
 * Register map based on RV-3129-C3 Application Manual Rev 1.1 (June 2019)
 *
 * Key architectural note:
 *   The CLKOUT frequency (FD1/FD0 bits) lives in the EEPROM Control register
 *   at address 0x30, NOT in RAM. EEPROM writes require a special unlock
 *   sequence and take ~14 ms to complete. The driver handles this automatically
 *   but callers must be aware of the latency and write-endurance limits
 *   (~10 000 cycles per EEPROM cell).
 */

#pragma once
#include "mbed.h"
#include <cstdint>
#include <ctime>

// ---------------------------------------------------------------------------
// I2C device address
// ---------------------------------------------------------------------------
#define RV3129_I2C_ADDR     (0x56<<1)   ///< 7-bit shifted to 8-bit write address

// ---------------------------------------------------------------------------
// RAM register addresses
// ---------------------------------------------------------------------------
namespace RV3129Reg {

// Control page  (page 0x00, addresses 0x00–0x04)
constexpr uint8_t CTRL1        = 0x00;
constexpr uint8_t CTRL_INT     = 0x01;
constexpr uint8_t CTRL_INT_FLAG= 0x02;
constexpr uint8_t CTRL_STATUS  = 0x03;
constexpr uint8_t CTRL_RESET   = 0x04;

// Clock page    (page 0x01, addresses 0x08–0x0E)
constexpr uint8_t SECONDS      = 0x08;
constexpr uint8_t MINUTES      = 0x09;
constexpr uint8_t HOURS        = 0x0A;
constexpr uint8_t DAYS         = 0x0B;
constexpr uint8_t WEEKDAYS     = 0x0C;
constexpr uint8_t MONTHS       = 0x0D;
constexpr uint8_t YEARS        = 0x0E;

// Alarm page    (page 0x02, addresses 0x10–0x16)
constexpr uint8_t ALM_SEC      = 0x10;
constexpr uint8_t ALM_MIN      = 0x11;
constexpr uint8_t ALM_HOUR     = 0x12;
constexpr uint8_t ALM_DAY      = 0x13;
constexpr uint8_t ALM_WEEKDAY  = 0x14;
constexpr uint8_t ALM_MONTH    = 0x15;
constexpr uint8_t ALM_YEAR     = 0x16;

// Timer page    (page 0x03, addresses 0x18–0x19)
constexpr uint8_t TIMER_LOW    = 0x18;
constexpr uint8_t TIMER_HIGH   = 0x19;

// Temperature page (page 0x04, address 0x20)
constexpr uint8_t TEMPERATURE  = 0x20;

// EEPROM user   (page 0x05, addresses 0x28–0x29)  -- EEPROM (non-volatile)
constexpr uint8_t EE_USER0     = 0x28;
constexpr uint8_t EE_USER1     = 0x29;

// EEPROM control page (page 0x06, addresses 0x30–0x33) -- EEPROM (non-volatile)
constexpr uint8_t EE_CTRL      = 0x30;   ///< CLKOUT freq, trickle, ThE, ThP
constexpr uint8_t EE_XTAL_OFF  = 0x31;   ///< factory – do not touch
constexpr uint8_t EE_XTAL_COEF = 0x32;   ///< factory – do not touch
constexpr uint8_t EE_XTAL_T0   = 0x33;   ///< factory – do not touch

// User RAM      (page 0x07, addresses 0x38–0x3F)
constexpr uint8_t USER_RAM_BASE     = 0x38;   ///< 8 bytes, 0x38–0x3F
constexpr uint8_t USER_RAM_SIZE     = 8;
constexpr uint8_t MAX_REG_FILE_SIZE = 8;    ///< max number of bytes that can be read/written in one transaction

} // namespace RV3129Reg

// ---------------------------------------------------------------------------
// Bit-field masks
// ---------------------------------------------------------------------------
namespace RV3129Bits {

// CTRL1 (0x00)
constexpr uint8_t CTRL1_CLK_INT  = 0x80; ///< 1=CLKOUT function, 0=INT function on CLKOUT pin
constexpr uint8_t CTRL1_TD1      = 0x40; ///< Timer clock source bit1
constexpr uint8_t CTRL1_TD0      = 0x20; ///< Timer clock source bit0
constexpr uint8_t CTRL1_SRON     = 0x10; ///< Self-recovery enable
constexpr uint8_t CTRL1_EERE     = 0x08; ///< Auto EEPROM refresh every hour
constexpr uint8_t CTRL1_TAR      = 0x04; ///< Timer auto-reload
constexpr uint8_t CTRL1_TE       = 0x02; ///< Timer enable
constexpr uint8_t CTRL1_WE       = 0x01; ///< Watch (1 Hz) enable

// CTRL_INT (0x01)
constexpr uint8_t INT_SRIE  = 0x10; ///< Self-recovery interrupt enable
constexpr uint8_t INT_V2IE  = 0x08; ///< VLOW2 interrupt enable
constexpr uint8_t INT_V1IE  = 0x04; ///< VLOW1 interrupt enable
constexpr uint8_t INT_TIE   = 0x02; ///< Timer interrupt enable
constexpr uint8_t INT_AIE   = 0x01; ///< Alarm interrupt enable

// CTRL_INT_FLAG (0x02)
constexpr uint8_t FLAG_SRF  = 0x10;
constexpr uint8_t FLAG_V2IF = 0x08;
constexpr uint8_t FLAG_V1IF = 0x04;
constexpr uint8_t FLAG_TF   = 0x02;
constexpr uint8_t FLAG_AF   = 0x01;

// CTRL_STATUS (0x03)
constexpr uint8_t STATUS_EEBUSY = 0x80; ///< EEPROM busy
constexpr uint8_t STATUS_PON    = 0x20; ///< Power-on reset flag
constexpr uint8_t STATUS_SR     = 0x10; ///< Self-recovery reset flag
constexpr uint8_t STATUS_V2F    = 0x08; ///< VLOW2 flag
constexpr uint8_t STATUS_V1F    = 0x04; ///< VLOW1 flag

// CTRL_RESET (0x04)
constexpr uint8_t RESET_SYSR    = 0x10; ///< System reset bit

// EE_CTRL (0x30) – EEPROM register controlling CLKOUT frequency
// Bits [7:4] = R80k/R20k/R5k/R1k  trickle charger
// Bits [3:2] = FD1/FD0             CLKOUT frequency
// Bit  [1]   = ThE                 thermometer enable
// Bit  [0]   = ThP                 thermometer period
constexpr uint8_t EECTRL_R80K   = 0x80;
constexpr uint8_t EECTRL_R20K   = 0x40;
constexpr uint8_t EECTRL_R5K    = 0x20;
constexpr uint8_t EECTRL_R1K    = 0x10;
constexpr uint8_t EECTRL_FD1    = 0x08;
constexpr uint8_t EECTRL_FD0    = 0x04;
constexpr uint8_t EECTRL_ThE    = 0x02;
constexpr uint8_t EECTRL_ThP    = 0x01;

// Alarm enable bits in alarm registers (MSB of each alarm register)
constexpr uint8_t ALM_ENABLE    = 0x80; ///< 0=alarm active for this field, 1=disabled

} // namespace RV3129Bits

// ---------------------------------------------------------------------------
// Public enumerations
// ---------------------------------------------------------------------------

/** CLKOUT frequency selection (FD1:FD0 in EEPROM_CTRL register 0x30).
 *  Stored in EEPROM – survives power loss, costs ~14 ms write time. */
enum class RV3129ClkFreq : uint8_t {
    FREQ_32768HZ = 0x00,   ///< FD1=0, FD0=0  (default from factory)
    FREQ_1024HZ  = 0x04,   ///< FD1=0, FD0=1
    FREQ_32HZ    = 0x08,   ///< FD1=1, FD0=0
    FREQ_1HZ     = 0x0C,   ///< FD1=1, FD0=1
};

/** Timer source clock (TD1:TD0 in CTRL1 register 0x00). */
enum class RV3129TimerClk : uint8_t {
    CLK_4096HZ = 0x00,  ///< TD1=0, TD0=0
    CLK_64HZ   = 0x20,  ///< TD1=0, TD0=1
    CLK_1HZ    = 0x40,  ///< TD1=1, TD0=0
    CLK_1_60HZ = 0x60,  ///< TD1=1, TD0=1  (1/60 Hz, i.e., 1 per minute)
};

/** Interrupt mode selection. */
enum class RV3129IntMode : uint8_t {
    ALARM  = RV3129Bits::INT_AIE,
    TIMER  = RV3129Bits::INT_TIE,
    VLOW1  = RV3129Bits::INT_V1IE,
    VLOW2  = RV3129Bits::INT_V2IE,
    SR     = RV3129Bits::INT_SRIE,
};

/** Interrupt flag selection. */
enum class RV3129IntFlag : uint8_t {
    ALARM  = RV3129Bits::FLAG_AF,   // Note: ALARM flag is cleared by writing 0 to FLAG_AF bit in CTRL_INT_FLAG register
    TIMER  = RV3129Bits::FLAG_TF,   // Note: TIMER flag is cleared by writing 0 to FLAG_TF bit in CTRL_INT_FLAG register
    VLOW1  = RV3129Bits::FLAG_V1IF, // Note: VLOW1 flag is cleared by writing 0 to FLAG_V1IF bit in CTRL_INT_FLAG register
    VLOW2  = RV3129Bits::FLAG_V2IF, // Note: VLOW2 flag is cleared by writing 0 to FLAG_V2IF bit in CTRL_INT_FLAG register
    SR     = RV3129Bits::FLAG_SRF,  // Note: SR flag is cleared by writing 0 to FLAG_SRF bit in CTRL_INT_FLAG register
    ALL    = 0x1F,
};

// ---------------------------------------------------------------------------
// Driver class
// ---------------------------------------------------------------------------

/**
 * @brief Mbed driver for the Micro Crystal RV-3129-C3 RTC.
 *
 * Usage example:
 * @code
 *   I2C i2c(PB_9, PB_8);
 *   RV3129 rtc(i2c);
 *
 *   rtc.begin();
 *   rtc.setUnixTime(1700000000UL);
 *
 *   // Set CLKOUT to 1 Hz – writes EEPROM, takes ~14 ms
 *   rtc.setClkoutFreq(RV3129ClkFreq::FREQ_1HZ);
 * @endcode
 */
class RV3129 {
public:
    /** @param i2c Reference to a configured I2C object (400 kHz recommended). */
    explicit RV3129(I2C &i2c, uint8_t addr = RV3129_I2C_ADDR);

    // ------------------------------------------------------------------
    // Initialisation
    // ------------------------------------------------------------------

    /** Initialise the driver; clears PON/SR flags. Returns true on success. */
    bool begin();

    // ------------------------------------------------------------------
    // Raw register access
    // ------------------------------------------------------------------

    /**
     * @brief Read one or more consecutive registers.
     * @param regAddr  Starting register address.
     * @param buf      Destination buffer.
     * @param len      Number of bytes to read.
     * @return true on success.
     *
     * @note The RV-3129 auto-increments the address pointer within a page.
     *       Cross-page reads are not supported in a single transaction.
     */
    bool readRegs(uint8_t regAddr, uint8_t *buf, size_t len);

    /**
     * @brief Write one or more consecutive registers.
     * @param regAddr  Starting register address.
     * @param buf      Data to write.
     * @param len      Number of bytes to write.
     * @return true on success.
     *
     * @warning For EEPROM registers (0x28–0x33) use writeEEPROMReg() instead.
     */
    bool writeRegs(uint8_t regAddr, const uint8_t *buf, size_t len);

    /** Convenience: read single register. */
    bool readReg(uint8_t regAddr, uint8_t &val);

    /** Convenience: write single register. */
    bool writeReg(uint8_t regAddr, uint8_t val);

    // ------------------------------------------------------------------
    // EEPROM register access
    // ------------------------------------------------------------------
    // The RV-3129-C3 EEPROM control registers (0x28–0x33) behave like RAM
    // for reads, but writes require waiting for EEbusy to clear (CTRL_STATUS
    // bit 7). The device automatically programs EEPROM after any write to an
    // EEPROM-page address. The typical write cycle is ~14 ms.
    //
    // The non-volatile user bytes (0x28–0x29) and the EEPROM control register
    // (0x30) are the only EEPROM cells the user should normally modify. The
    // crystal calibration registers (0x31–0x33) are factory-programmed and
    // must NOT be overwritten.

    /**
     * @brief Read an EEPROM-page register (0x28–0x33).
     *  Functionally identical to readReg(); provided for clarity.
     */
    bool readEEPROMReg(uint8_t regAddr, uint8_t &val);

    /**
     * @brief Write to an EEPROM-page register and wait for EEPROM programming.
     * @param regAddr  Must be 0x28, 0x29, or 0x30. DO NOT use for 0x31–0x33.
     * @param val      Value to write.
     * @param timeoutMs  Maximum wait for EEbusy to clear (default 100 ms).
     * @return true if write succeeded and EEPROM became ready within timeout.
     */
    bool writeEEPROMReg(uint8_t regAddr, uint8_t val, uint32_t timeoutMs = 100);

    // ------------------------------------------------------------------
    // Time (Unix timestamp)
    // ------------------------------------------------------------------

    /**
     * @brief Read current time as a Unix timestamp (seconds since 1970-01-01).
     * @param unixTime  Output: current time in seconds.
     * @return true on success.
     */
    bool getUnixTime(time_t &unixTime);

    /**
     * @brief Set the RTC to a Unix timestamp.
     * @param unixTime  Seconds since 1970-01-01 UTC.
     * @return true on success.
     */
    bool setUnixTime(time_t unixTime);

    // ------------------------------------------------------------------
    // Non-volatile user registers (EEPROM 0x28–0x29)
    // ------------------------------------------------------------------

    /**
     * @brief Read the two EEPROM user bytes.
     * @param byte0  Output: contents of register 0x28.
     * @param byte1  Output: contents of register 0x29.
     */
    bool getNVBytes(uint8_t &byte0, uint8_t &byte1);

    /**
     * @brief Write the two EEPROM user bytes (non-volatile, survives power loss).
     * @warning Each call consumes one EEPROM write cycle. Device endurance is
     *          ~10 000 writes per cell. Do not call in tight loops.
     */
    bool setNVBytes(uint8_t byte0, uint8_t byte1);

    // ------------------------------------------------------------------
    // Alarm
    // ------------------------------------------------------------------

    /**
     * @brief Set the alarm to fire at a given Unix timestamp.
     *
     * Internally converts to BCD and sets all alarm registers. Alarm enable
     * bits (AE_x) in each alarm register are cleared (enabled) for Seconds,
     * Minutes, Hours, Days, Months and Years; Weekday alarm is disabled.
     *
     * @param alarmTime  Unix timestamp of the desired alarm instant.
     * @return true on success.
     */
    bool setAlarm(time_t alarmTime);

    /**
     * @brief Read back the alarm time as a Unix timestamp.
     *
     * Only the alarm registers with AE_x=0 (enabled) contribute to the
     * returned value. Returns 0 and false if alarm registers are inconsistent.
     */
    bool getAlarm(time_t &alarmTime);

    /**
     * @brief Enable or disable the alarm interrupt (AIE bit in CTRL_INT).
     */
    bool setAlarmInterrupt(bool enable);

    /** @brief Clear the alarm interrupt flag (AF in CTRL_INT_FLAG). */
    bool clearAlarmFlag();

    /** @brief Check whether the alarm flag is set. */
    bool isAlarmFired(bool &fired);

    // ------------------------------------------------------------------
    // Interrupt mode (CTRL_INT register)
    // ------------------------------------------------------------------

    /**
     * @brief Enable or disable specific interrupt sources.
     * @param mode   One of the RV3129IntMode flags.
     * @param enable true to enable, false to disable.
     */
    bool setInterruptMode(RV3129IntMode mode, bool enable);

    /** @brief Read the raw CTRL_INT register. */
    bool getInterruptMode(uint8_t &intReg);

    /** @brief Read the raw CTRL_INT_FLAG register. */
    bool getInterruptFlags(uint8_t &flagReg);

    /** @brief Clear interrupt flags. */
    bool clearInterruptFlags(RV3129IntFlag flags = RV3129IntFlag::ALL);

    // ------------------------------------------------------------------
    // Periodic countdown timer
    // ------------------------------------------------------------------

    /**
     * @brief Configure and start the countdown timer.
     *
     * Period = count / clockFrequency.
     * Example: 10 s with CLK_1HZ → count=10.
     *
     * @param clkSrc   Source clock for the timer.
     * @param count    16-bit countdown value (Timer_Low + Timer_High).
     * @param autoReload  true = auto-reload (periodic), false = single shot.
     * @param interrupt   true = generate INT when timer reaches zero.
     */
    bool setTimer(RV3129TimerClk clkSrc, uint16_t count,
                  bool autoReload = false, bool interrupt = true);

    /** @brief Stop the countdown timer. */
    bool stopTimer();

    /** @brief Read timer count and clock source. */
    bool getTimer(RV3129TimerClk &clkSrc, uint16_t &count, bool &enabled);

    // ------------------------------------------------------------------
    // CLKOUT frequency  (*** EEPROM register 0x30 ***)
    // ------------------------------------------------------------------
    //
    // The FD1/FD0 bits that select the CLKOUT frequency reside in the
    // EEPROM Control register at address 0x30. This means:
    //  1. Changes persist through power cycles (that is the point).
    //  2. Each setClkoutFreq() call burns one EEPROM write cycle.
    //  3. The operation blocks for ~14 ms while EEPROM programs.
    //  4. The CLKOUT pin must be enabled by the CTRL1.Clk/Int bit.
    //
    // Available frequencies: 32 768 Hz, 1 024 Hz, 32 Hz, 1 Hz.

    /**
     * @brief Set the CLKOUT output frequency (writes EEPROM register 0x30).
     *
     * This also enables the CLKOUT function on pin 3 (sets CTRL1 bit7=1).
     *
     * @param freq  Desired frequency; see RV3129ClkFreq enum.
     * @return true if both the EEPROM write and the CTRL1 update succeeded.
     */
    bool setClkoutFreq(RV3129ClkFreq freq);

    /**
     * @brief Read the current CLKOUT frequency setting from EEPROM register 0x30.
     * @param freq  Output: current setting.
     */
    bool getClkoutFreq(RV3129ClkFreq &freq);

    /**
     * @brief Enable or disable the CLKOUT pin output.
     *  Sets/clears the Clk/Int bit (bit 7) in CTRL1. Does NOT touch EEPROM.
     */
    bool setClkoutEnabled(bool enable);

    // ------------------------------------------------------------------
    // Status helpers
    // ------------------------------------------------------------------

    /** @brief Returns true if an EEPROM write cycle is in progress. */
    bool isEEPROMBusy(bool &busy);

    /** @brief Returns true if a power-on reset has occurred (clears flag). */
    bool checkAndClearPOR(bool &pon);

    /** @brief Issue a software system reset (CTRL_RESET.SysR). */
    bool systemReset();

private:
    I2C    &_i2c;
    uint8_t _addr;          ///< 8-bit I2C write address

    // BCD helpers
    static uint8_t  bcdToDec(uint8_t bcd);
    static uint8_t  decToBcd(uint8_t dec);

    // Block until STATUS_EEBUSY clears or timeout expires
    bool waitEEReady(uint32_t timeoutMs);
};

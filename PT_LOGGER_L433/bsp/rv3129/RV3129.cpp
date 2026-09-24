/**
 * @file RV3129.cpp
 * @brief Mbed driver for Micro Crystal RV-3129-C3 DTCXO RTC module
 */

#include "RV3129.h"

using namespace RV3129Reg;
using namespace RV3129Bits;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RV3129::RV3129(I2C &i2c, uint8_t addr)
    : _i2c(i2c), _addr(addr) {}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

bool RV3129::begin()
{
    // Check communication by reading CTRL1
    uint8_t val;
    if (!readReg(CTRL1, val)) {
        return false;
    }

    // Clear Power-On Reset and Self-Recovery flags in CTRL_STATUS
    uint8_t status;
    if (readReg(CTRL_STATUS, status)) {
        // Write back with PON and SR cleared
        writeReg(CTRL_STATUS, status & ~(STATUS_PON | STATUS_SR));
    }

    // Clear any pending interrupt flags
    clearInterruptFlags();

    // Enable the 1 Hz watch source (WE=1) so the clock runs
    // Keep existing bits; just set WE
    val |= CTRL1_WE;
    writeReg(CTRL1, val);

    return true;
}

// ---------------------------------------------------------------------------
// Raw register access
// ---------------------------------------------------------------------------

bool RV3129::readReg(uint8_t regAddr, uint8_t &val)
{
    return readRegs(regAddr, &val, 1);
}

bool RV3129::writeReg(uint8_t regAddr, uint8_t val)
{
    return writeRegs(regAddr, &val, 1);
}

bool RV3129::readRegs(uint8_t regAddr, uint8_t *buf, size_t len)
{
    // Send register address
    if (_i2c.write(_addr, reinterpret_cast<const char *>(&regAddr), 1, true) != 0) {
        return false;
    }
    // Read bytes
    if (_i2c.read(_addr | 0x01, reinterpret_cast<char *>(buf), len) != 0) {
        return false;
    }
    return true;
}

bool RV3129::writeRegs(uint8_t regAddr, const uint8_t *buf, size_t len)
{
    // Build packet: [addr, data...]
    uint8_t pkt[MAX_REG_FILE_SIZE];
    pkt[0] = regAddr;
    for (size_t i = 0; i < len; ++i) {
        pkt[i + 1] = buf[i];
    }
    return (_i2c.write(_addr, reinterpret_cast<const char *>(pkt), len + 1) == 0);
}

// ---------------------------------------------------------------------------
// EEPROM register access
// ---------------------------------------------------------------------------

bool RV3129::readEEPROMReg(uint8_t regAddr, uint8_t &val)
{
    return readReg(regAddr, val);
}

bool RV3129::waitEEReady(uint32_t timeoutMs)
{
    uint32_t elapsed = 0;
    const uint32_t step = 2;
    while (elapsed < timeoutMs) {
        uint8_t status;
        if (!readReg(CTRL_STATUS, status)) {
            return false;
        }
        if (!(status & STATUS_EEBUSY)) {
            return true;
        }
        wait_us(step * 1000);
        elapsed += step;
    }
    return false; // timeout
}

bool RV3129::writeEEPROMReg(uint8_t regAddr, uint8_t val, uint32_t timeoutMs)
{
    // Reject crystal calibration addresses – user must not touch these
    if (regAddr >= EE_XTAL_OFF && regAddr <= EE_XTAL_T0) {
        return false;
    }
    // Reject addresses outside EEPROM pages
    if (!((regAddr >= EE_USER0 && regAddr <= EE_USER1) || regAddr == EE_CTRL)) {
        return false;
    }

    // Wait for any previous EEPROM activity to finish
    if (!waitEEReady(timeoutMs)) {
        return false;
    }

    // Write the register (device auto-programs EEPROM after write to EEPROM page)
    if (!writeReg(regAddr, val)) {
        return false;
    }

    // Wait for the programming cycle to complete (~14 ms typical)
    return waitEEReady(timeoutMs);
}

// ---------------------------------------------------------------------------
// BCD helpers
// ---------------------------------------------------------------------------

uint8_t RV3129::bcdToDec(uint8_t bcd)
{
    return static_cast<uint8_t>((bcd >> 4) * 10 + (bcd & 0x0F));
}

uint8_t RV3129::decToBcd(uint8_t dec)
{
    return static_cast<uint8_t>(((dec / 10) << 4) | (dec % 10));
}

// ---------------------------------------------------------------------------
// Time (Unix timestamp)
// ---------------------------------------------------------------------------

bool RV3129::getUnixTime(time_t &unixTime)
{
    // Read 7 bytes starting at SECONDS register (0x08..0x0E)
    uint8_t raw[7];
    if (!readRegs(SECONDS, raw, 7)) {
        return false;
    }

    // Decode BCD fields
    struct tm t = {};
    t.tm_sec   = bcdToDec(raw[0] & 0x7F);   // Seconds: bits 6–0
    t.tm_min   = bcdToDec(raw[1] & 0x7F);   // Minutes: bits 6–0
    t.tm_hour  = bcdToDec(raw[2] & 0x3F);   // Hours:   bits 5–0 (24h mode)
    t.tm_mday  = bcdToDec(raw[3] & 0x3F);   // Days:    bits 5–0
    // raw[4] = Weekdays (not needed for tm)
    t.tm_mon   = bcdToDec(raw[5] & 0x1F) - 1; // Months:  bits 4–0, tm_mon is 0-based
    t.tm_year  = bcdToDec(raw[6]) + 100;       // Years:   BCD relative to 2000, tm_year relative to 1900

    t.tm_isdst = 0;

    unixTime = mktime(&t);
    return (unixTime != static_cast<time_t>(-1));
}

bool RV3129::setUnixTime(time_t unixTime)
{
    struct tm *t = gmtime(&unixTime);
    if (!t) {
        return false;
    }

    uint8_t raw[7];
    raw[0] = decToBcd(static_cast<uint8_t>(t->tm_sec));
    raw[1] = decToBcd(static_cast<uint8_t>(t->tm_min));
    raw[2] = decToBcd(static_cast<uint8_t>(t->tm_hour));
    raw[3] = decToBcd(static_cast<uint8_t>(t->tm_mday));
    // Weekday: tm_wday is 0=Sunday; device expects 0–6 with same convention
    raw[4] = static_cast<uint8_t>(1u << t->tm_wday);  // one-hot weekday
    raw[5] = decToBcd(static_cast<uint8_t>(t->tm_mon + 1));  // 1-based month
    // Years: BCD relative to 2000; tm_year is relative to 1900
    raw[6] = decToBcd(static_cast<uint8_t>(t->tm_year - 100));
    return writeRegs(SECONDS, raw, 7);
}

// ---------------------------------------------------------------------------
// Non-volatile user registers
// ---------------------------------------------------------------------------

bool RV3129::getNVBytes(uint8_t &byte0, uint8_t &byte1)
{
    uint8_t buf[2];
    if (!readRegs(EE_USER0, buf, 2)) {
        return false;
    }
    byte0 = buf[0];
    byte1 = buf[1];
    return true;
}

bool RV3129::setNVBytes(uint8_t byte0, uint8_t byte1)
{
    // Write byte0 first – each write triggers its own EEPROM cycle
    if (!writeEEPROMReg(EE_USER0, byte0)) {
        return false;
    }
    return writeEEPROMReg(EE_USER1, byte1);
}

// ---------------------------------------------------------------------------
// Alarm
// ---------------------------------------------------------------------------

bool RV3129::setAlarm(time_t alarmTime)
{
    struct tm *t = gmtime(&alarmTime);
    if (!t) {
        return false;
    }

    // Write the 7 alarm registers (0x10..0x16). AE_x bit = 1 means the field
    // is enabled (included in the match); AE_x = 0 means disabled/ignored.
    // Sec/Min/Hour/Day are matched; Weekday/Month/Year are ignored.
    uint8_t raw[7];
    raw[0] = decToBcd(static_cast<uint8_t>(t->tm_sec))  | ALM_ENABLE; // Second alarm – enabled
    raw[1] = decToBcd(static_cast<uint8_t>(t->tm_min))  | ALM_ENABLE; // Minute alarm – enabled
    raw[2] = decToBcd(static_cast<uint8_t>(t->tm_hour)) | ALM_ENABLE; // Hour alarm   – enabled
    raw[3] = decToBcd(static_cast<uint8_t>(t->tm_mday)) | ALM_ENABLE; // Day alarm    – enabled
    raw[4] = 0;                                                       // Weekday alarm – disabled
    raw[5] = 0;                                                       // Month alarm   – disabled
    raw[6] = 0;                                                       // Year alarm    – disabled

    return writeRegs(ALM_SEC, raw, 7);
}

bool RV3129::getAlarm(time_t &alarmTime)
{
    uint8_t raw[7];
    if (!readRegs(ALM_SEC, raw, 7)) {
        return false;
    }

    // Disabled fields (AE_x=0) fall back to the current time/date's value
    // for that field.
    time_t now = 0;
    if (!getUnixTime(now)) {
        return false;
    }
    struct tm t = *gmtime(&now);
    if (raw[0] & ALM_ENABLE) t.tm_sec  = bcdToDec(raw[0] & 0x7F);
    if (raw[1] & ALM_ENABLE) t.tm_min  = bcdToDec(raw[1] & 0x7F);
    if (raw[2] & ALM_ENABLE) t.tm_hour = bcdToDec(raw[2] & 0x3F);
    if (raw[3] & ALM_ENABLE) t.tm_mday = bcdToDec(raw[3] & 0x3F);
    if (raw[5] & ALM_ENABLE) t.tm_mon  = bcdToDec(raw[5] & 0x1F) - 1;
    if (raw[6] & ALM_ENABLE) t.tm_year = bcdToDec(raw[6] & 0x7F) + 100;
    t.tm_isdst = 0;

    alarmTime = mktime(&t);
    return (alarmTime != static_cast<time_t>(-1));
}

bool RV3129::setAlarmInterrupt(bool enable)
{
    uint8_t reg;
    if (!readReg(CTRL_INT, reg)) {
        return false;
    }
    if (enable) {
        reg |= INT_AIE;
    } else {
        reg &= ~INT_AIE;
    }
    return writeReg(CTRL_INT, reg);
}

bool RV3129::clearAlarmFlag()
{
    uint8_t reg;
    if (!readReg(CTRL_INT_FLAG, reg)) {
        return false;
    }
    reg &= ~FLAG_AF;
    return writeReg(CTRL_INT_FLAG, reg);
}

bool RV3129::isAlarmFired(bool &fired)
{
    uint8_t reg;
    if (!readReg(CTRL_INT_FLAG, reg)) {
        return false;
    }
    fired = (reg & FLAG_AF) != 0;
    return true;
}

// ---------------------------------------------------------------------------
// Interrupt mode
// ---------------------------------------------------------------------------

bool RV3129::setInterruptMode(RV3129IntMode mode, bool enable)
{
    uint8_t reg;
    if (!readReg(CTRL_INT, reg)) {
        return false;
    }
    uint8_t bit = static_cast<uint8_t>(mode);
    if (enable) {
        reg |= bit;
    } else {
        reg &= ~bit;
    }
    return writeReg(CTRL_INT, reg);
}

bool RV3129::getInterruptMode(uint8_t &intReg)
{
    return readReg(CTRL_INT, intReg);
}

bool RV3129::getInterruptFlags(uint8_t &flagReg)
{
    return readReg(CTRL_INT_FLAG, flagReg);
}

bool RV3129::clearInterruptFlags(RV3129IntFlag flags)
{
    uint8_t reg;
    if (!readReg(CTRL_INT_FLAG, reg)) {
        return false;
    }
    reg &= ~static_cast<uint8_t>(flags);
    return writeReg(CTRL_INT_FLAG, reg);
}

// ---------------------------------------------------------------------------
// Periodic countdown timer
// ---------------------------------------------------------------------------

bool RV3129::setTimer(RV3129TimerClk clkSrc, uint16_t count,
                      bool autoReload, bool interrupt)
{
    // Write 16-bit count (little-endian: Timer_Low first)
    uint8_t timerBuf[2] = {
        static_cast<uint8_t>(count & 0xFF),
        static_cast<uint8_t>((count >> 8) & 0xFF)
    };
    if (!writeRegs(TIMER_LOW, timerBuf, 2)) {
        return false;
    }

    // Configure timer interrupt enable
    uint8_t intReg;
    if (!readReg(CTRL_INT, intReg)) {
        return false;
    }
    if (interrupt) {
        intReg |= INT_TIE;
    } else {
        intReg &= ~INT_TIE;
    }
    if (!writeReg(CTRL_INT, intReg)) {
        return false;
    }

    // Configure CTRL1: set TD1/TD0, TAR, TE; preserve other bits
    uint8_t ctrl;
    if (!readReg(CTRL1, ctrl)) {
        return false;
    }
    // Clear timer-related bits
    ctrl &= ~(CTRL1_TD1 | CTRL1_TD0 | CTRL1_TAR | CTRL1_TE);
    // Apply new settings
    ctrl |= static_cast<uint8_t>(clkSrc);
    if (autoReload) {
        ctrl |= CTRL1_TAR;
    }
    ctrl |= CTRL1_TE;

    return writeReg(CTRL1, ctrl);
}

bool RV3129::stopTimer()
{
    uint8_t ctrl;
    if (!readReg(CTRL1, ctrl)) {
        return false;
    }
    ctrl &= ~CTRL1_TE;
    return writeReg(CTRL1, ctrl);
}

bool RV3129::getTimer(RV3129TimerClk &clkSrc, uint16_t &count, bool &enabled)
{
    uint8_t ctrl;
    if (!readReg(CTRL1, ctrl)) {
        return false;
    }
    enabled = (ctrl & CTRL1_TE) != 0;
    clkSrc  = static_cast<RV3129TimerClk>(ctrl & (CTRL1_TD1 | CTRL1_TD0));

    uint8_t timerBuf[2];
    if (!readRegs(TIMER_LOW, timerBuf, 2)) {
        return false;
    }
    count = static_cast<uint16_t>(timerBuf[0]) |
            (static_cast<uint16_t>(timerBuf[1]) << 8);
    return true;
}

// ---------------------------------------------------------------------------
// CLKOUT frequency  (EEPROM register 0x30)
// ---------------------------------------------------------------------------

bool RV3129::setClkoutFreq(RV3129ClkFreq freq)
{
    // *** This register is EEPROM – the write cycle takes ~14 ms ***
    // Read current value of EE_CTRL to preserve other bits (trickle, ThE, ThP)
    uint8_t eeCtrl;
    if (!readEEPROMReg(EE_CTRL, eeCtrl)) {
        return false;
    }

    // Mask out old FD bits and apply new ones
    eeCtrl &= ~(EECTRL_FD1 | EECTRL_FD0);
    eeCtrl |= static_cast<uint8_t>(freq);

    // Write back to EEPROM (blocks until EEbusy clears)
    if (!writeEEPROMReg(EE_CTRL, eeCtrl)) {
        return false;
    }

    // Ensure CTRL1 has Clk/Int = 1 (CLKOUT function, not INT function on pin)
    uint8_t ctrl1;
    if (!readReg(CTRL1, ctrl1)) {
        return false;
    }
    ctrl1 |= CTRL1_CLK_INT;
    return writeReg(CTRL1, ctrl1);
}

bool RV3129::getClkoutFreq(RV3129ClkFreq &freq)
{
    uint8_t eeCtrl;
    if (!readEEPROMReg(EE_CTRL, eeCtrl)) {
        return false;
    }
    freq = static_cast<RV3129ClkFreq>(eeCtrl & (EECTRL_FD1 | EECTRL_FD0));
    return true;
}

bool RV3129::setClkoutEnabled(bool enable)
{
    uint8_t ctrl1;
    if (!readReg(CTRL1, ctrl1)) {
        return false;
    }
    if (enable) {
        ctrl1 |= CTRL1_CLK_INT;   // bit7=1 → CLKOUT function
    } else {
        ctrl1 &= ~CTRL1_CLK_INT;  // bit7=0 → INT function on CLKOUT pin
    }
    return writeReg(CTRL1, ctrl1);
}

// ---------------------------------------------------------------------------
// Status helpers
// ---------------------------------------------------------------------------

bool RV3129::isEEPROMBusy(bool &busy)
{
    uint8_t status;
    if (!readReg(CTRL_STATUS, status)) {
        return false;
    }
    busy = (status & STATUS_EEBUSY) != 0;
    return true;
}

bool RV3129::checkAndClearPOR(bool &pon)
{
    uint8_t status;
    if (!readReg(CTRL_STATUS, status)) {
        return false;
    }
    pon = (status & STATUS_PON) != 0;
    if (pon) {
        // Clear PON flag
        writeReg(CTRL_STATUS, status & ~STATUS_PON);
    }
    return true;
}

bool RV3129::systemReset()
{
    return writeReg(CTRL_RESET, RESET_SYSR);
}

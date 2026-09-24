#ifndef AD5142A_H
#define AD5142A_H
#ifdef __cplusplus
/**
 * @file    AD5142A.h
 * @brief   Mbed OS driver for the AD5142A dual-channel I2C digital potentiometer
 *
 * Device summary
 * --------------
 *   - Dual independent channels (RDAC1, RDAC2)
 *   - 256 wiper positions (8-bit, codes 0–255)
 *   - Nonvolatile EEPROM: wiper position auto-restored on power-up
 *   - I2C fast-mode up to 400 kHz
 *   - R_AB variants: 10 kΩ or 100 kΩ
 *   - R_W (wiper contact): 55–125 Ω typ (10 kΩ variant)
 *
 * I2C address
 * -----------
 *   7-bit address = 0b0101_A1_A0  (A1, A0 set by hardware pins)
 *   Write address (8-bit, R/W=0) = (0x28 | (A1<<2) | (A0<<1)) << 1
 *   But wait: the AD5142A datasheet uses address bits [A1:A0] directly, so
 *   the 7-bit address field is 0b01_01_A1_A0.
 *
 * I2C protocol (16-bit frame)
 * ---------------------------
 *   Byte 1 (command byte): [CMD3 CMD2 CMD1 CMD0 | A3 A2 A1 A0]
 *   Byte 2 (data byte)   : [D7  D6   D5   D4    | D3   D2  D1 D0]
 *
 *   Channel select is a single bit, A0 (A3:A1 = 0 for RDAC1/RDAC2):
 *     A0=0 = RDAC1    A0=1 = RDAC2
 *
 *   Command codes (CMD3:CMD0), per datasheet Table 10 — note these are the
 *   literal 4-bit values, NOT the datasheet's "Command Number" column labels
 *   (e.g. "Command Number 9" has bit pattern 0b0111, not 0x9):
 *     0x1  (0b0001) = Write to RDAC (scratchpad)
 *     0x3  (0b0011) = Read back contents (RDAC or EEPROM, selected by D1:D0)
 *     0x6  (0b0110) = Copy EEPROM → RDAC (restore)
 *     0x7  (0b0111) = Copy RDAC → EEPROM (store)
 *
 * Read-back
 * ---------
 *   Write the command byte with CMD=0x3 and the A0 bit for the desired
 *   channel, with data byte D1:D0 = 11 to select RDAC (01 = EEPROM), then
 *   issue a repeated-start I2C read for 2 bytes.
 *   The device returns [0 0 0 0 0 0 D9 D8] [D7..D0].  For AD5142A D9:D8=0.
 *
 * EEPROM write timing
 * -------------------
 *   tEEPROM_PROGRAM = 15–50 ms (datasheet Table 4).
 *   This driver uses ACK polling to detect completion (the device holds
 *   SDA low / NACKs during the write cycle) with a hard timeout fallback.
 *
 * Example
 * -------
 * @code
 *   AD5142A pot(I2C_SDA, I2C_SCL, 0, 0);   // A1=0, A0=0
 *
 *   // Channel 1
 *   pot.setWiper(AD5142A::CH1, 128);
 *   pot.saveToEEPROM(AD5142A::CH1);
 *
 *   uint8_t pos;
 *   pot.getWiper(AD5142A::CH1, pos);
 * @endcode
 */

#include "mbed.h"
#include <cstdint>

class AD5142A {
public:
    // -------------------------------------------------------------------------
    // Channel selector
    // -------------------------------------------------------------------------
    enum Channel : uint8_t {
        CH1 = 0x00,   // RDAC1 / EEMEM1  (ADDR1:ADDR0 = 00)
        CH2 = 0x01    // RDAC2 / EEMEM2  (ADDR1:ADDR0 = 01)
    };

    static constexpr uint8_t CODE_MIN = 0;
    static constexpr uint8_t CODE_MAX = 255;

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------
    /**
     * @param sda   SDA pin
     * @param scl   SCL pin
     * @param a1    State of A1 address pin (0 or 1)
     * @param a0    State of A0 address pin (0 or 1)
     * @param freq  I2C bus frequency in Hz (default 400 kHz)
     */
    AD5142A(PinName sda, PinName scl,
            int a1 = 0, int a0 = 0,
            int freq = 400000);

    // -------------------------------------------------------------------------
    // Core API
    // -------------------------------------------------------------------------

    /**
     * @brief Write a wiper position directly to the volatile RDAC register.
     *
     * Takes effect immediately. The value is NOT automatically saved to EEPROM.
     * Call saveToEEPROM() to make it persistent.
     *
     * @param ch        Channel (CH1 or CH2)
     * @param position  Wiper code 0–255
     * @return true on success, false on I2C error
     */
    bool setWiper(Channel ch, uint8_t position);

    /**
     * @brief Read back the current RDAC register for the given channel.
     *
     * @param ch        Channel (CH1 or CH2)
     * @param position  Filled with the current wiper code (0–255)
     * @return true on success, false on I2C error
     */
    bool getWiper(Channel ch, uint8_t &position);

    /**
     * @brief Copy the current RDAC register to the EEPROM for the given channel.
     *
     * The value stored is automatically loaded back into the RDAC at power-up
     * or after a hardware RESET.
     *
     * This call blocks until the NVM write completes (ACK polling with a 60 ms
     * timeout fallback).  The device NACKs all I2C commands while writing.
     *
     * @param ch  Channel (CH1 or CH2)
     * @return true on success, false on I2C error or timeout
     */
    bool saveToEEPROM(Channel ch);

    /**
     * @brief Restore the EEPROM value into the RDAC register for a channel.
     *
     * Equivalent to a software power-up restore.  Useful to discard unsaved
     * RDAC changes.
     *
     * @param ch  Channel (CH1 or CH2)
     * @return true on success, false on I2C error
     */
    bool restoreFromEEPROM(Channel ch);

private:
    I2C  _i2c;
    int  _addr;   // 8-bit write address (R/W bit = 0)

    // Command nibbles (CMD3:CMD0), per datasheet Table 10
    static constexpr uint8_t CMD_WRITE_RDAC    = 0x1;
    static constexpr uint8_t CMD_READ          = 0x3;
    static constexpr uint8_t CMD_RESTORE_RDAC  = 0x6;
    static constexpr uint8_t CMD_STORE_EEPROM  = 0x7;

    // EEPROM ACK polling
    static constexpr int     EEPROM_POLL_INTERVAL_MS = 2;
    static constexpr int     EEPROM_TIMEOUT_MS       = 60;

    /**
     * @brief Build the command byte.
     * Byte layout: [CMD3 CMD2 CMD1 CMD0 | A3 A2 A1 A0]
     *
     * @param cmd   4-bit command nibble
     * @param ch    Channel selector (contributes A0; A3:A1 = 0)
     * @return      Command byte
     */
    static uint8_t makeCmd(uint8_t cmd, Channel ch)
    {
        return static_cast<uint8_t>((cmd << 4) | (ch & 0x01));
    }

    /**
     * @brief Perform ACK polling to wait for EEPROM write completion.
     *
     * The AD5142A NACKs (does not acknowledge) all I2C transactions while the
     * internal NVM write cycle is in progress.  Poll the write address until
     * an ACK is received or the timeout expires.
     *
     * @return true when ACK received, false on timeout
     */
    bool pollEEPROMReady();
};

class AD5142AMapper {
public:
    // -------------------------------------------------------------------------
    // Chip variants — end-to-end resistance R_AB
    // -------------------------------------------------------------------------
    enum Variant : uint32_t {
        OHM_10K  =  10000,
        OHM_100K = 100000
    };

    static constexpr uint8_t CODE_MIN = 0;
    static constexpr uint8_t CODE_MAX = 255;   // 8-bit, 256 positions

    // Datasheet typical wiper resistances (Table 3)
    static constexpr float R_WIPER_10K_TYP  =  75.0f;  // Ω
    static constexpr float R_WIPER_100K_TYP = 175.0f;  // Ω

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------
    /**
     * @param variant  Chip variant (OHM_10K or OHM_100K).
     * @param r_wiper  Wiper contact resistance in Ω.
     *                 Defaults to the datasheet typical for the chosen variant.
     *                 Pass a measured value for improved accuracy.
     */
    explicit AD5142AMapper(Variant variant, float r_wiper = -1.0f)
        : _r_ab(static_cast<float>(variant))
        , _r_w(r_wiper >= 0.0f ? r_wiper
                                : (variant == OHM_10K ? R_WIPER_10K_TYP
                                                      : R_WIPER_100K_TYP))
    {}

    // -------------------------------------------------------------------------
    // Core conversions
    // -------------------------------------------------------------------------

    /**
     * @brief Convert a wiper code to its nominal W-to-B terminal resistance.
     *
     * R_WB = (D / 255) * R_AB + R_W
     *
     * @param code  Wiper position 0–255; clamped to valid range.
     * @return      Resistance in Ω.
     */
    float codeToResistance(uint8_t code) const
    {
        return (static_cast<float>(code) / static_cast<float>(CODE_MAX))
               * _r_ab + _r_w;
    }

    /**
     * @brief Convert a target resistance to the nearest achievable wiper code.
     *
     * Inverts R_WB = (D/255)*R_AB + R_W  →  D = round((R - R_W)/R_AB * 255)
     *
     * Values below R_W (the wiper floor) clamp to code 0.
     * Values above R_AB + R_W clamp to code 255.
     *
     * @param resistance_ohm  Target resistance in Ω.
     * @return                Nearest wiper code (0–255).
     */
    uint8_t resistanceToCode(float resistance_ohm) const
    {
        float ideal = (resistance_ohm - _r_w) / _r_ab
                      * static_cast<float>(CODE_MAX);
        int rounded = static_cast<int>(ideal + 0.5f);
        if (rounded < 0)         return CODE_MIN;
        if (rounded > CODE_MAX)  return CODE_MAX;
        return static_cast<uint8_t>(rounded);
    }

    /**
     * @brief Quantisation error for a requested resistance.
     *
     * error = codeToResistance(resistanceToCode(R)) - R
     * Positive → actual is higher than requested.
     *
     * @param resistance_ohm  Desired resistance in Ω.
     * @return                Error in Ω.
     */
    float quantisationError(float resistance_ohm) const
    {
        return codeToResistance(resistanceToCode(resistance_ohm))
               - resistance_ohm;
    }

    // -------------------------------------------------------------------------
    // Accessors / diagnostic helpers
    // -------------------------------------------------------------------------

    /** @return End-to-end resistance R_AB in Ω. */
    float getRab()            const { return _r_ab; }

    /** @return Wiper contact resistance R_W in Ω. */
    float getRwiper()         const { return _r_w; }

    /** @return Minimum achievable resistance (code=0): R_W. */
    float getMinResistance()  const { return _r_w; }

    /** @return Maximum achievable resistance (code=255): R_AB + R_W. */
    float getMaxResistance()  const { return _r_ab + _r_w; }

    /** @return Resistance change per LSB step: R_AB / 255 (Ω). */
    float getLSBResistance()  const
    {
        return _r_ab / static_cast<float>(CODE_MAX);
    }

private:
    float _r_ab;
    float _r_w;
};

#endif //__cplusplus
#endif // AD5142A_H

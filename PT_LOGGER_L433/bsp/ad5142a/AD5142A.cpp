#include "AD5142A.h"

// =============================================================================
//  Constructor
// =============================================================================
AD5142A::AD5142A(PinName sda, PinName scl, int a1, int a0, int freq)
    : _i2c(sda, scl)
{
    _i2c.frequency(freq);

    // 7-bit address = 0b01_01_A1_A0  →  0x28 | (a1<<1) | a0
    // 8-bit write address (R/W = 0) = 7-bit << 1
    _addr = static_cast<int>((0x28 | ((a1 & 1) << 1) | (a0 & 1))) << 1;
    _addr = static_cast<int>((0x2F)) << 1;

}

// =============================================================================
//  setWiper
// =============================================================================
bool AD5142A::setWiper(Channel ch, uint8_t position)
{
    char buf[2];
    buf[0] = static_cast<char>(makeCmd(CMD_WRITE_RDAC, ch));
    buf[1] = static_cast<char>(position);   // D7:D0, D9:D8 = 0 (in cmd byte)

    return (_i2c.write(_addr, buf, 2) == 0);
}

// =============================================================================
//  getWiper
// =============================================================================
bool AD5142A::getWiper(Channel ch, uint8_t &position)
{
    // Command 3 (0b0011) = "Read back contents". Data byte's D1:D0 select
    // the register to read: 11 = RDAC, 01 = EEPROM. We always want RDAC.
    // Per datasheet Figure 40, the readback instruction is a complete,
    // STOP-terminated write, followed by a separate START'd read of a
    // single data byte (D7:D0) — no repeated start, no second byte;
    // the AD5122A/AD5142A do not support repeat readback.
    char cmd[2] = { static_cast<char>(makeCmd(CMD_READ, ch)), 0x03 };

    if (_i2c.write(_addr, cmd, 2) != 0) {
        return false;
    }

    char data = 0;
    int  read_addr = _addr | 0x01;
    if (_i2c.read(read_addr, &data, 1) != 0) {
        return false;
    }

    position = static_cast<uint8_t>(data);
    return true;
}

// =============================================================================
//  saveToEEPROM
// =============================================================================
bool AD5142A::saveToEEPROM(Channel ch)
{
    // CMD_STORE_EEPROM copies RDAC → EEPROM for the selected channel.
    // The data byte value is a don't-care for this command.
    char buf[2];
    buf[0] = static_cast<char>(makeCmd(CMD_STORE_EEPROM, ch));
    buf[1] = 0x00;

    if (_i2c.write(_addr, buf, 2) != 0) {
        return false;
    }

    // Wait for NVM write to complete via ACK polling.
    // The device NACKs all bus activity until the write cycle finishes.
    return pollEEPROMReady();
}

// =============================================================================
//  restoreFromEEPROM
// =============================================================================
bool AD5142A::restoreFromEEPROM(Channel ch)
{
    // CMD_RESTORE_RDAC copies EEPROM → RDAC for the selected channel.
    // Data byte is a don't-care.
    char buf[2];
    buf[0] = static_cast<char>(makeCmd(CMD_RESTORE_RDAC, ch));
    buf[1] = 0x00;

    return (_i2c.write(_addr, buf, 2) == 0);
}

// =============================================================================
//  pollEEPROMReady  (private)
// =============================================================================
bool AD5142A::pollEEPROMReady()
{
    // During an EEPROM write cycle the AD5142A NACKs its I2C address.
    // Poll by attempting a write to the device until it ACKs (returns 0).
    // A single dummy byte write is sufficient to probe for ACK.
    const int max_polls = EEPROM_TIMEOUT_MS / EEPROM_POLL_INTERVAL_MS;

    for (int i = 0; i < max_polls; ++i) {
        ThisThread::sleep_for(std::chrono::milliseconds(EEPROM_POLL_INTERVAL_MS));

        // _i2c.write() with length=0 sends START + address + STOP.
        // Returns 0 if device ACKs (write cycle complete).
        if (_i2c.write(_addr, nullptr, 0) == 0) {
            return true;
        }
    }

    // Timeout — NVM write took longer than EEPROM_TIMEOUT_MS
    return false;
}

#ifndef MAX31888_H
#define MAX31888_H

/**
 * @file    MAX31888.h
 * @brief   Mbed OS driver for the MAX31888 1-Wire precision temperature sensor
 *
 * Device summary (datasheet Rev 1, 8/2023)
 * -----------------------------------------
 *   - ±0.25°C accuracy, -20°C to +105°C
 *   - 16-bit resolution (0.005°C/LSB), configurable 13–16 bit
 *   - 1-Wire interface (uses the provided OneWire class)
 *   - Internal FIFO: up to 32 temperature samples
 *   - High/Low temperature alarm thresholds
 *   - 3 GPIO pins configurable as: digital input/output or alarm output
 *   - Conversion time: 16.5–17.85 ms (16-bit)
 *
 * GPIO usage in this driver  (per datasheet GPIO section)
 * -------------------------------------------------------
 *   trig_pin  (MCU DigitalOut → MAX31888 GPIO1):
 *     GPIO1 configured as mode 0b11 (SPECIAL INPUT).
 *     Driving this line LOW for ≥5 µs initiates an external temperature
 *     conversion — identical to issuing Convert T [0x44] over 1-Wire.
 *     This driver pulses trig_pin LOW then releases it to HIGH.
 *     If trig_pin == NC, triggerConversion() falls back to the 1-Wire
 *     CMD_CONVERT_T command instead.
 *
 *   irq_pin  (MAX31888 GPIO0 → MCU InterruptIn):
 *     GPIO0 configured as mode 0b11 (SPECIAL OUTPUT / interrupt).
 *     The device drives GPIO0 LOW when any enabled interrupt flag in the
 *     INTERRUPT_STATUS register is set.  Enabling TEMP_RDY_EN (bit 0 of
 *     INTERRUPT_ENABLE [0x0D]) causes GPIO0 to assert low when a conversion
 *     completes and TEMP_RDY (bit 0 of INTERRUPT_STATUS [0x0C]) is set.
 *     The MCU InterruptIn fires on the falling edge.
 *
 * Wiring summary
 * --------------
 *   DQ      → 1-Wire bus pin (750 Ω pullup to VDD required)
 *   GPIO1   → MCU DigitalOut (trig_pin)   [optional hardware trigger]
 *   GPIO0   → MCU InterruptIn (irq_pin)   [optional TEMP_RDY / alarm IRQ]
 *   CEXT    → 100 nF capacitor to GND
 *
 * Example
 * -------
 * @code
 *   OneWire    ow(p10);
 *   MAX31888   sensor(ow, p11, p12);   // trig=p11 (→GPIO1), irq=p12 (←GPIO0)
 *
 *   sensor.begin();
 *   sensor.setAlarmHigh(40.0f);
 *   sensor.setAlarmLow(-10.0f);
 *
 *   sensor.triggerConversion();        // starts measurement
 *   ThisThread::sleep_for(20ms);       // wait for conversion
 *   float t;
 *   sensor.readTemperature(t);
 * @endcode
 *
 * Register map (from datasheet)
 * ------------------------------
 *   0x00  STATUS
 *   0x01  FIFO_WR_PTR
 *   0x02  FIFO_OVF_CNT
 *   0x03  FIFO_RD_PTR
 *   0x04  FIFO_FILL_COUNT
 *   0x08  FIFO_DATA
 *   0x09  FIFO_CONFIGURATION
 *   0x0C  INT_STATUS
 *   0x0D  INT_EN
 *   0x0E  FIFO_INT_STATUS
 *   0x0F  FIFO_INT_EN
 *   0x10  ALARM_HIGH_MSB / 0x11 ALARM_HIGH_LSB
 *   0x12  ALARM_LOW_MSB  / 0x13 ALARM_LOW_LSB
 *   0x14  TEMP_SENSOR_SETUP
 *   0x20  GPIO_SETUP
 *   0x21  GPIO_CONFIG
 */

#include "mbed.h"
#include "OneWire.h"

class MAX31888 {
public:
    // -------------------------------------------------------------------------
    // Resolution options (TEMP_SENSOR_SETUP register bits [1:0])
    // -------------------------------------------------------------------------
    enum Resolution : uint8_t {
        RES_13BIT = 0x00,   // 0.0625°C / LSB,  ~1 ms conversion
        RES_14BIT = 0x01,   // 0.03125°C / LSB, ~2 ms
        RES_15BIT = 0x02,   // 0.01563°C / LSB, ~4 ms
        RES_16BIT = 0x03    // 0.005°C / LSB,   ~18 ms  (default)
    };

    // -------------------------------------------------------------------------
    // GPIO mode (per-pin, 2-bit field in GPIO_SETUP [0x20])
    //   GPIO0=[1:0], GPIO1=[3:2], GPIO2=[5:4]
    //
    //   0b00 (INPUT)   — standard digital input; read via GPIO_CONFIG [0x21]
    //   0b01 (OUTPUT)  — standard digital output; write via GPIO_CONFIG [0x21]
    //   0b10 (ALARM)   — open-drain alarm output: driven low on alarm condition
    //   0b11 (SPECIAL) — special function:
    //                      GPIO0: interrupt output (driven low when INT_STATUS bits set)
    //                      GPIO1: conversion trigger input (falling edge starts Convert T)
    //                      GPIO2: not defined for special function
    // -------------------------------------------------------------------------
    enum GPIOMode : uint8_t {
        MAX_GPIO_MODE_INPUT   = 0x00,
        MAX_GPIO_MODE_OUTPUT  = 0x01,
        MAX_GPIO_MODE_ALARM   = 0x02,
        MAX_GPIO_MODE_SPECIAL = 0x03    // IRQ out (GPIO0) / trigger in (GPIO1)
    };

    enum GPIOPin : uint8_t {
        GPIO0 = 0,
        GPIO1 = 1,
        GPIO2 = 2
    };

    // -------------------------------------------------------------------------
    // Error codes returned by most methods
    // -------------------------------------------------------------------------
    enum Error : int8_t {
        OK             =  0,
        ERR_NO_DEVICE  = -1,   // 1-Wire reset: no presence pulse
        ERR_CRC        = -2,   // CRC mismatch on received data
        ERR_FIFO_EMPTY = -3,   // FIFO has no data ready
        ERR_TIMEOUT    = -4    // Conversion did not complete in time
    };

    // Temperature resolution in °C per raw LSB (16-bit mode)
    static constexpr float LSB_16BIT = 0.005f;

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------
    /**
     * @param ow        Reference to an initialised OneWire instance on the DQ pin.
     * @param trig_pin  MCU DigitalOut pin wired to MAX31888 GPIO1 (conversion trigger
     *                  input, mode SPECIAL 0b11).  Pulling this low ≥5 µs starts
     *                  a conversion.  Pass NC to use 1-Wire CMD_CONVERT_T instead.
     * @param irq_pin   MCU InterruptIn pin wired to MAX31888 GPIO0 (interrupt output,
     *                  mode SPECIAL 0b11).  Driven low by sensor when an enabled
     *                  interrupt flag fires (e.g. TEMP_RDY).  Pass NC if not used.
     */
    MAX31888(OneWire &ow,
             PinName  trig_pin = NC,
             PinName  irq_pin  = NC);

    // -------------------------------------------------------------------------
    // Initialisation
    // -------------------------------------------------------------------------
    /**
     * @brief Initialise the sensor: verify presence, set resolution, configure
     *        GPIO pins, and flush the FIFO.
     *
     * @param res  Conversion resolution (default 13-bit).
     * @return OK or an Error code.
     */
    Error init(Resolution res = RES_13BIT, uint8_t rom[8] = nullptr);

    // -------------------------------------------------------------------------
    // Temperature measurement
    // -------------------------------------------------------------------------
    /**
     * @brief Start a temperature conversion.
     *
     * If trig_pin was provided: pulses GPIO1 LOW for ≥10 µs (hardware trigger,
     * mode SPECIAL).  The device starts the conversion autonomously.
     *
     * If trig_pin == NC: issues the 1-Wire Convert T [0x44] command instead.
     *
     * Returns immediately — call waitForConversion() (polling) or rely on the
     * irq_pin TEMP_RDY interrupt to know when data is ready.
     *
     * @return OK or ERR_NO_DEVICE.
     */
    Error triggerConversion();

    /**
     * @brief Block until conversion is complete or timeout expires.
     *
     * Polls the 1-Wire bus (read time slot returns 1 when done).
     *
     * @param timeout_ms  Maximum wait in milliseconds (default 25 ms).
     * @return OK or ERR_TIMEOUT.
     */
    Error waitForConversion(uint32_t timeout_ms = 25);

    /**
     * @brief Read the most recent temperature from the FIFO.
     *
     * Reads 2 data bytes + 2 CRC16 bytes, verifies CRC, converts to °C.
     *
     * @param temperature_c  Filled with temperature in °C on success.
     * @return OK, ERR_NO_DEVICE, ERR_FIFO_EMPTY, or ERR_CRC.
     */
    Error readTemperature(float &temperature_c);

    /**
     * @brief Trigger, wait, and read in one blocking call.
     *
     * Convenience wrapper around triggerConversion() + waitForConversion()
     * + readTemperature().
     *
     * @param temperature_c  Filled with temperature in °C on success.
     * @return OK or an Error code.
     */
    Error measureBlocking(float &temperature_c);

    /**
     * @brief Return the typical conversion time in ms for the current resolution.
     */
    uint32_t conversionTimeMs() const;

    // -------------------------------------------------------------------------
    // Alarm thresholds
    // -------------------------------------------------------------------------
    /**
     * @brief Set the high-temperature alarm threshold.
     * @param temp_c  Alarm fires when temperature > temp_c.
     * @return OK or ERR_NO_DEVICE.
     */
    Error setAlarmHigh(float temp_c);

    /**
     * @brief Set the low-temperature alarm threshold.
     * @param temp_c  Alarm fires when temperature < temp_c.
     * @return OK or ERR_NO_DEVICE.
     */
    Error setAlarmLow(float temp_c);

    /**
     * @brief Read the current high-alarm threshold back from the device.
     */
    Error getAlarmHigh(float &temp_c);

    /**
     * @brief Read the current low-alarm threshold back from the device.
     */
    Error getAlarmLow(float &temp_c);

    // -------------------------------------------------------------------------
    // GPIO control (sensor-side GPIO pins)
    // -------------------------------------------------------------------------
    /**
     * @brief Configure one of the sensor's GPIO pins.
     *
     * @param pin   GPIO0, GPIO1, or GPIO2.
     * @param mode  Input, output, or alarm output.
     * @return OK or ERR_NO_DEVICE.
     */
    Error configureGPIO(GPIOPin pin, GPIOMode mode);

    /**
     * @brief Set the output state of a sensor GPIO pin (mode must be OUTPUT).
     * @param pin    GPIO pin to drive.
     * @param value  true = drive high (open-drain released), false = drive low.
     * @return OK or ERR_NO_DEVICE.
     */
    Error writeGPIO(GPIOPin pin, bool value);

    /**
     * @brief Read the current state of a sensor GPIO pin.
     * @param pin    GPIO pin to read.
     * @param value  Filled with current state.
     * @return OK or ERR_NO_DEVICE.
     */
    Error readGPIO(GPIOPin pin, bool &value);

    // -------------------------------------------------------------------------
    // Interrupt / IRQ control
    // -------------------------------------------------------------------------
    /**
     * @brief Enable the TEMP_RDY interrupt and assert GPIO0 when conversion done.
     *
     * Sets TEMP_RDY_EN (bit 0) in INTERRUPT_ENABLE [0x0D] and configures
     * GPIO0 to mode SPECIAL (0b11) so it is driven low by the device when
     * TEMP_RDY fires.  The MCU irq_pin InterruptIn will catch the falling edge.
     *
     * Call this from begin() or any time after.  Only meaningful when irq_pin
     * was provided to the constructor.
     *
     * @return OK or ERR_NO_DEVICE.
     */
    Error enableTempReadyIRQ();

    /**
     * @brief Disable the TEMP_RDY interrupt (clears TEMP_RDY_EN bit).
     * @return OK or ERR_NO_DEVICE.
     */
    Error disableTempReadyIRQ();

    /**
     * @brief Register a callback invoked when the irq_pin falls (TEMP_RDY or alarm).
     *
     * The callback runs in ISR context — set a flag and handle in the main thread.
     *
     * @param cb  Callback<void()> to invoke on interrupt.
     */
    void attachIRQCallback(Callback<void()> cb);

    /**
     * @brief Detach the IRQ callback.
     */
    void detachIRQCallback();

    // -------------------------------------------------------------------------
    // FIFO helpers
    // -------------------------------------------------------------------------
    /**
     * @brief Return the number of unread samples in the FIFO.
     * @param count  Filled with sample count (0–32) on success.
     * @return OK or ERR_NO_DEVICE.
     */
    Error getFIFOCount(uint8_t &count);

    /**
     * @brief Flush (reset) the FIFO by resetting write and read pointers.
     * @return OK or ERR_NO_DEVICE.
     */
    Error flushFIFO();

    // -------------------------------------------------------------------------
    // Device utility
    // -------------------------------------------------------------------------
    /**
     * @brief Read the 64-bit ROM code (device serial number).
     * @param rom  8-byte array filled with ROM code (LSB first).
     * @return OK or ERR_NO_DEVICE.
     */
    Error readROM(uint8_t rom[8]);

    /**
     * @brief Issue a soft-reset command over 1-Wire.
     * @return OK or ERR_NO_DEVICE.
     */
    Error softReset();

private:
    OneWire         &_ow;
    DigitalOut      *_trig;       // MCU output → sensor GPIO1 (trigger input)
    InterruptIn     *_irq;        // MCU IRQ input ← sensor GPIO0 (interrupt output)
    Callback<void()> _irq_cb;
    Resolution       _res;
    uint8_t          _rom[8];
    bool             _single;
    // INTERRUPT_ENABLE [0x0D] bit positions
    static constexpr uint8_t INT_EN_TEMP_RDY   = (1 << 0);  // TEMP_RDY_EN
    static constexpr uint8_t INT_EN_TEMP_HI    = (1 << 1);  // TEMP_HI_EN  (alarm high)
    static constexpr uint8_t INT_EN_TEMP_LO    = (1 << 2);  // TEMP_LO_EN  (alarm low)

    // 1-Wire ROM commands
    static constexpr uint8_t ROM_SKIP      = 0xCC;
    static constexpr uint8_t ROM_MATCH     = 0x55;
    static constexpr uint8_t ROM_READ      = 0x33;
    static constexpr uint8_t ROM_SEARCH    = 0xF0;

    // MAX31888 function commands
    static constexpr uint8_t CMD_CONVERT_T      = 0x44;
    static constexpr uint8_t CMD_WRITE_REG      = 0xCC;  // same byte as ROM SKIP — context differs
    static constexpr uint8_t CMD_READ_REG       = 0x33;  // same byte as ROM READ — context differs
    static constexpr uint8_t CMD_SOFT_RESET     = 0x82;

    // Register addresses
    static constexpr uint8_t REG_STATUS         = 0x00;
    static constexpr uint8_t REG_FIFO_WR_PTR   = 0x01;
    static constexpr uint8_t REG_FIFO_OVF_CNT  = 0x02;
    static constexpr uint8_t REG_FIFO_RD_PTR   = 0x03;
    static constexpr uint8_t REG_FIFO_FILL     = 0x04;
    static constexpr uint8_t REG_FIFO_DATA     = 0x08;
    static constexpr uint8_t REG_FIFO_CFG      = 0x09;
    static constexpr uint8_t REG_INT_STATUS    = 0x0C;
    static constexpr uint8_t REG_INT_EN        = 0x0D;
    static constexpr uint8_t REG_ALARM_HI_MSB  = 0x10;
    static constexpr uint8_t REG_ALARM_HI_LSB  = 0x11;
    static constexpr uint8_t REG_ALARM_LO_MSB  = 0x12;
    static constexpr uint8_t REG_ALARM_LO_LSB  = 0x13;
    static constexpr uint8_t REG_TEMP_SETUP    = 0x14;
    static constexpr uint8_t REG_GPIO_SETUP    = 0x20;
    static constexpr uint8_t REG_GPIO_CONFIG   = 0x21;

    // ---- Private helpers ----

    /** Send reset + Skip ROM to address the (single) device on bus. */
    bool _select();

    /**
     * Write one register value.
     * Transaction: Reset | Skip ROM | CMD_WRITE_REG | reg_addr | value | CRC16
     */
    Error _writeReg(uint8_t reg, uint8_t value);

    /**
     * Read N bytes starting at reg_addr.
     * Transaction: Reset | Skip ROM | CMD_READ_REG | reg_addr | N | read N+2 bytes (data + CRC16)
     */
    Error _readReg(uint8_t reg, uint8_t *buf, uint8_t len);

    /** Convert raw 16-bit signed temperature word to float °C. */
    static float _rawToFloat(int16_t raw, Resolution res);

    /** Convert float °C to raw 16-bit word for alarm register. */
    static int16_t _floatToRaw(float temp_c, Resolution res);

    /** ISR trampoline for irq_pin. */
    void _onIRQ();
};

#endif // MAX31888_H

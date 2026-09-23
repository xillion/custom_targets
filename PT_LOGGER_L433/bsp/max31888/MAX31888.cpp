#include "MAX31888.h"

// =============================================================================
//  Constructor
// =============================================================================
MAX31888::MAX31888(OneWire &ow, PinName trig_pin, PinName irq_pin)
    : _ow(ow)
    , _trig(nullptr)
    , _irq(nullptr)
    , _res(RES_16BIT)
{
    if (trig_pin != NC) {
        _trig = new DigitalOut(trig_pin, 1);   // idle high (open-drain convention)
    }
    if (irq_pin != NC) {
        _irq = new InterruptIn(irq_pin);
        _irq->mode(PullUp);                    // alarm output is open-drain
    }
}

// =============================================================================
//  init
// =============================================================================
MAX31888::Error MAX31888::init(Resolution res, uint8_t rom[8])
{
    _res = res;
    _single = true;
    if(rom != nullptr){
        memcpy(_rom, rom, 8);
        _single = false;
    }


    // Verify device is present
    if (!_ow.reset()) {
        return ERR_NO_DEVICE;
    }

    // Soft-reset to clear any stale state
    Error err = softReset();
    if (err != OK) return err;

    // Allow device to restart
    ThisThread::sleep_for(2ms);

    // Set resolution
    err = _writeReg(REG_TEMP_SETUP, static_cast<uint8_t>(res));
    debug("MAX31888 init: writeReg TEMP_SETUP -> %d\r\n", static_cast<int>(err));
    if (err != OK) return err;

    // Configure GPIO pins according to datasheet GPIO section:
    //   GPIO0 = SPECIAL (0b11) → interrupt output (driven low when INT_STATUS bits set)
    //   GPIO1 = SPECIAL (0b11) → conversion trigger input (falling edge starts Convert T)
    // Read-modify-write GPIO_SETUP register.
    uint8_t gpio_setup = 0;
    err = _readReg(REG_GPIO_SETUP, &gpio_setup, 1);
    debug("MAX31888 init: readReg GPIO_SETUP -> %d (0x%02X)\r\n",
          static_cast<int>(err), gpio_setup);
    if (err != OK) return err;

    // GPIO0 field [1:0] = 0b11, GPIO1 field [3:2] = 0b11
    gpio_setup &= 0xF0;                         // clear [3:0]
    if (_irq)  gpio_setup |= (0x03 << 0);       // GPIO0 = SPECIAL output (IRQ)
    if (_trig) gpio_setup |= (0x03 << 2);       // GPIO1 = SPECIAL input  (trigger)

    err = _writeReg(REG_GPIO_SETUP, gpio_setup);
    debug("MAX31888 init: writeReg GPIO_SETUP -> %d\r\n", static_cast<int>(err));
    if (err != OK) return err;

    // Enable TEMP_RDY interrupt so GPIO0 asserts when conversion finishes
    if (_irq) {
        err = enableTempReadyIRQ();
        if (err != OK) return err;
    }

    // Flush FIFO
    err = flushFIFO();
    return err;
}

// =============================================================================
//  triggerConversion
// =============================================================================
MAX31888::Error MAX31888::triggerConversion()
{
    if (_trig) {
        // Hardware trigger via GPIO1 (SPECIAL input mode, 0b11).
        // Datasheet: driving GPIO1 low ≥5 µs initiates an external conversion.
        // The MCU DigitalOut is idle HIGH (open-drain released).
        *_trig = 0;
        wait_us(10);     // hold low for 10 µs (> 5 µs minimum)
        *_trig = 1;      // release
        return OK;
    }

    // Fallback: no trig_pin wired — use 1-Wire Convert T [0x44] command.
    if (!_ow.reset()) return ERR_NO_DEVICE;
    _ow.skip();

    uint8_t cmd = CMD_CONVERT_T;
    _ow.write(CMD_CONVERT_T, true);

    // Datasheet: master RX inverted CRC-16 of the command
    uint8_t crc_rx[2] = { _ow.read(), _ow.read() };
    if(!_ow.check_crc16(&cmd, 1, crc_rx)){
        return ERR_CRC;
    }
    // Keep strong pullup for the conversion period (parasite power)
    _ow.write_bit(1);
    return OK;
}

// =============================================================================
//  readTemperature
// =============================================================================
MAX31888::Error MAX31888::readTemperature(float &temperature_c)
{
    // Check FIFO has data
    uint8_t fill = 0;
    Error   err  = getFIFOCount(fill);
    if (err != OK) return err;
    if (fill == 0) return ERR_FIFO_EMPTY;

    // Read 2 data bytes + 2 CRC16 bytes from FIFO_DATA register
    // Transaction: Reset | Skip ROM | Read Register [0x33] | addr | count | data...
    uint8_t buf[4] = {0};
    err = _readReg(REG_FIFO_DATA, buf, 2);   // _readReg appends CRC16 bytes
    if (err != OK) return err;

    // buf[0] = data MSB, buf[1] = data LSB, buf[2:3] = CRC16 (checked in _readReg)
    int16_t raw = static_cast<int16_t>((buf[0] << 8) | buf[1]);
    temperature_c = _rawToFloat(raw, _res);

    return OK;
}

// =============================================================================
//  measureBlocking
// =============================================================================
MAX31888::Error MAX31888::measureBlocking(float &temperature_c)
{
    uint8_t data[10]; // 3B command, 32x2B FIFO, 2B CRC + margin
    int16_t raw;

    if (!_ow.reset()) return ERR_NO_DEVICE;
    _ow.write(ROM_SKIP, 1);
    _ow.write(CMD_CONVERT_T, 1);        // start conversion, with parasite power on at the end
    data[0]=CMD_CONVERT_T;
    _ow.read_bytes(&data[1], 2); // 2 bytes of CRC 16
    _ow.write_bit(1); // sets strong pullup on DQ line
    ThisThread::sleep_for(conversionTimeMs() * 1ms);// in case of parasite supply, 17.5ms is maximum conversion time for MAX31888
    _ow.depower();

    if(!_ow.check_crc16(data,1,&data[1])){return ERR_CRC;}
    else {
        if (!_ow.reset()) return ERR_NO_DEVICE;
        _ow.write(CMD_WRITE_REG, 1);
        data[0]=CMD_READ_REG;         // Read Register
        data[1]=REG_FIFO_DATA;         // Starting Adddress -> FIFO Data Register   
        data[2]=0x01;         // Length (Bytes -1) -> 2 Bytes
        _ow.write_bytes(data, 3);
        _ow.read_bytes(&data[3], 4); // we need 2 bytes of data and 2 bytes of CRC 16
        if(!_ow.check_crc16(data,5,&data[5])){ 
            return ERR_CRC;
        }
    }
    raw = (data[3] << 8) | data[4]; 
    temperature_c = raw*0.005;
    return OK;
}

// =============================================================================
//  conversionTimeMs
// =============================================================================
uint32_t MAX31888::conversionTimeMs() const
{
    switch (_res) {
        case RES_13BIT: return  2;
        case RES_14BIT: return  4;
        case RES_15BIT: return  9;
        case RES_16BIT:
        default:        return 20;   // datasheet max 17.85 ms + margin
    }
}

// =============================================================================
//  setAlarmHigh / setAlarmLow
// =============================================================================
MAX31888::Error MAX31888::setAlarmHigh(float temp_c)
{
    int16_t raw = _floatToRaw(temp_c, _res);
    Error err = _writeReg(REG_ALARM_HI_MSB, static_cast<uint8_t>(raw >> 8));
    if (err != OK) return err;
    return _writeReg(REG_ALARM_HI_LSB, static_cast<uint8_t>(raw & 0xFF));
}

MAX31888::Error MAX31888::setAlarmLow(float temp_c)
{
    int16_t raw = _floatToRaw(temp_c, _res);
    Error err = _writeReg(REG_ALARM_LO_MSB, static_cast<uint8_t>(raw >> 8));
    if (err != OK) return err;
    return _writeReg(REG_ALARM_LO_LSB, static_cast<uint8_t>(raw & 0xFF));
}

MAX31888::Error MAX31888::getAlarmHigh(float &temp_c)
{
    uint8_t buf[2];
    Error err = _readReg(REG_ALARM_HI_MSB, buf, 2);
    if (err != OK) return err;
    int16_t raw = static_cast<int16_t>((buf[0] << 8) | buf[1]);
    temp_c = _rawToFloat(raw, _res);
    return OK;
}

MAX31888::Error MAX31888::getAlarmLow(float &temp_c)
{
    uint8_t buf[2];
    Error err = _readReg(REG_ALARM_LO_MSB, buf, 2);
    if (err != OK) return err;
    int16_t raw = static_cast<int16_t>((buf[0] << 8) | buf[1]);
    temp_c = _rawToFloat(raw, _res);
    return OK;
}

// =============================================================================
//  configureGPIO
// =============================================================================
MAX31888::Error MAX31888::configureGPIO(GPIOPin pin, GPIOMode mode)
{
    // Read current GPIO_SETUP register
    uint8_t setup = 0;
    Error err = _readReg(REG_GPIO_SETUP, &setup, 1);
    if (err != OK) return err;

    // Each pin occupies 2 bits: GPIO0=[1:0], GPIO1=[3:2], GPIO2=[5:4]
    uint8_t shift = static_cast<uint8_t>(pin * 2);
    setup &= ~(0x03 << shift);                          // clear the field
    setup |= (static_cast<uint8_t>(mode) << shift);    // write new mode

    return _writeReg(REG_GPIO_SETUP, setup);
}

// =============================================================================
//  writeGPIO / readGPIO
// =============================================================================
MAX31888::Error MAX31888::writeGPIO(GPIOPin pin, bool value)
{
    uint8_t cfg = 0;
    Error err = _readReg(REG_GPIO_CONFIG, &cfg, 1);
    if (err != OK) return err;

    uint8_t bit = static_cast<uint8_t>(1 << pin);
    if (value) cfg |=  bit;
    else       cfg &= ~bit;

    return _writeReg(REG_GPIO_CONFIG, cfg);
}

MAX31888::Error MAX31888::readGPIO(GPIOPin pin, bool &value)
{
    uint8_t cfg = 0;
    Error err = _readReg(REG_GPIO_CONFIG, &cfg, 1);
    if (err != OK) return err;
    value = (cfg >> pin) & 0x01;
    return OK;
}

// =============================================================================
//  enableTempReadyIRQ / disableTempReadyIRQ
// =============================================================================
MAX31888::Error MAX31888::enableTempReadyIRQ()
{
    // Read current INT_EN register, set TEMP_RDY_EN bit (bit 0)
    uint8_t reg = 0;
    Error err = _readReg(REG_INT_EN, &reg, 1);
    if (err != OK) return err;
    reg |= INT_EN_TEMP_RDY;
    return _writeReg(REG_INT_EN, reg);
}

MAX31888::Error MAX31888::disableTempReadyIRQ()
{
    uint8_t reg = 0;
    Error err = _readReg(REG_INT_EN, &reg, 1);
    if (err != OK) return err;
    reg &= ~INT_EN_TEMP_RDY;
    return _writeReg(REG_INT_EN, reg);
}

// =============================================================================
//  attachIRQCallback / detachIRQCallback
// =============================================================================
void MAX31888::attachIRQCallback(Callback<void()> cb)
{
    _irq_cb = cb;
    if (_irq) {
        _irq->fall(callback(this, &MAX31888::_onIRQ));
    }
}

void MAX31888::detachIRQCallback()
{
    if (_irq) {
        _irq->fall(nullptr);
    }
    _irq_cb = nullptr;
}

void MAX31888::_onIRQ()
{
    if (_irq_cb) {
        _irq_cb();
    }
}

// =============================================================================
//  FIFO helpers
// =============================================================================
MAX31888::Error MAX31888::getFIFOCount(uint8_t &count)
{
    return _readReg(REG_FIFO_FILL, &count, 1);
}

MAX31888::Error MAX31888::flushFIFO()
{
    // Reset FIFO: write 0 to WR_PTR, OVF_CNT, RD_PTR
    Error err;
    err = _writeReg(REG_FIFO_WR_PTR,  0x00); if (err != OK) return err;
    err = _writeReg(REG_FIFO_OVF_CNT, 0x00); if (err != OK) return err;
    err = _writeReg(REG_FIFO_RD_PTR,  0x00);
    return err;
}

// =============================================================================
//  readROM
// =============================================================================
MAX31888::Error MAX31888::readROM(uint8_t rom[8])
{
    if (!_ow.reset()) return ERR_NO_DEVICE;
    _ow.write(ROM_READ);
    for (int i = 0; i < 8; ++i) {
        rom[i] = _ow.read();
    }
    // Verify CRC8 of ROM
    if (OneWire::crc8(rom, 7) != rom[7]) {
        return ERR_CRC;
    }
    return OK;
}

// =============================================================================
//  softReset
// =============================================================================
MAX31888::Error MAX31888::softReset()
{
    if (!_ow.reset()) return ERR_NO_DEVICE;
    _ow.skip();
    _ow.write(CMD_SOFT_RESET);

    // Datasheet: master RX inverted CRC-16 (logged for diagnostics only)
    uint8_t crc_rx[2] = { _ow.read(), _ow.read() };
    uint16_t crc_calc = OneWire::crc16(&CMD_SOFT_RESET, 1);
    uint16_t crc_received = static_cast<uint16_t>(crc_rx[0] | (crc_rx[1] << 8));
    debug("MAX31888 softReset: rx=[%02X %02X] invcalc=0x%04X rx=0x%04X\r\n",
          crc_rx[0], crc_rx[1],
          static_cast<unsigned>((crc_calc ^ 0xFFFF) & 0xFFFF),
          static_cast<unsigned>(crc_received));
    return OK;
}

// =============================================================================
//  Private: _select
// =============================================================================
bool MAX31888::_select()
{
    if (!_ow.reset()) return false;
    _ow.skip();          // Skip ROM — single device on bus
    return true;
}

// =============================================================================
//  Private: _writeReg
// =============================================================================
MAX31888::Error MAX31888::_writeReg(uint8_t reg, uint8_t value)
{
    if (!_ow.reset()) return ERR_NO_DEVICE;
    _ow.skip();

    // Write Register [0xCC] | addr | len-1 | data  (datasheet p.19)
    // Single-byte write: length byte = 0x00
    _ow.write(CMD_WRITE_REG);
    _ow.write(reg);
    _ow.write(0x00);
    _ow.write(value);

    // Read and verify the 2-byte inverted CRC-16 the device sends back
    uint8_t crc_rx[2];
    crc_rx[0] = _ow.read();
    crc_rx[1] = _ow.read();

    // CRC-16 over [CMD_WRITE_REG | reg | len | value]
    uint8_t seq[4] = { CMD_WRITE_REG, reg, 0x00, value };
    uint16_t crc_calc = OneWire::crc16(seq, 4);

    uint16_t crc_received = static_cast<uint16_t>(crc_rx[0] | (crc_rx[1] << 8));
    debug("MAX31888 _writeReg: reg=0x%02X val=0x%02X rx=[%02X %02X] invcalc=0x%04X rx=0x%04X\r\n",
          reg, value, crc_rx[0], crc_rx[1],
          static_cast<unsigned>((crc_calc ^ 0xFFFF) & 0xFFFF),
          static_cast<unsigned>(crc_received));
    if ((crc_calc ^ crc_received) != 0xFFFF) {
        return ERR_CRC;
    }

    // Send release byte (0xFF) to confirm transaction
    _ow.write(0xFF);
    return OK;
}

// =============================================================================
//  Private: _readReg
// =============================================================================
MAX31888::Error MAX31888::_readReg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    if (!_ow.reset()) return ERR_NO_DEVICE;
    _ow.skip();

    // Read Register [0x33] | addr | len-1 → (len) data + inverted CRC-16
    // (datasheet p.19: length byte = number of bytes to read - 1)
    _ow.write(CMD_READ_REG);
    _ow.write(reg);
    _ow.write(static_cast<uint8_t>(len - 1));

    // Read (len) data bytes + 2 CRC16 bytes
    for (uint8_t i = 0; i < len; ++i) {
        buf[i] = _ow.read();
    }
    uint8_t crc_rx[2];
    crc_rx[0] = _ow.read();
    crc_rx[1] = _ow.read();

    // Verify inverted CRC-16 over [CMD_READ_REG | reg | len-1 | data...]
    uint8_t hdr[3] = { CMD_READ_REG, reg, static_cast<uint8_t>(len - 1) };
    uint16_t crc_calc = OneWire::crc16(hdr, 3);
    crc_calc = OneWire::crc16(buf, len, crc_calc);

    uint16_t crc_received = static_cast<uint16_t>(crc_rx[0] | (crc_rx[1] << 8));
    debug("MAX31888 _readReg: reg=0x%02X len=%u rx=[%02X %02X] invcalc=0x%04X rx=0x%04X\r\n",
          reg, len, crc_rx[0], crc_rx[1],
          static_cast<unsigned>((crc_calc ^ 0xFFFF) & 0xFFFF),
          static_cast<unsigned>(crc_received));
    if ((crc_calc ^ crc_received) != 0xFFFF) {
        return ERR_CRC;
    }

    return OK;
}

// =============================================================================
//  Private: _rawToFloat
// =============================================================================
float MAX31888::_rawToFloat(int16_t raw, Resolution res)
{
    // Datasheet p.12: FIFO holds a left-justified 16-bit two's complement word;
    // T = value x 0.005 in 16-bit mode (no shift).
    // In lower resolutions the data is left-justified: shift right by (16 - bits),
    // i.e. (3 - res) for RES_13BIT..RES_16BIT.
    int8_t shift = 3 - static_cast<int8_t>(res);
    int16_t shifted = (shift > 0) ? static_cast<int16_t>(raw >> shift) : raw;
    return static_cast<float>(shifted) * LSB_16BIT;
}

// =============================================================================
//  Private: _floatToRaw
// =============================================================================
int16_t MAX31888::_floatToRaw(float temp_c, Resolution res)
{
    // Inverse of _rawToFloat (left-justified for current resolution).
    int8_t shift = 3 - static_cast<int8_t>(res);
    int16_t raw = static_cast<int16_t>(temp_c / LSB_16BIT);
    return (shift > 0) ? static_cast<int16_t>(raw << shift) : raw;
}


#ifndef MCP23017_H
#define MCP23017_H

#include <Wire.h>

#define MCP23017_IODIRA 0x00
#define MCP23017_IODIRB 0x01
#define MCP23017_GPPUA 0x0C
#define MCP23017_GPPUB 0x0D
#define MCP23017_GPIOA 0x12
#define MCP23017_GPIOB 0x13
#define MCP23017_OLATA 0x14
#define MCP23017_OLATB 0x15

class MCP23017
{
public:
    MCP23017(uint8_t address = 0x20) : _address(address), iodirA(0xFF), iodirB(0xFF), gppuA(0), gppuB(0), gpioA(0), gpioB(0) {}

    void begin()
    {
        Wire.begin();
        writeRegister(MCP23017_IODIRA, iodirA);
        writeRegister(MCP23017_IODIRB, iodirB);
        writeRegister(MCP23017_GPPUA, gppuA); // Initialize pull-ups
        writeRegister(MCP23017_GPPUB, gppuB); // Initialize pull-ups
        writeRegister(MCP23017_GPIOA, gpioA);
        writeRegister(MCP23017_GPIOB, gpioB);
    }

    void pinMode(uint8_t pin, uint8_t mode)
    {
        if (pin < 8)
        {
            if (mode == INPUT_PULLUP)
            {
                iodirA |= (1 << pin); // INPUT
                gppuA |= (1 << pin);  // Enable pull-up
            }
            else if (mode == INPUT)
            {
                iodirA |= (1 << pin); // INPUT
                gppuA &= ~(1 << pin); // Disable pull-up for active-high with external pull-down
            }
            else // OUTPUT mode
            {
                iodirA &= ~(1 << pin); // OUTPUT
                gppuA &= ~(1 << pin);  // Outputs don't need pull-ups
            }
            writeRegister(MCP23017_IODIRA, iodirA);
            writeRegister(MCP23017_GPPUA, gppuA);
        }
        else
        {
            pin -= 8;
            if (mode == INPUT_PULLUP)
            {
                iodirB |= (1 << pin); // INPUT
                gppuB |= (1 << pin);  // Enable pull-up
            }
            else if (mode == INPUT)
            {
                iodirB |= (1 << pin); // INPUT
                gppuB &= ~(1 << pin); // Disable pull-up for active-high with external pull-down
            }
            else // OUTPUT mode
            {
                iodirB &= ~(1 << pin); // OUTPUT
                gppuB &= ~(1 << pin);  // Outputs don't need pull-ups
            }
            writeRegister(MCP23017_IODIRB, iodirB);
            writeRegister(MCP23017_GPPUB, gppuB);
        }
    }

    void digitalWrite(uint8_t pin, uint8_t value)
    {
        if (pin < 8)
        {
            if (value)
                gpioA |= (1 << pin);
            else
                gpioA &= ~(1 << pin);
            writeRegister(MCP23017_GPIOA, gpioA);
        }
        else
        {
            pin -= 8;
            if (value)
                gpioB |= (1 << pin);
            else
                gpioB &= ~(1 << pin);
            writeRegister(MCP23017_GPIOB, gpioB);
        }
    }

    int digitalRead(uint8_t pin)
    {
        if (pin < 8)
        {
            uint8_t val = readRegister(MCP23017_GPIOA);
            return (val >> pin) & 0x01;
        }
        else
        {
            pin -= 8;
            uint8_t val = readRegister(MCP23017_GPIOB);
            return (val >> pin) & 0x01;
        }
    }

    // --- Advanced Features ---
    // Enable interrupts for PA0-7 (mode: 0=change, 1=rising, 2=falling)
    void enableInterruptsPA(uint8_t mode)
    {
        // GPINTENA: Enable interrupt on change for PA pins
        writeRegister(0x04, 0xFF); // GPINTENA
        // INTCONA: Interrupt control (0=change, 1=compare to DEFVAL)
        if (mode == 0)
        {
            writeRegister(0x08, 0x00); // INTCONA: interrupt on change
        }
        else
        {
            writeRegister(0x08, 0xFF); // INTCONA: compare to DEFVAL
            if (mode == 1)
                writeRegister(0x06, 0x00); // DEFVALA: rising edge (compare to 0)
            else
                writeRegister(0x06, 0xFF); // DEFVALA: falling edge (compare to 1)
        }
        // IOCON: Enable mirroring, open-drain, etc. if needed
    }

    void disableInterruptsPA()
    {
        writeRegister(0x04, 0x00); // GPINTENA
    }

    // Returns which PA pin triggered (INTFA)
    uint8_t getInterruptSourcePA()
    {
        return readRegister(0x0E); // INTFA
    }

    // Clear interrupts (read INTCAPA)
    void clearInterruptsPA()
    {
        readRegister(0x10); // INTCAPA
    }

    // Bulk GPIO operations
    uint16_t readAllPins()
    {
        uint8_t a = readRegister(MCP23017_GPIOA);
        uint8_t b = readRegister(MCP23017_GPIOB);
        return ((uint16_t)b << 8) | a;
    }

    void writeAllPins(uint16_t value)
    {
        writeRegister(MCP23017_GPIOA, value & 0xFF);
        writeRegister(MCP23017_GPIOB, (value >> 8) & 0xFF);
    }

private:
    uint8_t _address;
    uint8_t iodirA, iodirB;
    uint8_t gppuA, gppuB;
    uint8_t gpioA, gpioB;

    void writeRegister(uint8_t reg, uint8_t value)
    {
        Wire.beginTransmission(_address);
        Wire.write(reg);
        Wire.write(value);
        Wire.endTransmission();
    }

    uint8_t readRegister(uint8_t reg)
    {
        Wire.beginTransmission(_address);
        Wire.write(reg);
        Wire.endTransmission();
        Wire.requestFrom(_address, (uint8_t)1);
        if (Wire.available())
        {
            return Wire.read();
        }
        return 0;
    }
};

#endif // MCP23017_H

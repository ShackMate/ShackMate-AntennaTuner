#ifndef MCP23017_H
#define MCP23017_H

#include <Wire.h>
#include <Arduino.h>

class MCP23017
{
public:
    MCP23017(uint8_t address = 0x20);
    void begin();
    void pinMode(uint8_t pin, uint8_t mode);
    void digitalWrite(uint8_t pin, uint8_t value);
    int digitalRead(uint8_t pin);
    void writeGPIOAB(uint16_t value);
    uint16_t readGPIOAB();

private:
    uint8_t _i2caddr;
    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
};

#endif // MCP23017_H

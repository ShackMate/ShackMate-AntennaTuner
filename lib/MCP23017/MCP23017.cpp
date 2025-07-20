#include "MCP23017.h"

// MCP23017 Register Addresses
#define IODIRA 0x00
#define IODIRB 0x01
#define GPIOA 0x12
#define GPIOB 0x13
#define OLATA 0x14
#define OLATB 0x15

MCP23017::MCP23017(uint8_t address) : _i2caddr(address) {}

void MCP23017::begin()
{
    Wire.begin();
    // Set all to inputs by default
    writeRegister(IODIRA, 0xFF);
    writeRegister(IODIRB, 0xFF);
}

void MCP23017::pinMode(uint8_t pin, uint8_t mode)
{
    uint8_t iodir = (pin < 8) ? IODIRA : IODIRB;
    uint8_t bit = pin % 8;
    uint8_t reg = readRegister(iodir);
    if (mode == OUTPUT)
    {
        reg &= ~(1 << bit);
    }
    else
    {
        reg |= (1 << bit);
    }
    writeRegister(iodir, reg);
}

void MCP23017::digitalWrite(uint8_t pin, uint8_t value)
{
    uint8_t gpio = (pin < 8) ? OLATA : OLATB;
    uint8_t bit = pin % 8;
    uint8_t reg = readRegister(gpio);
    if (value)
    {
        reg |= (1 << bit);
    }
    else
    {
        reg &= ~(1 << bit);
    }
    writeRegister(gpio, reg);
}

int MCP23017::digitalRead(uint8_t pin)
{
    uint8_t gpio = (pin < 8) ? GPIOA : GPIOB;
    uint8_t bit = pin % 8;
    uint8_t reg = readRegister(gpio);
    return (reg & (1 << bit)) ? HIGH : LOW;
}

void MCP23017::writeGPIOAB(uint16_t value)
{
    writeRegister(OLATA, value & 0xFF);
    writeRegister(OLATB, value >> 8);
}

uint16_t MCP23017::readGPIOAB()
{
    uint8_t a = readRegister(GPIOA);
    uint8_t b = readRegister(GPIOB);
    return ((uint16_t)b << 8) | a;
}

void MCP23017::writeRegister(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(_i2caddr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t MCP23017::readRegister(uint8_t reg)
{
    Wire.beginTransmission(_i2caddr);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(_i2caddr, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

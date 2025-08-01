#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include "../lib/MCP23017/MCP23017.h"
#include <Adafruit_NeoPixel.h>
#include "Config.h"

// Forward declarations
class ConfigManager;

class HardwareManager
{
private:
    MCP23017 *mcp;
    Adafruit_NeoPixel *atomLed;
    ConfigManager *config;

    // LED state
    RGBColor currentColor;
    bool blinkState;
    unsigned long lastBlinkTime;
    uint16_t blinkInterval;

    // Hardware status
    bool mcpInitialized;
    bool ledInitialized;
    bool i2cInitialized;

    // Helper methods
    bool initializeI2C();
    bool initializeMCP23017();
    bool initializeLED();
    void setupIndicatorPins();

public:
    HardwareManager(ConfigManager *configManager);
    ~HardwareManager();

    // Initialization
    bool begin();
    void reset();

    // MCP23017 access
    MCP23017 *getMCP() { return mcp; }

    // LED control
    void setLED(const RGBColor &color);
    void setLED(uint8_t r, uint8_t g, uint8_t b);
    void setBlinkLED(const RGBColor &color, uint16_t intervalMs);
    void stopBlink();
    void updateLED(); // Call in main loop

    // Status indicators (read from MCP23017)
    bool getTuningStatus();
    bool getSWRStatus();
    int getTuningStatusRaw();
    int getSWRStatusRaw();

    // Hardware status
    bool isMCPReady() const { return mcpInitialized; }
    bool isLEDReady() const { return ledInitialized; }
    bool isI2CReady() const { return i2cInitialized; }
    bool isHardwareReady() const { return mcpInitialized && ledInitialized && i2cInitialized; }

    // Diagnostics
    bool testI2C();
    bool testMCP23017();
    bool testLED();
    void runDiagnostics();
    String getHardwareStatus();

    // Error recovery
    bool recoverI2C();
    bool recoverMCP23017();
    void attemptRecovery();
};

#endif // HARDWARE_MANAGER_H

#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include "../lib/MCP23017/MCP23017.h"
#include "Config.h"

// Forward declarations
class ConfigManager;

struct ButtonMapping
{
    uint8_t mcpPin;
    const char *id;
    const char *name;
    uint8_t momentaryIndex;
};

struct MomentaryAction
{
    uint8_t mcpPin;
    unsigned long expireMillis;
    bool inProgress;

    MomentaryAction() : mcpPin(255), expireMillis(0), inProgress(false) {}
    MomentaryAction(uint8_t pin) : mcpPin(pin), expireMillis(0), inProgress(false) {}
};

class ButtonManager
{
private:
    MCP23017 *mcp;
    ConfigManager *config;

    // Button state tracking
    int lastButtonStates[BUTTON_COUNT];
    MomentaryAction momentaryActions[MOMENTARY_ACTION_COUNT];

    // Button mapping table
    static const ButtonMapping buttonMappings[BUTTON_COUNT];

    // Helper methods
    int findButtonIndex(const String &buttonId);
    bool isValidButton(const String &buttonId);
    void updateButtonState(uint8_t pin, bool state);

public:
    ButtonManager(MCP23017 *mcpInstance, ConfigManager *configManager);

    // Initialization
    bool begin();
    bool setMCP(MCP23017 *mcpInstance);
    void setupOutputs();

    // Button control
    bool setButtonOutput(const String &buttonId, bool state);
    bool setButtonOutput(const String &buttonId); // Uses saved state
    bool pressButton(const String &buttonId);
    bool releaseButton(const String &buttonId);
    bool pulseButton(const String &buttonId, unsigned long durationMs = 200); // NEW: Timed pulse

    // Momentary button handling
    bool startMomentaryAction(const String &buttonId);
    bool stopMomentaryAction(const String &buttonId);
    void processMomentaryActions(); // Call in main loop

    // State management
    void scanButtonStates(); // Call in main loop
    bool getButtonState(const String &buttonId);
    int getLastButtonState(uint8_t index);

    // Special button handling
    bool isAntButtonMomentary();
    void handleModelSwitch();

    // Debug
    void printButtonStates();
    String getButtonInfo(const String &buttonId);
};

#endif // BUTTON_MANAGER_H

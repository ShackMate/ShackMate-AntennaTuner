#include "ButtonManager.h"
#include "ConfigManager.h"

// Static button mapping table
const ButtonMapping ButtonManager::buttonMappings[BUTTON_COUNT] = {
    {BUTTON_CUP_PIN, "button-cup", "Capacitor Up", BTN_IDX_CUP},
    {BUTTON_CDN_PIN, "button-cdn", "Capacitor Down", BTN_IDX_CDN},
    {BUTTON_LUP_PIN, "button-lup", "Inductor Up", BTN_IDX_LUP},
    {BUTTON_LDN_PIN, "button-ldn", "Inductor Down", BTN_IDX_LDN},
    {BUTTON_TUNE_PIN, "button-tune", "Tune", BTN_IDX_TUNE},
    {BUTTON_ANT_PIN, "button-ant", "Antenna", BTN_IDX_ANT}};

ButtonManager::ButtonManager(MCP23017 *mcpInstance, ConfigManager *configManager)
    : mcp(mcpInstance), config(configManager)
{
    // Initialize button states to HIGH (inactive)
    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        lastButtonStates[i] = HIGH;
    }

    // Initialize momentary actions
    for (int i = 0; i < MOMENTARY_ACTION_COUNT; i++)
    {
        if (i < BUTTON_COUNT)
        {
            momentaryActions[i] = MomentaryAction(buttonMappings[i].mcpPin);
        }
        else
        {
            momentaryActions[i] = MomentaryAction(255); // Invalid pin
        }
    }
}

bool ButtonManager::begin()
{
    if (!mcp)
    {
        DEBUG_PRINTLN("[ERROR] ButtonManager: MCP23017 instance is null");
        return false;
    }

    if (!config)
    {
        DEBUG_PRINTLN("[ERROR] ButtonManager: ConfigManager instance is null");
        return false;
    }

    setupOutputs();

    DEBUG_PRINTLN("[INFO] ButtonManager initialized");
    return true;
}

bool ButtonManager::setMCP(MCP23017 *mcpInstance)
{
    if (!mcpInstance)
    {
        DEBUG_PRINTLN("[ERROR] ButtonManager: Cannot set null MCP instance");
        return false;
    }

    mcp = mcpInstance;
    DEBUG_PRINTLN("[INFO] ButtonManager: MCP instance updated");
    return true;
}

void ButtonManager::setupOutputs()
{
    if (!mcp)
    {
        DEBUG_PRINTLN("[ERROR] setupOutputs: MCP23017 instance is null");
        return;
    }

    DEBUG_PRINTLN("[INFO] Setting up button outputs...");

    // Test MCP23017 communication first
    DEBUG_PRINTF("[INFO] Testing MCP23017 communication at address 0x%02X...\n", MCP23017_ADDRESS);

    try
    {
        // Test basic I/O configuration
        mcp->pinMode(0, OUTPUT);
        mcp->digitalWrite(0, HIGH);
        bool testRead = mcp->digitalRead(0);
        DEBUG_PRINTF("[INFO] MCP23017 test - Set pin 0 HIGH, read back: %s\n", testRead ? "HIGH" : "LOW");

        mcp->digitalWrite(0, LOW);
        testRead = mcp->digitalRead(0);
        DEBUG_PRINTF("[INFO] MCP23017 test - Set pin 0 LOW, read back: %s\n", testRead ? "HIGH" : "LOW");
    }
    catch (...)
    {
        DEBUG_PRINTLN("[ERROR] MCP23017 communication test failed - exception caught");
        return;
    }

    // Configure all button pins as outputs
    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        uint8_t pin = buttonMappings[i].mcpPin;
        try
        {
            mcp->pinMode(pin, OUTPUT);
            mcp->digitalWrite(pin, HIGH); // Inactive state for active-low logic
            DEBUG_PRINTF("[DEBUG] Button %s (pin %d) configured as OUTPUT HIGH\n",
                         buttonMappings[i].name, pin);
        }
        catch (...)
        {
            DEBUG_PRINTF("[ERROR] Failed to configure button %s (pin %d)\n",
                         buttonMappings[i].name, pin);
        }
    }

    // Configure AUTO button pin (not in buttonMappings array)
    mcp->pinMode(BUTTON_AUTO_PIN, OUTPUT);
    mcp->digitalWrite(BUTTON_AUTO_PIN, HIGH); // Inactive state for active-low logic
    DEBUG_PRINTF("[DEBUG] Button AUTO (pin %d) configured as OUTPUT HIGH\n", BUTTON_AUTO_PIN);

    // Set initial states based on saved configuration
    setButtonOutput("button-ant");
    setButtonOutput("button-auto");
}

int ButtonManager::findButtonIndex(const String &buttonId)
{
    // Handle alternative button IDs
    String normalizedId = buttonId;
    if (buttonId == "button-cup1")
        normalizedId = "button-cup";
    else if (buttonId == "button-lup1")
        normalizedId = "button-lup";
    else if (buttonId == "button-cup2")
        normalizedId = "button-cdn";
    else if (buttonId == "button-lup2")
        normalizedId = "button-ldn";

    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        if (normalizedId == buttonMappings[i].id)
        {
            return i;
        }
    }
    return -1;
}

bool ButtonManager::isValidButton(const String &buttonId)
{
    return findButtonIndex(buttonId) >= 0;
}

bool ButtonManager::setButtonOutput(const String &buttonId, bool state)
{
    DEBUG_PRINTF("[DEBUG] setButtonOutput(%s, %s) called\n", buttonId.c_str(), state ? "true" : "false");

    if (!mcp)
    {
        DEBUG_PRINTLN("[ERROR] setButtonOutput: MCP23017 instance is null");
        return false;
    }

    // Handle special buttons (ANT/AUTO) that have custom logic
    if (buttonId == "button-ant")
    {
        // For ANT button, update config state and set hardware directly
        config->setAntState(state);

        if (isAntButtonMomentary())
        {
            // In momentary mode (Model 998), always inactive
            mcp->digitalWrite(BUTTON_ANT_PIN, HIGH);
            DEBUG_PRINTF("[DEBUG] ANT button (pin %d) set to inactive (HIGH) for momentary mode\n", BUTTON_ANT_PIN);
        }
        else
        {
            // In latching mode, use the state we just set
            // NOTE: Logic inverted - ANT 1 (false) should be ACTIVE (LOW), ANT 2 (true) should be INACTIVE (HIGH)
            try
            {
                mcp->digitalWrite(BUTTON_ANT_PIN, state ? HIGH : LOW);
                DEBUG_PRINTF("[DEBUG] ANT button (pin %d) set to %s for latching mode (state=%s)\n",
                             BUTTON_ANT_PIN, state ? "INACTIVE (HIGH)" : "ACTIVE (LOW)", state ? "true" : "false");
            }
            catch (...)
            {
                DEBUG_PRINTF("[ERROR] Failed to set ANT button pin %d\n", BUTTON_ANT_PIN);
                return false;
            }
        }
        return true;
    }
    else if (buttonId == "button-auto")
    {
        // For AUTO button, update config state and set hardware directly
        config->setAutoState(state);
        mcp->digitalWrite(BUTTON_AUTO_PIN, state ? LOW : HIGH);
        DEBUG_PRINTF("[DEBUG] AUTO button (pin %d) set to %s (state=%s)\n",
                     BUTTON_AUTO_PIN, state ? "ACTIVE (LOW)" : "INACTIVE (HIGH)", state ? "true" : "false");
        return true;
    }

    // Handle other buttons using the mapping table
    int index = findButtonIndex(buttonId);
    if (index < 0)
    {
        DEBUG_PRINTF("[ERROR] Invalid button ID: %s\n", buttonId.c_str());
        return false;
    }

    uint8_t pin = buttonMappings[index].mcpPin;
    mcp->digitalWrite(pin, state ? LOW : HIGH); // Active-low logic

    DEBUG_PRINTF("[DEBUG] Button %s set to %s\n",
                 buttonMappings[index].name, state ? "ACTIVE" : "INACTIVE");

    return true;
}

bool ButtonManager::setButtonOutput(const String &buttonId)
{
    DEBUG_PRINTF("[DEBUG] setButtonOutput called for: %s\n", buttonId.c_str());

    if (!mcp)
    {
        DEBUG_PRINTLN("[ERROR] setButtonOutput: MCP23017 instance is null");
        return false;
    }

    if (!config)
    {
        DEBUG_PRINTLN("[ERROR] setButtonOutput: ConfigManager instance is null");
        return false;
    }

    if (buttonId == "button-ant")
    {
        if (isAntButtonMomentary())
        {
            // In momentary mode (Model 998), always inactive
            mcp->digitalWrite(BUTTON_ANT_PIN, HIGH);
            DEBUG_PRINTF("[DEBUG] ANT button (pin %d) set to inactive (HIGH) for momentary mode\n", BUTTON_ANT_PIN);
        }
        else
        {
            // In latching mode, use saved state
            // NOTE: Logic inverted - ANT 1 (false) should be ACTIVE (LOW), ANT 2 (true) should be INACTIVE (HIGH)
            bool antState = config->getAntState();
            DEBUG_PRINTF("[DEBUG] Retrieved ANT state from config: %s\n", antState ? "true (ANT 2)" : "false (ANT 1)");
            mcp->digitalWrite(BUTTON_ANT_PIN, antState ? HIGH : LOW);
            DEBUG_PRINTF("[DEBUG] ANT button (pin %d) set to %s for latching mode (state=%s)\n",
                         BUTTON_ANT_PIN, antState ? "INACTIVE (HIGH)" : "ACTIVE (LOW)", antState ? "true" : "false");
        }
        return true;
    }
    else if (buttonId == "button-auto")
    {
        bool autoState = config->getAutoState();
        mcp->digitalWrite(BUTTON_AUTO_PIN, autoState ? LOW : HIGH);
        DEBUG_PRINTF("[DEBUG] AUTO button (pin %d) set to %s (state=%s)\n",
                     BUTTON_AUTO_PIN, autoState ? "ACTIVE (LOW)" : "INACTIVE (HIGH)", autoState ? "true" : "false");
        return true;
    }

    DEBUG_PRINTF("[ERROR] setButtonOutput without state not supported for: %s\n", buttonId.c_str());
    return false;
}

bool ButtonManager::pressButton(const String &buttonId)
{
    return setButtonOutput(buttonId, true);
}

bool ButtonManager::releaseButton(const String &buttonId)
{
    return setButtonOutput(buttonId, false);
}

bool ButtonManager::startMomentaryAction(const String &buttonId)
{
    int index = findButtonIndex(buttonId);
    if (index < 0)
    {
        return false;
    }

    uint8_t pin = buttonMappings[index].mcpPin;
    uint8_t momentaryIdx = buttonMappings[index].momentaryIndex;

    // Special handling for ANT button in momentary mode
    if (buttonId == "button-ant" && isAntButtonMomentary())
    {
        mcp->digitalWrite(pin, LOW);
        momentaryActions[momentaryIdx].inProgress = true;
        momentaryActions[momentaryIdx].expireMillis = 0; // No timeout
        DEBUG_PRINTLN("[DEBUG] ANT momentary action started (Model 998)");
        return true;
    }

    // Standard momentary action
    mcp->digitalWrite(pin, LOW); // Press (active low)
    momentaryActions[momentaryIdx].inProgress = true;
    momentaryActions[momentaryIdx].expireMillis = 0; // No timeout - wait for release

    DEBUG_PRINTF("[DEBUG] Momentary action started for %s (pin %d)\n",
                 buttonMappings[index].name, pin);

    return true;
}

bool ButtonManager::stopMomentaryAction(const String &buttonId)
{
    int index = findButtonIndex(buttonId);
    if (index < 0)
    {
        return false;
    }

    uint8_t pin = buttonMappings[index].mcpPin;
    uint8_t momentaryIdx = buttonMappings[index].momentaryIndex;

    mcp->digitalWrite(pin, HIGH); // Release (inactive high)
    momentaryActions[momentaryIdx].inProgress = false;
    momentaryActions[momentaryIdx].expireMillis = 0;

    DEBUG_PRINTF("[DEBUG] Momentary action stopped for %s (pin %d)\n",
                 buttonMappings[index].name, pin);

    return true;
}

void ButtonManager::processMomentaryActions()
{
    unsigned long now = millis();

    for (int i = 0; i < MOMENTARY_ACTION_COUNT; i++)
    {
        // Only auto-release buttons with timeout set (expireMillis > 0)
        if (momentaryActions[i].inProgress &&
            momentaryActions[i].expireMillis > 0 &&
            now >= momentaryActions[i].expireMillis)
        {

            mcp->digitalWrite(momentaryActions[i].mcpPin, HIGH); // Release
            momentaryActions[i].inProgress = false;

            DEBUG_PRINTF("[DEBUG] Auto-releasing MCP pin %d\n", momentaryActions[i].mcpPin);
        }
    }
}

void ButtonManager::scanButtonStates()
{
    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        uint8_t pin = buttonMappings[i].mcpPin;
        int currentState = mcp->digitalRead(pin);

        if (currentState != lastButtonStates[i])
        {
            lastButtonStates[i] = currentState;
            DEBUG_PRINTF("[DEBUG] Button state change: %s = %s\n",
                         buttonMappings[i].name,
                         currentState == HIGH ? "HIGH" : "LOW");
        }
    }
}

bool ButtonManager::getButtonState(const String &buttonId)
{
    int index = findButtonIndex(buttonId);
    if (index < 0)
    {
        return false;
    }

    uint8_t pin = buttonMappings[index].mcpPin;
    return mcp->digitalRead(pin) == LOW; // Active-low logic
}

int ButtonManager::getLastButtonState(uint8_t index)
{
    if (index >= BUTTON_COUNT)
    {
        return HIGH;
    }
    return lastButtonStates[index];
}

bool ButtonManager::isAntButtonMomentary()
{
    return config->getCurrentCivModel().indexOf("998") >= 0;
}

void ButtonManager::handleModelSwitch()
{
    // Clear any in-progress ANT button momentary action
    momentaryActions[BTN_IDX_ANT].inProgress = false;
    momentaryActions[BTN_IDX_ANT].expireMillis = 0;

    // Reset ANT button output based on new model
    setButtonOutput("button-ant");

    DEBUG_PRINTF("[DEBUG] Button states reset after model switch to %s\n",
                 config->getCurrentCivModel().c_str());
}

void ButtonManager::printButtonStates()
{
    DEBUG_PRINTLN("=== Button States ===");
    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        bool state = getButtonState(buttonMappings[i].id);
        DEBUG_PRINTF("%s: %s\n", buttonMappings[i].name, state ? "ACTIVE" : "INACTIVE");
    }
    DEBUG_PRINTLN("====================");
}

String ButtonManager::getButtonInfo(const String &buttonId)
{
    int index = findButtonIndex(buttonId);
    if (index < 0)
    {
        return "Invalid button";
    }

    const ButtonMapping &btn = buttonMappings[index];
    bool state = getButtonState(buttonId);

    return String(btn.name) + " (Pin " + String(btn.mcpPin) + "): " +
           (state ? "ACTIVE" : "INACTIVE");
}

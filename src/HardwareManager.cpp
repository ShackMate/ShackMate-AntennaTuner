#include "HardwareManager.h"
#include "ConfigManager.h"

HardwareManager::HardwareManager(ConfigManager *configManager)
    : mcp(nullptr), atomLed(nullptr), config(configManager),
      currentColor(Colors::OFF), blinkState(false), lastBlinkTime(0), blinkInterval(0),
      mcpInitialized(false), ledInitialized(false), i2cInitialized(false)
{
}

HardwareManager::~HardwareManager()
{
    if (mcp)
    {
        delete mcp;
    }
    if (atomLed)
    {
        delete atomLed;
    }
}

bool HardwareManager::begin()
{
    DEBUG_PRINTLN("[INFO] Initializing Hardware Manager...");

    bool success = true;

    // Initialize I2C first
    if (!initializeI2C())
    {
        DEBUG_PRINTLN("[ERROR] Failed to initialize I2C");
        success = false;
    }

    // Initialize MCP23017
    if (success && !initializeMCP23017())
    {
        DEBUG_PRINTLN("[ERROR] Failed to initialize MCP23017");
        success = false;
    }

    // Initialize LED (this should always work)
    if (!initializeLED())
    {
        DEBUG_PRINTLN("[WARNING] Failed to initialize LED");
        // Don't fail completely if LED doesn't work
    }

    if (success)
    {
        setupIndicatorPins();
        DEBUG_PRINTLN("[INFO] Hardware Manager initialized successfully");
    }
    else
    {
        DEBUG_PRINTLN("[ERROR] Hardware Manager initialization failed");
    }

    return success;
}

bool HardwareManager::initializeI2C()
{
    DEBUG_PRINTF("[INFO] Initializing I2C (SDA: %d, SCL: %d, Speed: %d Hz)\n",
                 I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_SPEED);

    Wire.setClock(I2C_CLOCK_SPEED);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Test I2C communication
    i2cInitialized = testI2C();

    if (i2cInitialized)
    {
        DEBUG_PRINTLN("[INFO] I2C initialized successfully");
    }
    else
    {
        DEBUG_PRINTLN("[ERROR] I2C initialization failed");
    }

    return i2cInitialized;
}

bool HardwareManager::initializeMCP23017()
{
    if (!i2cInitialized)
    {
        DEBUG_PRINTLN("[ERROR] Cannot initialize MCP23017 - I2C not ready");
        return false;
    }

    DEBUG_PRINTF("[INFO] Initializing MCP23017 at address 0x%02X\n", MCP23017_ADDRESS);

    // Create MCP23017 instance
    mcp = new MCP23017(MCP23017_ADDRESS);
    if (!mcp)
    {
        DEBUG_PRINTLN("[ERROR] Failed to create MCP23017 instance");
        return false;
    }

    // Initialize MCP23017
    mcp->begin();

    // Test MCP23017 communication
    mcpInitialized = testMCP23017();

    if (mcpInitialized)
    {
        DEBUG_PRINTLN("[INFO] MCP23017 initialized successfully");
    }
    else
    {
        DEBUG_PRINTLN("[ERROR] MCP23017 initialization failed");
        delete mcp;
        mcp = nullptr;
    }

    return mcpInitialized;
}

bool HardwareManager::initializeLED()
{
    DEBUG_PRINTF("[INFO] Initializing LED (Pin: %d, Count: %d)\n", ATOM_LED_PIN, ATOM_NUM_LEDS);

    // Create LED instance
    atomLed = new Adafruit_NeoPixel(ATOM_NUM_LEDS, ATOM_LED_PIN, NEO_GRB + NEO_KHZ800);
    if (!atomLed)
    {
        DEBUG_PRINTLN("[ERROR] Failed to create LED instance");
        return false;
    }

    // Initialize LED
    atomLed->begin();
    atomLed->setBrightness(50); // 50/255 brightness
    atomLed->clear();
    atomLed->show();

    // Test LED
    ledInitialized = testLED();

    if (ledInitialized)
    {
        DEBUG_PRINTLN("[INFO] LED initialized successfully");
    }
    else
    {
        DEBUG_PRINTLN("[ERROR] LED initialization failed");
        delete atomLed;
        atomLed = nullptr;
    }

    return ledInitialized;
}

void HardwareManager::setupIndicatorPins()
{
    if (!mcpInitialized || !mcp)
    {
        DEBUG_PRINTLN("[ERROR] Cannot setup indicator pins - MCP23017 not ready");
        return;
    }

    // Set indicator pins as inputs with pull-down for active-high operation
    mcp->pinMode(MCP_TUNING_PIN, INPUT);
    mcp->pinMode(MCP_SWR_PIN, INPUT);

    DEBUG_PRINTF("[INFO] Indicator pins configured - TUNING: PA%d, SWR: PA%d\n",
                 MCP_TUNING_PIN, MCP_SWR_PIN);
}

void HardwareManager::setLED(const RGBColor &color)
{
    if (!ledInitialized || !atomLed)
    {
        return;
    }

    currentColor = color;
    blinkInterval = 0; // Stop blinking

    atomLed->setPixelColor(0, atomLed->Color(color.r, color.g, color.b));
    atomLed->show();
}

void HardwareManager::setLED(uint8_t r, uint8_t g, uint8_t b)
{
    setLED(RGBColor(r, g, b));
}

void HardwareManager::setBlinkLED(const RGBColor &color, uint16_t intervalMs)
{
    if (!ledInitialized || !atomLed)
    {
        return;
    }

    currentColor = color;
    blinkInterval = intervalMs;
    blinkState = false;
    lastBlinkTime = millis();

    // Start with LED on
    atomLed->setPixelColor(0, atomLed->Color(color.r, color.g, color.b));
    atomLed->show();
}

void HardwareManager::stopBlink()
{
    blinkInterval = 0;
    setLED(currentColor);
}

void HardwareManager::updateLED()
{
    if (!ledInitialized || !atomLed || blinkInterval == 0)
    {
        return;
    }

    unsigned long now = millis();
    if (now - lastBlinkTime >= blinkInterval)
    {
        lastBlinkTime = now;
        blinkState = !blinkState;

        if (blinkState)
        {
            atomLed->setPixelColor(0, atomLed->Color(currentColor.r, currentColor.g, currentColor.b));
        }
        else
        {
            atomLed->setPixelColor(0, atomLed->Color(0, 0, 0));
        }
        atomLed->show();
    }
}

bool HardwareManager::getTuningStatus()
{
    if (!mcpInitialized || !mcp)
    {
        return false;
    }

    return mcp->digitalRead(MCP_TUNING_PIN) == HIGH;
}

bool HardwareManager::getSWRStatus()
{
    if (!mcpInitialized || !mcp)
    {
        return false;
    }

    return mcp->digitalRead(MCP_SWR_PIN) == HIGH;
}

int HardwareManager::getTuningStatusRaw()
{
    if (!mcpInitialized || !mcp)
    {
        return -1;
    }

    return mcp->digitalRead(MCP_TUNING_PIN);
}

int HardwareManager::getSWRStatusRaw()
{
    if (!mcpInitialized || !mcp)
    {
        return -1;
    }

    return mcp->digitalRead(MCP_SWR_PIN);
}

bool HardwareManager::testI2C()
{
    // Try to scan for devices on I2C bus
    Wire.beginTransmission(MCP23017_ADDRESS);
    byte error = Wire.endTransmission();

    if (error == 0)
    {
        DEBUG_PRINTF("[INFO] I2C device found at address 0x%02X\n", MCP23017_ADDRESS);
        return true;
    }
    else
    {
        DEBUG_PRINTF("[ERROR] I2C device not found at address 0x%02X (error: %d)\n",
                     MCP23017_ADDRESS, error);
        return false;
    }
}

bool HardwareManager::testMCP23017()
{
    if (!mcp)
    {
        return false;
    }

    // Try to configure and read back a pin to test communication
    // Test with a safe configuration change
    try
    {
        // Set pin 0 as output then input to test register access
        mcp->pinMode(0, OUTPUT);
        mcp->digitalWrite(0, HIGH);
        bool state1 = mcp->digitalRead(0);

        mcp->digitalWrite(0, LOW);
        bool state2 = mcp->digitalRead(0);

        // Test passed if we can read different states
        bool success = (state1 != state2);

        DEBUG_PRINTF("[HARDWARE] MCP23017 test %s\n", success ? "PASSED" : "FAILED");
        return success;
    }
    catch (...)
    {
        DEBUG_PRINTLN("[HARDWARE] MCP23017 test FAILED - Exception");
        return false;
    }
}

bool HardwareManager::testLED()
{
    if (!atomLed)
    {
        return false;
    }

    // Simple test - try to set a color and assume it works
    // We can't really verify LED output without external sensors
    atomLed->setPixelColor(0, atomLed->Color(1, 1, 1));
    atomLed->show();
    delay(10);
    atomLed->clear();
    atomLed->show();

    DEBUG_PRINTLN("[INFO] LED test completed (visual verification required)");
    return true;
}

void HardwareManager::runDiagnostics()
{
    DEBUG_PRINTLN("========== HARDWARE DIAGNOSTICS ==========");

    DEBUG_PRINTF("I2C Status: %s\n", i2cInitialized ? "OK" : "FAILED");
    if (i2cInitialized)
    {
        DEBUG_PRINTF("  SDA Pin: %d, SCL Pin: %d\n", I2C_SDA_PIN, I2C_SCL_PIN);
        DEBUG_PRINTF("  Clock Speed: %d Hz\n", I2C_CLOCK_SPEED);
    }

    DEBUG_PRINTF("MCP23017 Status: %s\n", mcpInitialized ? "OK" : "FAILED");
    if (mcpInitialized)
    {
        DEBUG_PRINTF("  Address: 0x%02X\n", MCP23017_ADDRESS);
        DEBUG_PRINTF("  Tuning Pin: PA%d, SWR Pin: PA%d\n", MCP_TUNING_PIN, MCP_SWR_PIN);

        // Read current indicator states
        int tuningRaw = getTuningStatusRaw();
        int swrRaw = getSWRStatusRaw();
        DEBUG_PRINTF("  Current States - Tuning: %s, SWR: %s\n",
                     tuningRaw == HIGH ? "ACTIVE" : "INACTIVE",
                     swrRaw == HIGH ? "ACTIVE" : "INACTIVE");
    }

    DEBUG_PRINTF("LED Status: %s\n", ledInitialized ? "OK" : "FAILED");
    if (ledInitialized)
    {
        DEBUG_PRINTF("  Pin: %d, Count: %d\n", ATOM_LED_PIN, ATOM_NUM_LEDS);
    }

    DEBUG_PRINTF("Overall Hardware Status: %s\n", isHardwareReady() ? "READY" : "NOT READY");
    DEBUG_PRINTLN("==========================================");
}

String HardwareManager::getHardwareStatus()
{
    String status = "{";
    status += "\"i2c_ready\":" + String(i2cInitialized ? "true" : "false") + ",";
    status += "\"mcp_ready\":" + String(mcpInitialized ? "true" : "false") + ",";
    status += "\"led_ready\":" + String(ledInitialized ? "true" : "false") + ",";
    status += "\"hardware_ready\":" + String(isHardwareReady() ? "true" : "false");

    if (mcpInitialized)
    {
        status += ",\"tuning_active\":" + String(getTuningStatus() ? "true" : "false");
        status += ",\"swr_ok\":" + String(getSWRStatus() ? "true" : "false");
    }

    status += "}";
    return status;
}

bool HardwareManager::recoverI2C()
{
    DEBUG_PRINTLN("[INFO] Attempting I2C recovery...");

    // Reset I2C
    Wire.end();
    delay(100);

    return initializeI2C();
}

bool HardwareManager::recoverMCP23017()
{
    if (!i2cInitialized)
    {
        DEBUG_PRINTLN("[ERROR] Cannot recover MCP23017 - I2C not ready");
        return false;
    }

    DEBUG_PRINTLN("[INFO] Attempting MCP23017 recovery...");

    // Clean up old instance
    if (mcp)
    {
        delete mcp;
        mcp = nullptr;
        mcpInitialized = false;
    }

    // Try to reinitialize
    return initializeMCP23017();
}

void HardwareManager::attemptRecovery()
{
    DEBUG_PRINTLN("[WARNING] Hardware failure detected, attempting recovery...");

    if (!i2cInitialized)
    {
        recoverI2C();
    }

    if (i2cInitialized && !mcpInitialized)
    {
        recoverMCP23017();
    }

    if (isHardwareReady())
    {
        DEBUG_PRINTLN("[INFO] Hardware recovery successful");
        setupIndicatorPins();
    }
    else
    {
        DEBUG_PRINTLN("[ERROR] Hardware recovery failed");
    }
}

void HardwareManager::reset()
{
    DEBUG_PRINTLN("[INFO] Resetting hardware...");

    // Reset LED
    if (ledInitialized && atomLed)
    {
        atomLed->clear();
        atomLed->show();
    }

    // Reset MCP23017 if possible
    if (mcpInitialized && mcp)
    {
        // Reset all pins to default state
        for (int i = 0; i < 16; i++)
        {
            mcp->pinMode(i, INPUT);
        }
    }

    DEBUG_PRINTLN("[INFO] Hardware reset completed");
}

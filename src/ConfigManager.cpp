#include "ConfigManager.h"

ConfigManager::ConfigManager()
    : antState(false), autoState(false), currentCivModel(DEFAULT_CIV_MODEL),
      deviceNumber(1), civAddress(CIV_BASE_ADDRESS + 1)
{
}

ConfigManager::~ConfigManager()
{
    // Preferences automatically close when destroyed
}

bool ConfigManager::begin()
{
    DEBUG_PRINTLN("[INFO] Initializing ConfigManager...");

    // Test preferences access
    if (!devicePrefs.begin(PREFS_DEVICE_NAMESPACE, false))
    {
        DEBUG_PRINTLN("[ERROR] Failed to initialize device preferences");
        return false;
    }
    devicePrefs.end();

    loadAllSettings();

    DEBUG_PRINTLN("[INFO] ConfigManager initialized successfully");
    return true;
}

void ConfigManager::loadAllSettings()
{
    // Load device number and calculate CI-V address
    devicePrefs.begin(PREFS_DEVICE_NAMESPACE, false);
    if (devicePrefs.isKey("deviceNumber"))
    {
        deviceNumber = devicePrefs.getInt("deviceNumber", 1);
    }
    else
    {
        deviceNumber = 1;
        devicePrefs.putInt("deviceNumber", deviceNumber);
    }
    devicePrefs.end();

    deviceNumber = constrain(deviceNumber, MIN_DEVICE_NUMBER, MAX_DEVICE_NUMBER);
    updateCivAddress();

    // Load CI-V model
    civModelPrefs.begin(PREFS_CIV_MODEL_NAMESPACE, false);
    currentCivModel = civModelPrefs.getString("model", DEFAULT_CIV_MODEL);
    civModelPrefs.end();

    // Load button states
    loadLatchedStates();

    DEBUG_PRINTF("[INFO] Configuration loaded - Device: %d, Model: %s, CIV: 0x%02X\n",
                 deviceNumber, currentCivModel.c_str(), civAddress);
}

void ConfigManager::setDeviceNumber(uint8_t number)
{
    uint8_t newNumber = constrain(number, MIN_DEVICE_NUMBER, MAX_DEVICE_NUMBER);

    if (newNumber != deviceNumber)
    {
        deviceNumber = newNumber;
        updateCivAddress();

        // Save to preferences
        devicePrefs.begin(PREFS_DEVICE_NAMESPACE, false);
        devicePrefs.putInt("deviceNumber", deviceNumber);
        devicePrefs.end();

        DEBUG_PRINTF("[INFO] Device number updated to %d (CI-V: 0x%02X)\n",
                     deviceNumber, civAddress);
    }
}

void ConfigManager::updateCivAddress()
{
    civAddress = CIV_BASE_ADDRESS + deviceNumber;
}

bool ConfigManager::setCivModel(const String &model)
{
    if (model == currentCivModel)
    {
        return true; // No change needed
    }

    DEBUG_PRINTF("[INFO] Attempting to change CI-V model from %s to %s\n",
                 currentCivModel.c_str(), model.c_str());

    civModelPrefs.begin(PREFS_CIV_MODEL_NAMESPACE, false);
    size_t bytesWritten = civModelPrefs.putString("model", model);
    civModelPrefs.end();

    if (bytesWritten > 0)
    {
        String oldModel = currentCivModel;
        currentCivModel = model;

        // Verify the save by reading it back
        civModelPrefs.begin(PREFS_CIV_MODEL_NAMESPACE, false);
        String verifyModel = civModelPrefs.getString("model", "ERROR");
        civModelPrefs.end();

        if (verifyModel == model)
        {
            DEBUG_PRINTF("[INFO] CI-V model successfully changed to %s (%d bytes)\n",
                         currentCivModel.c_str(), bytesWritten);
            return true;
        }
        else
        {
            // Rollback on verification failure
            currentCivModel = oldModel;
            DEBUG_PRINTF("[ERROR] CI-V model verification failed! Expected: %s, Got: %s\n",
                         model.c_str(), verifyModel.c_str());
            return false;
        }
    }
    else
    {
        DEBUG_PRINTF("[ERROR] Failed to save CI-V model to preferences\n");
        return false;
    }
}

void ConfigManager::setAntState(bool state)
{
    if (antState != state)
    {
        antState = state;
        saveLatchedStates();
        DEBUG_PRINTF("[INFO] ANT state changed to %s\n", state ? "ANT 2" : "ANT 1");
    }
}

void ConfigManager::setAutoState(bool state)
{
    if (autoState != state)
    {
        autoState = state;
        saveLatchedStates();
        DEBUG_PRINTF("[INFO] AUTO state changed to %s\n", state ? "AUTO" : "SEMI");
    }
}

void ConfigManager::loadLatchedStates()
{
    configPrefs.begin(PREFS_CONFIG_NAMESPACE, false);
    antState = configPrefs.getBool("ant", false);
    autoState = configPrefs.getBool("auto", false);
    configPrefs.end();

    DEBUG_PRINTF("[INFO] Latched states loaded - ANT: %s, AUTO: %s\n",
                 antState ? "ANT 2" : "ANT 1",
                 autoState ? "AUTO" : "SEMI");
}

void ConfigManager::saveLatchedStates()
{
    configPrefs.begin(PREFS_CONFIG_NAMESPACE, false);
    configPrefs.putBool("ant", antState);
    configPrefs.putBool("auto", autoState);
    configPrefs.end();

    DEBUG_PRINTF("[DEBUG] Latched states saved - ANT: %s, AUTO: %s\n",
                 antState ? "ANT 2" : "ANT 1",
                 autoState ? "AUTO" : "SEMI");
}

bool ConfigManager::hasWifiCredentials()
{
    wifiPrefs.begin(PREFS_WIFI_NAMESPACE, true); // Read-only
    bool hasSSID = wifiPrefs.isKey("ssid");
    bool hasPassword = wifiPrefs.isKey("password");
    wifiPrefs.end();

    return hasSSID && hasPassword;
}

void ConfigManager::clearWifiCredentials()
{
    wifiPrefs.begin(PREFS_WIFI_NAMESPACE, false);
    wifiPrefs.clear();
    wifiPrefs.end();

    DEBUG_PRINTLN("[INFO] WiFi credentials cleared");
}

void ConfigManager::printConfiguration()
{
    DEBUG_PRINTLN("========== CONFIGURATION ==========");
    DEBUG_PRINTF("Project: %s v%s\n", PROJECT_NAME, PROJECT_VERSION);
    DEBUG_PRINTF("Build: %s\n", FIRMWARE_BUILD_DATE);
    DEBUG_PRINTF("Device Number: %d\n", deviceNumber);
    DEBUG_PRINTF("CI-V Address: 0x%02X\n", civAddress);
    DEBUG_PRINTF("CI-V Model: %s\n", currentCivModel.c_str());
    DEBUG_PRINTF("ANT State: %s\n", antState ? "ANT 2" : "ANT 1");
    DEBUG_PRINTF("AUTO State: %s\n", autoState ? "AUTO" : "SEMI");
    DEBUG_PRINTF("Model Type: %s\n", isModelMomentary() ? "Momentary" : "Latching");
    DEBUG_PRINTLN("===================================");
}

String ConfigManager::getConfigurationJson()
{
    String json = "{";
    json += "\"project_name\":\"" + String(PROJECT_NAME) + "\",";
    json += "\"version\":\"" + String(PROJECT_VERSION) + "\",";
    json += "\"build_date\":\"" + String(FIRMWARE_BUILD_DATE) + "\",";
    json += "\"device_number\":" + String(deviceNumber) + ",";
    json += "\"civ_address\":\"0x" + String(civAddress, HEX) + "\",";
    json += "\"civ_model\":\"" + currentCivModel + "\",";
    json += "\"ant_state\":\"" + String(antState ? "ANT 2" : "ANT 1") + "\",";
    json += "\"auto_state\":\"" + String(autoState ? "AUTO" : "SEMI") + "\",";
    json += "\"ant_button_momentary\":" + String(isModelMomentary() ? "true" : "false");
    json += "}";

    return json;
}

void ConfigManager::resetToDefaults()
{
    DEBUG_PRINTLN("[INFO] Resetting configuration to defaults...");

    // Reset device settings
    setDeviceNumber(1);
    setCivModel(DEFAULT_CIV_MODEL);

    // Reset button states
    resetButtonStates();

    DEBUG_PRINTLN("[INFO] Configuration reset to defaults");
}

void ConfigManager::resetButtonStates()
{
    antState = false;
    autoState = false;
    saveLatchedStates();

    DEBUG_PRINTLN("[INFO] Button states reset to defaults");
}

bool ConfigManager::validateConfiguration()
{
    bool valid = true;

    // Validate device number
    if (deviceNumber < MIN_DEVICE_NUMBER || deviceNumber > MAX_DEVICE_NUMBER)
    {
        DEBUG_PRINTF("[ERROR] Invalid device number: %d\n", deviceNumber);
        valid = false;
    }

    // Validate CI-V model
    if (currentCivModel.length() == 0)
    {
        DEBUG_PRINTLN("[ERROR] Empty CI-V model");
        valid = false;
    }

    // Validate CI-V address calculation
    uint8_t expectedAddress = CIV_BASE_ADDRESS + deviceNumber;
    if (civAddress != expectedAddress)
    {
        DEBUG_PRINTF("[ERROR] CI-V address mismatch. Expected: 0x%02X, Got: 0x%02X\n",
                     expectedAddress, civAddress);
        valid = false;
    }

    if (valid)
    {
        DEBUG_PRINTLN("[INFO] Configuration validation passed");
    }
    else
    {
        DEBUG_PRINTLN("[ERROR] Configuration validation failed");
    }

    return valid;
}

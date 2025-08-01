#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

class ConfigManager
{
private:
    // Preferences instances
    Preferences wifiPrefs;
    Preferences configPrefs;
    Preferences devicePrefs;
    Preferences civModelPrefs;

    // Cached values
    bool antState;
    bool autoState;
    String currentCivModel;
    uint8_t deviceNumber;
    uint8_t civAddress;

    // Helper methods
    void updateCivAddress();

public:
    ConfigManager();
    ~ConfigManager();

    // Initialization
    bool begin();
    void loadAllSettings();

    // Device configuration
    void setDeviceNumber(uint8_t number);
    uint8_t getDeviceNumber() const { return deviceNumber; }
    uint8_t getCivAddress() const { return civAddress; }

    // CI-V Model configuration
    bool setCivModel(const String &model);
    String getCurrentCivModel() const { return currentCivModel; }
    bool isModelMomentary() const { return currentCivModel.indexOf("998") >= 0; }

    // Button states
    void setAntState(bool state);
    void setAutoState(bool state);
    bool getAntState() const { return antState; }
    bool getAutoState() const { return autoState; }
    void loadLatchedStates();
    void saveLatchedStates();

    // WiFi configuration (placeholder for future expansion)
    bool hasWifiCredentials();
    void clearWifiCredentials();

    // Debug and status
    void printConfiguration();
    String getConfigurationJson();

    // Reset functions
    void resetToDefaults();
    void resetButtonStates();

    // Validation
    bool validateConfiguration();
};

#endif // CONFIG_MANAGER_H

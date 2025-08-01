#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =========================================================================
// HARDWARE CONFIGURATION
// =========================================================================

// ESP32-S3 ATOM S3 Lite Configuration
#define ATOM_LED_PIN 35
#define ATOM_NUM_LEDS 1
#define I2C_SDA_PIN 38
#define I2C_SCL_PIN 39
#define I2C_CLOCK_SPEED 100000

// MCP23017 Configuration
#define MCP23017_ADDRESS 0x27

// =========================================================================
// MCP23017 GPIO PIN MAPPINGS
// =========================================================================

// Input pins (indicators)
#define MCP_TUNING_PIN 0 // PA0 (9PIN #1) - Tuning indicator
#define MCP_SWR_PIN 5    // PA5 (9PIN #6) - SWR indicator

// Output pins (button controls)
#define BUTTON_CDN_PIN 1  // PA1 (9PIN #2) - Capacitor Down
#define BUTTON_LDN_PIN 3  // PA3 (9PIN #4) - Inductor Down
#define BUTTON_TUNE_PIN 4 // PA4 (9PIN #5) - Tune
#define BUTTON_CUP_PIN 6  // PA6 (9PIN #7) - Capacitor Up
#define BUTTON_ANT_PIN 7  // PA7 (9PIN #8) - Antenna
#define BUTTON_LUP_PIN 8  // PB0 (9PIN #9) - Inductor Up
#define BUTTON_AUTO_PIN 9 // PB1 (9PIN #10) - Auto (not used in this version)

// =========================================================================
// PROJECT METADATA
// =========================================================================

#define PROJECT_NAME "ShackMate - Tuner Control"
#define PROJECT_VERSION "2.1.0"
#define PROJECT_AUTHOR "Half Baked Circuits"
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// =========================================================================
// NETWORK CONFIGURATION
// =========================================================================

#define MDNS_NAME "shackmate-tuner"
#define UDP_DISCOVERY_PORT 4210
#define WEBSOCKET_PORT 4000
#define HTTP_PORT 80
#define AP_NAME "shackmate-tuner"

// =========================================================================
// CI-V CONFIGURATION
// =========================================================================

#define DEFAULT_CIV_MODEL "991-994"
#define CIV_BASE_ADDRESS 0xB3
#define MIN_DEVICE_NUMBER 1
#define MAX_DEVICE_NUMBER 4

// =========================================================================
// TIMING CONFIGURATION
// =========================================================================

#define DASHBOARD_UPDATE_INTERVAL 1000 // ms
#define STATE_UPDATE_INTERVAL 5000     // ms
#define DISCOVERY_INTERVAL 30000       // ms
#define LED_BLINK_FAST 100             // ms
#define LED_BLINK_SLOW 500             // ms
#define WATCHDOG_TIMEOUT 30            // seconds
#define WIFI_CONNECT_TIMEOUT 30        // seconds

// =========================================================================
// BUTTON CONFIGURATION
// =========================================================================

#define BUTTON_COUNT 6
#define MOMENTARY_ACTION_COUNT 8

// Button indices for arrays
enum ButtonIndex
{
    BTN_IDX_CUP = 0,
    BTN_IDX_CDN = 1,
    BTN_IDX_LUP = 2,
    BTN_IDX_LDN = 3,
    BTN_IDX_TUNE = 4,
    BTN_IDX_ANT = 5,
    BTN_IDX_AUTO = 6
};

// =========================================================================
// LED STATUS COLORS
// =========================================================================

struct RGBColor
{
    uint8_t r, g, b;

    RGBColor() : r(0), g(0), b(0) {}
    RGBColor(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
};

// Predefined colors
namespace Colors
{
    const RGBColor OFF(0, 0, 0);
    const RGBColor RED(255, 0, 0);
    const RGBColor GREEN(0, 255, 0);
    const RGBColor BLUE(0, 0, 255);
    const RGBColor PURPLE(128, 0, 128);
    const RGBColor WHITE(255, 255, 255);
    const RGBColor YELLOW(255, 255, 0);
}

// =========================================================================
// PREFERENCES NAMESPACES
// =========================================================================

#define PREFS_WIFI_NAMESPACE "wifi"
#define PREFS_CONFIG_NAMESPACE "config"
#define PREFS_DEVICE_NAMESPACE "device"
#define PREFS_CIV_MODEL_NAMESPACE "civmodel"

// =========================================================================
// DEBUG CONFIGURATION - ENABLED FOR DEBUGGING
// =========================================================================

// Enable debug output for troubleshooting
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)

// =========================================================================
// ERROR CODES
// =========================================================================

enum ErrorCode
{
    ERR_NONE = 0,
    ERR_WIFI_FAILED = 1,
    ERR_LITTLEFS_FAILED = 2,
    ERR_MCP23017_FAILED = 3,
    ERR_WEBSOCKET_FAILED = 4,
    ERR_OTA_FAILED = 5
};

#endif // CONFIG_H

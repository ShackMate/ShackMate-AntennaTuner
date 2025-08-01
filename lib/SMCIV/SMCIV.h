#ifndef SMCIV_H
#define SMCIV_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <vector>

class SMCIV
{
public:
    // Callback function type for antenna state changes
    typedef void (*AntennaStateCallback)(uint8_t antennaPort, uint8_t rcsType);
    // Callback function type for GPIO antenna output control
    typedef void (*GpioOutputCallback)(uint8_t antennaIndex);
    // Callback function type for CI-V response sending
    typedef void (*CivResponseCallback)(const String &hexResponse);

    SMCIV();

    // Initialize with WebSocket client pointer and CI-V address pointer
    void begin(WebSocketsClient *client, uint8_t *civAddrPtr);

    // Main loop to be called regularly
    void loop();

    // Connect to remote WebSocket server by IP and port
    void connectToRemoteWs(const String &ip, uint16_t port);

    // Handle WebSocket client events
    void handleWsClientEvent(WStype_t type, uint8_t *payload, size_t length);

    // Process incoming WebSocket messages (hex encoded ASCII)
    void handleIncomingWsMessage(const String &asciiHex);

    // Send a CI-V response for given command and subcommand from specific address
    void sendCivResponse(uint8_t cmd, uint8_t subcmd, uint8_t fromAddr);

    // Set the switch model type: 0 for RCS-8, 1 for RCS-10
    void setRcsType(uint8_t value);

    // Public getter and setter for antenna port
    void setSelectedAntennaPort(uint8_t port); // port range depends on rcsType (0-4 for RCS-8, 0-7 for RCS-10)
    uint8_t getSelectedAntennaPort();

    // Broadcast antenna state JSON over WebSocket
    void broadcastAntennaState();

    // Set callback function for antenna state changes
    void setAntennaStateCallback(AntennaStateCallback callback);

    // Set callback function for GPIO output control
    void setGpioOutputCallback(GpioOutputCallback callback);

    // Set callback function for CI-V response sending
    void setCivResponseCallback(CivResponseCallback callback);

    // =========================================================================
    // ANTENNA TUNER SPECIFIC COMMANDS
    // =========================================================================

    // Callback function types for antenna tuner integration
    typedef void (*TunerButtonCallback)(uint8_t buttonCode);       // For CMD 34 button presses
    typedef bool (*TunerIndicatorCallback)(uint8_t indicatorType); // For CMD 33 indicator reads
    typedef String (*TunerModelCallback)();                        // For CMD 30 model reads
    typedef bool (*TunerModelSetCallback)(uint8_t modelCode);      // For CMD 30 model sets

    // Set callback functions for antenna tuner integration
    void setTunerButtonCallback(TunerButtonCallback callback);
    void setTunerIndicatorCallback(TunerIndicatorCallback callback);
    void setTunerModelCallback(TunerModelCallback callback);
    void setTunerModelSetCallback(TunerModelSetCallback callback);

    // Handle antenna tuner specific CI-V commands
    void handleTunerCommand(uint8_t cmd, uint8_t subcmd, uint8_t fromAddr, const uint8_t *data, size_t dataLen);

private:
    WebSocketsClient *wsClient = nullptr;
    uint8_t *civAddressPtr = nullptr;
    AntennaStateCallback antennaCallback = nullptr;
    GpioOutputCallback gpioCallback = nullptr;
    CivResponseCallback civResponseCallback = nullptr;

    // Antenna tuner callbacks
    TunerButtonCallback tunerButtonCallback = nullptr;
    TunerIndicatorCallback tunerIndicatorCallback = nullptr;
    TunerModelCallback tunerModelCallback = nullptr;
    TunerModelSetCallback tunerModelSetCallback = nullptr;

    // Helper to format byte array to uppercase hex string
    static String formatBytesToHex(const uint8_t *data, size_t len);

    // Helper to send CI-V hex response via callback or WebSocket
    void sendCivHexResponse(const uint8_t *response, size_t length);

private:
    uint8_t calculateChecksum(uint8_t *data, size_t length);
    void sendResponse(const uint8_t *response, size_t length);

    uint8_t selectedAntennaPort = 1; // zero-based index of selected antenna port (default 1)
    uint8_t rcsType = 0;             // Switch Model: 0 for RCS-8 (5 ports), 1 for RCS-10 (8 ports)
};

#endif // SMCIV_H
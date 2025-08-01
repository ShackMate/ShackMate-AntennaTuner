#include "SMCIV.h"
#include <Preferences.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <WiFi.h>
#include "../../include/Config.h"

// Preferences storage for antenna port selection
static Preferences antennaPrefs;
// Preferences storage for configuration (including RCS type)
static Preferences configPrefs;

SMCIV::SMCIV()
{
    wsClient = nullptr;
    civAddressPtr = nullptr;
    selectedAntennaPort = 0;
    rcsType = 0;
}

void SMCIV::begin(WebSocketsClient *client, unsigned char *civAddrPtr)
{
    wsClient = client;
    civAddressPtr = civAddrPtr;
    // Load selectedAntennaPort from NVS (default to 0, which represents port 1)
    antennaPrefs.begin("switch", true);
    selectedAntennaPort = antennaPrefs.getInt("selectedIndex", 0); // default to 0 (zero-based, represents port 1)
    antennaPrefs.end();
    Serial.printf("[DEBUG] NVS initial load: selectedAntennaPort=%u (represents port %u)\n", selectedAntennaPort, selectedAntennaPort + 1);
}

void SMCIV::loop()
{
    // Placeholder for future periodic tasks
}

void SMCIV::connectToRemoteWs(const String &host, unsigned short port)
{
    if (wsClient)
    {
        Serial.printf("[CI-V] Connecting to WS server at %s:%u\n", host.c_str(), port);
        wsClient->begin(host, port, "/");
        wsClient->onEvent([this](WStype_t type, uint8_t *payload, size_t length)
                          { this->handleWsClientEvent(type, payload, length); });
    }
}

// Format a byte array as a hex string (uppercase, space separated)
String SMCIV::formatBytesToHex(const uint8_t *data, size_t length)
{
    String result;
    char hexbuf[3]; // 2 chars + null terminator
    for (size_t i = 0; i < length; ++i)
    {
        if (i > 0)
            result += " ";
        sprintf(hexbuf, "%02X", data[i]);
        result += hexbuf;
    }
    result.toUpperCase();
    return result;
}

// Send a CI-V response for the given command and subcommand
void SMCIV::sendCivResponse(uint8_t cmd, uint8_t subcmd, uint8_t fromAddr)
{
    uint8_t civAddr = civAddressPtr ? *civAddressPtr : 0xB8;

    // Debug prints to confirm command/subcommand, civAddr, and WiFi IP
    Serial.printf("[CI-V] sendCivResponse called with cmd=0x%02X, subcmd=0x%02X, fromAddr=0x%02X\n", cmd, subcmd, fromAddr);
    Serial.printf("[CI-V] civAddr value: 0x%02X\n", civAddr);
    Serial.printf("[CI-V] WiFi IP: %s\n", WiFi.localIP().toString().c_str());

    if (cmd == 0x19 && subcmd == 0x01)
    {
        IPAddress ip = WiFi.localIP();
        uint8_t response[11] = {
            0xFE, 0xFE, 0xEE, civAddr,
            0x19, 0x01,                 // Echo back the original command
            ip[0], ip[1], ip[2], ip[3], // IP address bytes
            0xFD};

        Serial.printf("[CI-V] Sending IP response with command echo: %s\n", formatBytesToHex(response, 11).c_str());
        sendCivHexResponse(response, 11);
        return;
    }

    if (cmd == 0x19 && subcmd == 0x00)
    {
        uint8_t response[8] = {0xFE, 0xFE, 0xEE, civAddr, 0x19, 0x00, civAddr, 0xFD};
        Serial.printf("[CI-V] Sending 19 00 response to broadcast (EE) from our address (0x%02X): %s\n",
                      civAddr, formatBytesToHex(response, 8).c_str());
        sendCivHexResponse(response, 8);
        return;
    }

    if (cmd == 0x30 && (subcmd == 0x00 || subcmd == 0x01))
    {
        Serial.printf("[DEBUG] rcsType before sending 0x30 response: %u\n", rcsType);
        uint8_t response[] = {0xFE, 0xFE, fromAddr, civAddr, 0x30, rcsType, 0xFD};
        Serial.printf("[CI-V] Sending 30 read/set response (rcsType as 6th byte): %s\n", formatBytesToHex(response, sizeof(response)).c_str());
        sendCivHexResponse(response, sizeof(response));
        return;
    }

    if (cmd == 0x31)
    {
        if (subcmd == 0x00)
        {
            uint8_t selectedPort = getSelectedAntennaPort() + 1;
            Serial.printf("[CI-V] Responding to 31 read with antenna port: %u\n", selectedPort);
            uint8_t response[] = {0xFE, 0xFE, fromAddr, civAddr, 0x31, selectedPort, 0xFD};
            if (wsClient)
            {
                String hexMsg = formatBytesToHex(response, sizeof(response));
                sendCivHexResponse(response, sizeof(response));
            }
            return;
        }
        else if (subcmd >= 1 && subcmd <= 8)
        {
            uint8_t newPort = subcmd;
            bool valid = false;
            if (rcsType == 0 && newPort >= 1 && newPort <= 5)
                valid = true;
            if (rcsType == 1 && newPort >= 1 && newPort <= 8)
                valid = true;
            if (valid)
            {
                setSelectedAntennaPort(newPort - 1); // Store zero-based internally and save to NVS
                Serial.printf("[CI-V] Antenna port set to: %u (saved to NVS)\n", newPort);
                uint8_t response[] = {0xFE, 0xFE, fromAddr, civAddr, 0x31, newPort, 0xFD};
                if (wsClient)
                {
                    String hexMsg = formatBytesToHex(response, sizeof(response));
                    sendCivHexResponse(response, sizeof(response));
                }
                broadcastAntennaState();
            }
            else
            {
                uint8_t response[] = {0xFE, 0xFE, 0xEE, civAddr, 0xFA, 0xFD};
                if (wsClient)
                {
                    String hexMsg = formatBytesToHex(response, sizeof(response));
                    sendCivHexResponse(response, sizeof(response));
                }
            }
            return;
        }
    }

    // Default fallback response (never respond to 19 01 here)
    if (!(cmd == 0x19 && subcmd == 0x01))
    {
        uint8_t fallbackSubcmd = subcmd;
        uint8_t response[8] = {0xFE, 0xFE, fromAddr, civAddr, cmd, fallbackSubcmd, civAddr, 0xFD};
        if (wsClient)
        {
            String hexMsg = formatBytesToHex(response, sizeof(response));
            sendCivHexResponse(response, sizeof(response));
        }
    }
}

uint8_t SMCIV::getSelectedAntennaPort()
{
    Serial.printf("[DEBUG] getSelectedAntennaPort() returns %u\n", selectedAntennaPort);
    return selectedAntennaPort;
}

void SMCIV::setSelectedAntennaPort(uint8_t port)
{
    Serial.printf("[DEBUG] setSelectedAntennaPort() called: input port=%u, current rcsType=%u\n", port, rcsType);
    bool valid = false;
    if (rcsType == 0 && port <= 4)
        valid = true;
    if (rcsType == 1 && port <= 7)
        valid = true;

    if (!valid)
    {
        Serial.printf("[SMCIV] Attempted to set invalid antenna port %u for rcsType %u\n", port, rcsType);
        return;
    }

    selectedAntennaPort = port;
    Serial.printf("[SMCIV] setSelectedAntennaPort updated, new value: %u\n", selectedAntennaPort);
    Serial.printf("[DEBUG] Writing selectedIndex=%u to NVS...\n", selectedAntennaPort);
    antennaPrefs.begin("switch", false);
    antennaPrefs.putInt("selectedIndex", selectedAntennaPort);
    antennaPrefs.end();
    Serial.println("[DEBUG] NVS write complete.");

    // Call GPIO callback to update physical outputs
    if (gpioCallback)
    {
        gpioCallback(selectedAntennaPort);
    }

    broadcastAntennaState();
}

void SMCIV::broadcastAntennaState()
{
    // CI-V WebSocket is for hex-encoded CI-V messages only, not JSON
    // If JSON broadcasting is needed, it should be handled by the main application
    // via a separate WebSocket server for web UI clients
    Serial.printf("[SMCIV] Antenna state changed to port %u (zero-based), external port %u\n",
                  selectedAntennaPort, selectedAntennaPort + 1);

    // Call the registered callback to notify the main application
    if (antennaCallback)
    {
        antennaCallback(selectedAntennaPort, rcsType);
    }
}

void SMCIV::setAntennaStateCallback(AntennaStateCallback callback)
{
    antennaCallback = callback;
    Serial.println("[SMCIV] Antenna state callback registered");
}

void SMCIV::setGpioOutputCallback(GpioOutputCallback callback)
{
    gpioCallback = callback;
    Serial.println("[SMCIV] GPIO output callback registered");
}

void SMCIV::setCivResponseCallback(CivResponseCallback callback)
{
    civResponseCallback = callback;
    Serial.println("[SMCIV] CI-V response callback registered");
}

void SMCIV::sendCivHexResponse(const uint8_t *response, size_t length)
{
    String hexMsg = formatBytesToHex(response, length);
    if (civResponseCallback)
    {
        civResponseCallback(hexMsg);
    }
    else if (wsClient)
    {
        wsClient->sendTXT(hexMsg);
    }
}

void SMCIV::setRcsType(uint8_t value)
{
    if (value <= 1) // Valid values are 0 (RCS-8) or 1 (RCS-10)
    {
        rcsType = value;
        Serial.printf("[SMCIV] RCS type set to %u (%s)\n", rcsType, (rcsType == 0) ? "RCS-8" : "RCS-10");

        // Validate current antenna port against new RCS type limits
        uint8_t maxPort = (rcsType == 0) ? 4 : 7; // RCS-8: 0-4, RCS-10: 0-7
        if (selectedAntennaPort > maxPort)
        {
            Serial.printf("[SMCIV] Current antenna port %u exceeds limit for RCS type %u, resetting to 0\n", selectedAntennaPort, rcsType);
            setSelectedAntennaPort(0);
        }
    }
    else
    {
        Serial.printf("[SMCIV] Invalid RCS type %u, must be 0 or 1\n", value);
    }
}

void SMCIV::handleIncomingWsMessage(const String &asciiHex)
{
    Serial.printf("[CI-V] Received WS message (raw): %s\n", asciiHex.c_str());

    // Ignore JSON messages - CI-V WebSocket is for hex-encoded messages only
    if (asciiHex.startsWith("{") || asciiHex.startsWith("["))
    {
        Serial.println("[CI-V] Ignored: JSON message received on CI-V WebSocket");
        return;
    }

    std::vector<uint8_t> bytes;
    int len = asciiHex.length();
    for (int i = 0; i < len;)
    {
        while (i < len && asciiHex[i] == ' ')
            ++i;
        if (i + 1 < len)
        {
            String sub = asciiHex.substring(i, i + 2);
            bytes.push_back(strtoul(sub.c_str(), nullptr, 16));
            i += 2;
        }
        else
        {
            break;
        }
        while (i < len && asciiHex[i] == ' ')
            ++i;
    }

    Serial.print("[CI-V] Parsed bytes: ");
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        char hexbuf[3];
        sprintf(hexbuf, "%02X", bytes[i]);
        Serial.print(hexbuf);
        if (i < bytes.size() - 1)
            Serial.print(" ");
    }
    Serial.println();

    if (bytes.size() < 5)
        return;

    Serial.print("[CI-V] Incoming command bytes: ");
    size_t lastCmd = bytes.size();
    if (lastCmd > 0 && bytes[lastCmd - 1] == 0xFD)
        lastCmd--;
    for (size_t i = 4; i < lastCmd; ++i)
    {
        char hexbuf[3];
        sprintf(hexbuf, "%02X", bytes[i]);
        Serial.print(hexbuf);
        if (i < lastCmd - 1)
            Serial.print(" ");
    }
    Serial.println();

    uint8_t toAddr = bytes[2];
    uint8_t fromAddr = bytes[3];
    uint8_t cmd = bytes[4];
    uint8_t myAddr = civAddressPtr ? *civAddressPtr : 0xB8;
    uint8_t subcmd = (bytes.size() > 5) ? bytes[5] : 0x00;

    DEBUG_PRINTF("[CI-V] To: 0x%02X, From: 0x%02X, MyAddr: 0x%02X, Cmd: 0x%02X, SubCmd: 0x%02X\n",
                 toAddr, fromAddr, myAddr, cmd, subcmd);

    // Always answer 19 00 requests from broadcast (0x00) or our own address, even if fromAddr == myAddr
    if (cmd != 0x19 || subcmd != 0x00)
    {
        if (toAddr == myAddr && fromAddr == myAddr)
        {
            // Suppress verbose print: Both DEST and SRC are my CI-V address, not replying.
            return;
        }
    }

    bool isBroadcast = (toAddr == 0x00 || toAddr == 0xEE);
    bool isMine = (toAddr == myAddr);

    DEBUG_PRINTF("[CI-V] isBroadcast: %s, isMine: %s\n", isBroadcast ? "true" : "false", isMine ? "true" : "false");

    // Check if this is a response (not a command) - responses should be ignored to prevent loops
    bool isResponse = false;
    if (cmd == 0x19 && subcmd == 0x00 && bytes.size() >= 7)
    {
        // For 19 00 responses, the data byte should match the FROM address
        uint8_t dataAddr = bytes[6];
        if (dataAddr == fromAddr)
        {
            isResponse = true;
            DEBUG_PRINTF("[CI-V] Detected 19 00 response (data addr 0x%02X matches from addr 0x%02X) - ignoring to prevent loop\n", dataAddr, fromAddr);
        }
    }
    else if (cmd == 0x19 && subcmd == 0x01 && bytes.size() >= 10)
    {
        // For 19 01 responses, this would contain IP data - responses are longer than commands
        isResponse = true;
        DEBUG_PRINTF("[CI-V] Detected 19 01 response (IP data) - ignoring\n");
    }

    if (isResponse)
    {
        DEBUG_PRINTF("[CI-V] Message is a response - ignoring to prevent infinite loop\n");
        return;
    }

    // Only process valid addressed/broadcast commands, but always process 19 00 from broadcast or our address
    if (
        (cmd == 0x19 && subcmd == 0x00 && (isBroadcast || isMine)) ||
        (cmd == 0x19 && subcmd == 0x01 && (isBroadcast || isMine)) ||
        (cmd == 0x30 && bytes.size() == 6 && bytes[5] == 0xFD && (isBroadcast || isMine)) ||
        (cmd == 0x31 && bytes.size() == 6 && bytes[5] == 0xFD && (isBroadcast || isMine)) ||
        (cmd == 0x33 && bytes.size() == 6 && bytes[5] == 0xFD && (isBroadcast || isMine)) ||
        (cmd == 0x34 && bytes.size() == 7 && bytes[6] == 0xFD && (isBroadcast || isMine)) ||
        isMine)
    {
        DEBUG_PRINTF("[CI-V] Command accepted for processing\n");
        // Allowed, continue
    }
    else
    {
        DEBUG_PRINTF("[CI-V] Command rejected - not addressed to us\n");
        // Suppress verbose print: Not addressed to us or not a valid broadcast read.
        return;
    }

    if (cmd == 0x19 && (subcmd == 0x00 || subcmd == 0x01))
    {
        sendCivResponse(cmd, subcmd, fromAddr);
        return;
    }

    if (cmd == 0x30)
    {
        if (bytes.size() == 6 && bytes[5] == 0xFD && (isBroadcast || isMine))
        {
            // This is a tuner model read command (CMD 30 with no subcmd)
            // Call tuner command handler for model read
            const uint8_t *dataPtr = nullptr;                    // No data for read
            handleTunerCommand(cmd, 0x01, fromAddr, dataPtr, 0); // Use subcmd 0x01 for read
            return;
        }
        if (bytes.size() == 7 && (bytes[5] == 0x00 || bytes[5] == 0x01) && bytes[6] == 0xFD && isMine)
        {
            // This is a tuner model set command (CMD 30 with data)
            // Call tuner command handler for model set
            const uint8_t *dataPtr = &bytes[5];                  // Data byte (0x00 or 0x01)
            handleTunerCommand(cmd, 0x00, fromAddr, dataPtr, 1); // Use subcmd 0x00 for set
            return;
        }
        if (isBroadcast && bytes.size() == 7 && (bytes[5] == 0x00 || bytes[5] == 0x01) && bytes[6] == 0xFD)
        {
            // Broadcast set commands are not allowed for tuner model - send NAK
            uint8_t response[] = {0xFE, 0xFE, fromAddr, myAddr, 0x30, 0xFA, 0xFD};
            DEBUG_PRINTF("[CI-V] Rejecting broadcast SET command 30 with FA\n");
            sendCivHexResponse(response, sizeof(response));
            return;
        }
    }

    if (cmd == 0x31)
    {
        if (bytes.size() == 6 && bytes[5] == 0xFD && (isMine || isBroadcast))
        {
            uint8_t selectedPort = getSelectedAntennaPort() + 1; // one-based
            uint8_t response[] = {0xFE, 0xFE, fromAddr, myAddr, 0x31, selectedPort, 0xFD};
            if (wsClient)
            {
                String hexMsg = formatBytesToHex(response, sizeof(response));
                sendCivHexResponse(response, sizeof(response));
            }
            return;
        }
        else if (bytes.size() == 7 && bytes[6] == 0xFD && (isMine || isBroadcast))
        {
            uint8_t newPort = bytes[5];
            bool valid = false;
            if (rcsType == 0 && newPort >= 1 && newPort <= 5)
                valid = true;
            if (rcsType == 1 && newPort >= 1 && newPort <= 8)
                valid = true;
            if (valid)
            {
                setSelectedAntennaPort(newPort - 1); // store zero-based and save to NVS
                Serial.printf("[CI-V] Antenna port set to: %u (saved to NVS)\n", newPort);
                uint8_t response[] = {0xFE, 0xFE, fromAddr, myAddr, 0x31, newPort, 0xFD};
                if (wsClient)
                {
                    String hexMsg = formatBytesToHex(response, sizeof(response));
                    sendCivHexResponse(response, sizeof(response));
                }
                broadcastAntennaState();
            }
            else
            {
                uint8_t response[] = {0xFE, 0xFE, 0xEE, myAddr, 0xFA, 0xFD};
                if (wsClient)
                {
                    String hexMsg = formatBytesToHex(response, sizeof(response));
                    sendCivHexResponse(response, sizeof(response));
                }
            }
            return;
        }
    }

    // =========================================================================
    // ANTENNA TUNER COMMANDS (CMD 30 with different subcmds, 33, 34)
    // =========================================================================

    // Handle antenna tuner model commands (CMD 30 with specific subcmds)
    if (cmd == 0x30 && bytes.size() >= 6)
    {
        // Check for tuner-specific subcmds that differ from RCS switch commands
        if (bytes[5] == 0x01 && bytes.size() == 7 && bytes[6] == 0xFD) // Read Model
        {
            // Call tuner command handler for model read
            const uint8_t *dataPtr = nullptr; // No data for read
            handleTunerCommand(cmd, 0x01, fromAddr, dataPtr, 0);
            return;
        }
        else if (bytes[5] == 0x00 && bytes.size() == 8 && bytes[7] == 0xFD) // Set Model with data
        {
            // Call tuner command handler for model set
            const uint8_t *dataPtr = &bytes[6]; // Data byte
            handleTunerCommand(cmd, 0x00, fromAddr, dataPtr, 1);
            return;
        }
    }

    // Handle LED indicator read commands (CMD 33)
    if (cmd == 0x33 && bytes.size() == 6 && bytes[5] == 0xFD)
    {
        Serial.println("[DEBUG] Command 33 handler reached - calling tuner command handler");
        // Simple read command: FE FE addr addr 33 FD
        const uint8_t *dataPtr = nullptr; // No data for read
        handleTunerCommand(cmd, 0x01, fromAddr, dataPtr, 0);
        return;
    }

    // Handle remote tuner button commands (CMD 34)
    if (cmd == 0x34 && bytes.size() == 7 && bytes[6] == 0xFD)
    {
        uint8_t buttonCode = bytes[5]; // Button code (00-06)

        // Validate button code range
        if (buttonCode > 0x06)
        {
            // Invalid button code - send error response with the invalid code
            uint8_t response[] = {0xFE, 0xFE, fromAddr, myAddr, 0x34, buttonCode, 0xFA, 0xFD};
            DEBUG_PRINTF("[CI-V] Invalid button code 0x%02X for command 34\n", buttonCode);
            sendCivHexResponse(response, sizeof(response));
            return;
        }

        // Check if this is a broadcast SET command - these are not allowed
        if (isBroadcast)
        {
            // Broadcast SET commands are rejected with FA (error)
            uint8_t response[] = {0xFE, 0xFE, fromAddr, myAddr, 0x34, buttonCode, 0xFA, 0xFD};
            DEBUG_PRINTF("[CI-V] Rejecting broadcast SET command 34 with FA\n");
            sendCivHexResponse(response, sizeof(response));
            return;
        }

        // Direct SET command - process normally
        const uint8_t *dataPtr = &buttonCode; // Button code
        handleTunerCommand(cmd, 0x00, fromAddr, dataPtr, 1);
        return;
    }

    if (!(cmd == 0x19 && subcmd == 0x01))
    {
        uint8_t response[8] = {0xFE, 0xFE, fromAddr, myAddr, cmd, subcmd, myAddr, 0xFD};
        if (wsClient)
        {
            String hexMsg = formatBytesToHex(response, sizeof(response));
            sendCivHexResponse(response, sizeof(response));
        }
    }
}

void SMCIV::handleWsClientEvent(WStype_t type, uint8_t *payload, size_t length)
{
    if (type == WStype_TEXT)
    {
        String textPayload = String((char *)payload);
        Serial.print("[WS CLIENT EVENT] Payload text: ");
        Serial.println(textPayload);
        handleIncomingWsMessage(textPayload);
    }
}

// =========================================================================
// ANTENNA TUNER COMMAND HANDLERS
// =========================================================================

void SMCIV::setTunerButtonCallback(TunerButtonCallback callback)
{
    tunerButtonCallback = callback;
}

void SMCIV::setTunerIndicatorCallback(TunerIndicatorCallback callback)
{
    tunerIndicatorCallback = callback;
}

void SMCIV::setTunerModelCallback(TunerModelCallback callback)
{
    tunerModelCallback = callback;
}

void SMCIV::setTunerModelSetCallback(TunerModelSetCallback callback)
{
    tunerModelSetCallback = callback;
}

void SMCIV::handleTunerCommand(uint8_t cmd, uint8_t subcmd, uint8_t fromAddr, const uint8_t *data, size_t dataLen)
{
    uint8_t civAddr = civAddressPtr ? *civAddressPtr : 0xB8;

    Serial.printf("[CI-V TUNER] Command: 0x%02X, SubCmd: 0x%02X, From: 0x%02X\n", cmd, subcmd, fromAddr);

    switch (cmd)
    {
    case 0x30: // Model commands
    {
        if (subcmd == 0x01) // Read Model
        {
            String model = tunerModelCallback ? tunerModelCallback() : "991-994";
            uint8_t modelData = (model.indexOf("998") >= 0) ? 0x01 : 0x00;

            uint8_t response[7] = {0xFE, 0xFE, fromAddr, civAddr, 0x30, modelData, 0xFD};
            Serial.printf("[CI-V TUNER] Model read response: %s (data: 0x%02X)\n", model.c_str(), modelData);

            if (wsClient)
            {
                String hexMsg = formatBytesToHex(response, 7);
                sendCivHexResponse(response, sizeof(response));
            }
        }
        else if (subcmd == 0x00 && dataLen > 0) // Set Model
        {
            uint8_t modelCode = data[0];
            bool success = false;

            // Validate model code first
            if (modelCode == 0x00 || modelCode == 0x01)
            {
                if (tunerModelSetCallback)
                {
                    success = tunerModelSetCallback(modelCode);
                }
            }
            else
            {
                // Invalid model code - force failure
                success = false;
                Serial.printf("[CI-V TUNER] Invalid model code: 0x%02X (valid: 0x00, 0x01)\n", modelCode);
            }

            if (success)
            {
                // Send ACK response: FE FE fromAddr civAddr FB FD
                uint8_t response[6] = {0xFE, 0xFE, fromAddr, civAddr, 0xFB, 0xFD};
                Serial.printf("[CI-V TUNER] Model set (code: 0x%02X): ACK\n", modelCode);

                if (wsClient)
                {
                    String hexMsg = formatBytesToHex(response, 6);
                    sendCivHexResponse(response, sizeof(response));
                }
            }
            else
            {
                // Send NAK response with original command and data: FE FE fromAddr civAddr 30 modelCode FA FD
                uint8_t response[8] = {0xFE, 0xFE, fromAddr, civAddr, 0x30, modelCode, 0xFA, 0xFD};
                Serial.printf("[CI-V TUNER] Model set (code: 0x%02X): NAK with echo\n", modelCode);

                if (wsClient)
                {
                    String hexMsg = formatBytesToHex(response, 8);
                    sendCivHexResponse(response, sizeof(response));
                }
            }
        }
        break;
    }

    case 0x33: // Read LED Indicators
    {
        Serial.printf("[DEBUG] handleTunerCommand case 0x33, subcmd: 0x%02X\n", subcmd);
        if (subcmd == 0x01) // Read indicators
        {
            uint8_t indicatorStatus = 0x00; // Default: all OFF
            Serial.println("[DEBUG] About to call tunerIndicatorCallback");

            if (tunerIndicatorCallback)
            {
                Serial.println("[DEBUG] tunerIndicatorCallback exists, calling for indicators");
                bool tuning = tunerIndicatorCallback(1); // 1 = tuning indicator
                bool swr = tunerIndicatorCallback(2);    // 2 = SWR indicator
                Serial.printf("[DEBUG] Callback results - tuning: %s, swr: %s\n", tuning ? "true" : "false", swr ? "true" : "false");

                if (tuning && swr)
                    indicatorStatus = 0x03; // Both ON
                else if (swr)
                    indicatorStatus = 0x02; // SWR ON
                else if (tuning)
                    indicatorStatus = 0x01; // Tuning ON
                // else indicatorStatus = 0x00 (already set)
            }
            else
            {
                Serial.println("[DEBUG] tunerIndicatorCallback is NULL!");
            }

            uint8_t response[7] = {0xFE, 0xFE, fromAddr, civAddr, 0x33, indicatorStatus, 0xFD};
            Serial.printf("[CI-V TUNER] Indicator read response: 0x%02X\n", indicatorStatus);
            Serial.printf("[DEBUG] Sending response: %s\n", formatBytesToHex(response, 7).c_str());

            if (wsClient)
            {
                String hexMsg = formatBytesToHex(response, 7);
                sendCivHexResponse(response, sizeof(response));
            }
            else
            {
                Serial.println("[DEBUG] wsClient is NULL!");
            }
        }
        else
        {
            Serial.printf("[DEBUG] Command 33 with unexpected subcmd: 0x%02X\n", subcmd);
        }
        break;
    }

    case 0x34: // Remote Tuner Buttons
    {
        if (dataLen > 0) // Button press command
        {
            uint8_t buttonCode = data[0];
            bool success = false;

            Serial.printf("[CI-V TUNER] Button press: 0x%02X\n", buttonCode);

            if (tunerButtonCallback)
            {
                tunerButtonCallback(buttonCode);
                success = true; // Assume success for button presses
            }

            // Send ACK/NAK with original command and button code
            uint8_t response[8] = {0xFE, 0xFE, fromAddr, civAddr, 0x34, buttonCode, (uint8_t)(success ? 0xFB : 0xFA), 0xFD};
            Serial.printf("[CI-V TUNER] Button press (code: 0x%02X): %s\n", buttonCode, success ? "ACK" : "NAK");

            if (wsClient)
            {
                String hexMsg = formatBytesToHex(response, 8);
                sendCivHexResponse(response, sizeof(response));
            }
        }
        break;
    }

    default:
    {
        // Unknown command - send NAK
        uint8_t response[6] = {0xFE, 0xFE, fromAddr, civAddr, 0xFA, 0xFD};
        Serial.printf("[CI-V TUNER] Unknown command: 0x%02X - NAK\n", cmd);

        if (wsClient)
        {
            String hexMsg = formatBytesToHex(response, 6);
            sendCivHexResponse(response, sizeof(response));
        }
        break;
    }
    }
}
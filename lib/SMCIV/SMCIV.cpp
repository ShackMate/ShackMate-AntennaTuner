#include "SMCIV.h"
#include <Preferences.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <WiFi.h>

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
    uint8_t civAddr = civAddressPtr ? *civAddressPtr : 0xB4;

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
        if (wsClient)
        {
            String hexMsg = formatBytesToHex(response, 11);
            wsClient->sendTXT(hexMsg);
        }
        return;
    }

    if (cmd == 0x19 && subcmd == 0x00)
    {
        uint8_t response[8] = {0xFE, 0xFE, fromAddr, civAddr, 0x19, 0x00, civAddr, 0xFD};
        Serial.printf("[CI-V] Sending 19 00 response (with fixed checksum 0xFD): %s\n", formatBytesToHex(response, 8).c_str());
        if (wsClient)
        {
            String hexMsg = formatBytesToHex(response, 8);
            wsClient->sendTXT(hexMsg);
        }
        return;
    }

    if (cmd == 0x30 && (subcmd == 0x00 || subcmd == 0x01))
    {
        Serial.printf("[DEBUG] rcsType before sending 0x30 response: %u\n", rcsType);
        uint8_t response[] = {0xFE, 0xFE, fromAddr, civAddr, 0x30, rcsType, 0xFD};
        Serial.printf("[CI-V] Sending 30 read/set response (rcsType as 6th byte): %s\n", formatBytesToHex(response, sizeof(response)).c_str());
        if (wsClient)
        {
            String hexMsg = formatBytesToHex(response, sizeof(response));
            wsClient->sendTXT(hexMsg);
        }
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
                wsClient->sendTXT(hexMsg);
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
                    wsClient->sendTXT(hexMsg);
                }
                broadcastAntennaState();
            }
            else
            {
                uint8_t response[] = {0xFE, 0xFE, 0xEE, civAddr, 0xFA, 0xFD};
                if (wsClient)
                {
                    String hexMsg = formatBytesToHex(response, sizeof(response));
                    wsClient->sendTXT(hexMsg);
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
            wsClient->sendTXT(hexMsg);
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
    uint8_t myAddr = civAddressPtr ? *civAddressPtr : 0xB4;
    uint8_t subcmd = (bytes.size() > 5) ? bytes[5] : 0x00;

    // Always answer 19 00 requests from broadcast (0x00) or our own address, even if fromAddr == myAddr
    if (cmd != 0x19 || subcmd != 0x00)
    {
        if (toAddr == myAddr && fromAddr == myAddr)
        {
            // Suppress verbose print: Both DEST and SRC are my CI-V address, not replying.
            return;
        }
    }

    bool isBroadcast = (toAddr == 0x00);
    bool isMine = (toAddr == myAddr);

    // Only process valid addressed/broadcast commands, but always process 19 00 from broadcast or our address
    if (
        (cmd == 0x19 && subcmd == 0x00 && (isBroadcast || isMine)) ||
        (cmd == 0x19 && subcmd == 0x01 && (isBroadcast || isMine)) ||
        (cmd == 0x30 && bytes.size() == 6 && bytes[5] == 0xFD && (isBroadcast || isMine)) ||
        (cmd == 0x31 && bytes.size() == 6 && bytes[5] == 0xFD && (isBroadcast || isMine)) ||
        isMine)
    {
        // Allowed, continue
    }
    else
    {
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
            uint8_t response[] = {0xFE, 0xFE, fromAddr, myAddr, 0x30, rcsType, 0xFD};
            if (wsClient)
            {
                String hexMsg = formatBytesToHex(response, sizeof(response));
                wsClient->sendTXT(hexMsg);
            }
            return;
        }
        if (bytes.size() == 7 && bytes[5] == 0x00 && bytes[6] == 0xFD && isMine)
        {
            rcsType = 0;
            // Save to NVS
            configPrefs.begin("config", false);
            configPrefs.putInt("rcs_type", rcsType);
            configPrefs.end();

            // Send correct response format: FE FE EE B4 30 00 FD (without extra data)
            uint8_t response[] = {0xFE, 0xFE, fromAddr, myAddr, 0x30, 0x00, 0xFD};
            if (wsClient)
            {
                String hexMsg = formatBytesToHex(response, sizeof(response));
                wsClient->sendTXT(hexMsg);
            }

            // Trigger callback to notify web UI
            if (antennaCallback)
            {
                antennaCallback(selectedAntennaPort, rcsType);
            }

            Serial.printf("[SMCIV] RCS type set to RCS-8 (0) via CI-V command\n");
            return;
        }
        if (bytes.size() == 7 && bytes[5] == 0x01 && bytes[6] == 0xFD && isMine)
        {
            rcsType = 1;
            // Save to NVS
            configPrefs.begin("config", false);
            configPrefs.putInt("rcs_type", rcsType);
            configPrefs.end();

            // Send correct response format: FE FE EE B4 30 01 FD (without extra data)
            uint8_t response[] = {0xFE, 0xFE, fromAddr, myAddr, 0x30, 0x01, 0xFD};
            if (wsClient)
            {
                String hexMsg = formatBytesToHex(response, sizeof(response));
                wsClient->sendTXT(hexMsg);
            }

            // Trigger callback to notify web UI
            if (antennaCallback)
            {
                antennaCallback(selectedAntennaPort, rcsType);
            }

            Serial.printf("[SMCIV] RCS type set to RCS-10 (1) via CI-V command\n");
            return;
        }
        if (isBroadcast && bytes.size() == 7 && (bytes[5] == 0x00 || bytes[5] == 0x01) && bytes[6] == 0xFD)
        {
            uint8_t response[] = {0xFE, 0xFE, fromAddr, myAddr, 0xFA, 0xFD};
            if (wsClient)
            {
                String hexMsg = formatBytesToHex(response, sizeof(response));
                wsClient->sendTXT(hexMsg);
            }
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
                wsClient->sendTXT(hexMsg);
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
                    wsClient->sendTXT(hexMsg);
                }
                broadcastAntennaState();
            }
            else
            {
                uint8_t response[] = {0xFE, 0xFE, 0xEE, myAddr, 0xFA, 0xFD};
                if (wsClient)
                {
                    String hexMsg = formatBytesToHex(response, sizeof(response));
                    wsClient->sendTXT(hexMsg);
                }
            }
            return;
        }
    }

    if (!(cmd == 0x19 && subcmd == 0x01))
    {
        uint8_t response[8] = {0xFE, 0xFE, fromAddr, myAddr, cmd, subcmd, myAddr, 0xFD};
        if (wsClient)
        {
            String hexMsg = formatBytesToHex(response, sizeof(response));
            wsClient->sendTXT(hexMsg);
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
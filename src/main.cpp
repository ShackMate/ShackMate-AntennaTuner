/*
 * ShackMate Antenna Tuner Controller
 * Refactored Version 2.1.0
 *
 * A modular, maintainable ESP32-S3 based antenna tuner controller
 * with web dashboard, CI-V integration, and MCP23017 GPIO expansion.
 *
 * Author: Half Baked Circuits
 * Date: 2025-07-31
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <time.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

// Project modules
#include "Config.h"
#include "ConfigManager.h"
#include "HardwareManager.h"
#include "ButtonManager.h"
#include "../lib/SMCIV/SMCIV.h"

// =========================================================================
// GLOBAL MANAGERS
// =========================================================================

ConfigManager config;
HardwareManager hardware(&config);
ButtonManager buttons(nullptr, &config); // MCP instance set after hardware init
SMCIV smciv;

// =========================================================================
// NETWORK OBJECTS
// =========================================================================

AsyncWebServer httpServer(HTTP_PORT);
AsyncWebServer *wsServer = nullptr;
AsyncWebSocket ws("/ws");
AsyncWebSocket dashboardWs("/dashboard-ws");
WiFiUDP udpDiscovery;
WebSocketsClient remoteWS;

// =========================================================================
// GLOBAL STATE
// =========================================================================

// System state
bool otaActive = false;
bool captivePortalActive = false;
bool remoteWSConnected = false;

// Network discovery
String deviceIP = "";
String tcpPort = String(WEBSOCKET_PORT);
String discoveredWsServer = "";
String lastRemoteWsServer = "";

// =========================================================================
// FORWARD DECLARATIONS
// =========================================================================

void setupWiFi();
void setupWebServers();
void setupOTA();
void setupDiscovery();
void loadFileSystem();

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len);
void onDashboardWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                        AwsEventType type, void *arg, uint8_t *data, size_t len);
void onRemoteWsEvent(WStype_t type, uint8_t *payload, size_t length);

void handleRoot(AsyncWebServerRequest *request);
void handleUpdateLatch(AsyncWebServerRequest *request);

void processUDPDiscovery();
void processWebSocketMessages();
void processSystemTasks();
void updateStatusLED();
void sendDashboardUpdate(AsyncWebSocketClient *client = nullptr);

String processTemplate(String tmpl);
String loadFile(const char *path);
String extractTimestamp(const String &json);
String toHexUpper(const String &data);

// =========================================================================
// SETUP
// =========================================================================

void setup()
{
  // Initialize serial with high baud rate
  Serial.begin(115200);
  delay(1000); // Allow serial to stabilize

  // Suppress ESP-IDF verbose logging
  esp_log_level_set("*", ESP_LOG_ERROR);

  // Print startup banner
  Serial.println("\n" + String("================================================="));
  Serial.printf("%s v%s\n", PROJECT_NAME, PROJECT_VERSION);
  Serial.printf("Build: %s\n", FIRMWARE_BUILD_DATE);
  Serial.printf("Author: %s\n", PROJECT_AUTHOR);
  Serial.println(String("================================================="));

  // Initialize core managers
  DEBUG_PRINTLN("[SETUP] Initializing core managers...");
  if (!config.begin())
  {
    Serial.println("[FATAL] Failed to initialize ConfigManager");
    ESP.restart();
  }

  if (!hardware.begin())
  {
    Serial.println("[FATAL] Failed to initialize HardwareManager");
    ESP.restart();
  }

  // Initialize button manager with MCP instance
  if (!buttons.setMCP(hardware.getMCP()))
  {
    Serial.println("[FATAL] Failed to set MCP instance in ButtonManager");
    ESP.restart();
  }

  if (!buttons.begin())
  {
    Serial.println("[FATAL] Failed to initialize ButtonManager");
    ESP.restart();
  }

  // Set initial LED state
  hardware.setLED(Colors::OFF);

  // Load file system
  loadFileSystem();

  // Setup networking
  setupWiFi();
  setupWebServers();
  setupOTA();
  setupDiscovery();

  // Load CI-V model and apply button states
  config.loadAllSettings();
  buttons.setButtonOutput("button-ant");
  buttons.setButtonOutput("button-auto");

  // Print final configuration
  config.printConfiguration();
  hardware.runDiagnostics();

  // Set LED to green - system ready
  hardware.setLED(Colors::GREEN);

  // Send initial dashboard update
  sendDashboardUpdate(nullptr);

  DEBUG_PRINTLN("[SETUP] System initialization complete!");
}

// =========================================================================
// MAIN LOOP
// =========================================================================

void loop()
{
  // Handle OTA updates
  ArduinoOTA.handle();

  // Update hardware components
  hardware.updateLED();

  // Process button states and momentary actions
  buttons.scanButtonStates();
  buttons.processMomentaryActions();

  // Update status LED based on system state
  updateStatusLED();

  // Process network tasks
  processUDPDiscovery();
  processWebSocketMessages();

  // Process system tasks
  processSystemTasks();

  // Handle remote WebSocket client
  remoteWS.loop();

  // Handle SMCIV tasks
  smciv.loop();
}

// =========================================================================
// INITIALIZATION FUNCTIONS
// =========================================================================

void loadFileSystem()
{
  DEBUG_PRINTLN("[SETUP] Mounting LittleFS...");

  if (!LittleFS.begin())
  {
    Serial.println("[ERROR] LittleFS mount failed!");
    hardware.setLED(Colors::RED);
    delay(3000);
  }
  else
  {
    DEBUG_PRINTLN("[INFO] LittleFS mounted successfully");
  }
}

void setupWiFi()
{
  DEBUG_PRINTLN("[SETUP] Configuring WiFi...");

  WiFiManager wifiManager;

  // Configure WiFi manager
  WiFi.mode(WIFI_AP_STA);
  captivePortalActive = true;
  hardware.setBlinkLED(Colors::PURPLE, LED_BLINK_SLOW);

  wifiManager.setDebugOutput(false);
  wifiManager.setAPCallback([](WiFiManager *wm)
                            { DEBUG_PRINTLN("[INFO] WiFiManager AP mode activated"); });

  // Try to connect
  if (!wifiManager.autoConnect(AP_NAME))
  {
    Serial.println("[ERROR] WiFi connection failed!");
    hardware.setLED(Colors::RED);
    delay(3000);
    ESP.restart();
  }

  // WiFi connected successfully
  deviceIP = WiFi.localIP().toString();
  captivePortalActive = false;
  hardware.setLED(Colors::GREEN);

  // Print connection info
  DEBUG_PRINTF("[WIFI] Connected to: %s\n", WiFi.SSID().c_str());
  DEBUG_PRINTF("[WIFI] IP Address: %s\n", deviceIP.c_str());
  DEBUG_PRINTF("[WIFI] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  DEBUG_PRINTF("[WIFI] Subnet: %s\n", WiFi.subnetMask().toString().c_str());
}

void setupWebServers()
{
  DEBUG_PRINTLN("[SETUP] Setting up web servers...");

  // Setup mDNS
  MDNS.begin(MDNS_NAME);

  // Setup WebSocket server
  ws.onEvent(onWsEvent);
  wsServer = new AsyncWebServer(WEBSOCKET_PORT);
  wsServer->addHandler(&ws);
  wsServer->begin();

  // Setup dashboard WebSocket
  dashboardWs.onEvent(onDashboardWsEvent);
  httpServer.addHandler(&dashboardWs);

  // Setup HTTP routes
  httpServer.on("/", HTTP_GET, handleRoot);
  httpServer.on("/updateLatch", HTTP_GET, handleUpdateLatch);
  httpServer.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(204); });

  // Debug routes
  httpServer.on("/test.html", HTTP_GET, [](AsyncWebServerRequest *request)
                {
        File file = LittleFS.open("/test.html", "r");
        if (!file) {
            request->send(404, "text/plain", "File not found");
            return;
        }
        request->send(LittleFS, "/test.html", "text/html");
        file.close(); });

  // MCP23017 test endpoint
  httpServer.on("/test-mcp", HTTP_GET, [](AsyncWebServerRequest *request)
                {
        String response = "MCP23017 Test Results:\n";
        
        if (!hardware.getMCP()) {
            response += "ERROR: MCP instance is null\n";
            request->send(500, "text/plain", response);
            return;
        }
        
        response += "MCP instance exists\n";
        
        // Test pin 7 (ANT button)
        try {
            hardware.getMCP()->pinMode(7, OUTPUT);
            hardware.getMCP()->digitalWrite(7, LOW);
            response += "ANT pin 7 set to LOW\n";
            delay(100);
            hardware.getMCP()->digitalWrite(7, HIGH);
            response += "ANT pin 7 set to HIGH\n";
            
            // Test reading back
            bool state = hardware.getMCP()->digitalRead(7);
            response += "ANT pin 7 reads: " + String(state ? "HIGH" : "LOW") + "\n";
        } catch (...) {
            response += "ERROR: Exception during MCP operation\n";
        }
        
        request->send(200, "text/plain", response); });

  // Device restart endpoint for debugging
  httpServer.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request)
                {
        request->send(200, "text/plain", "Restarting device...");
        delay(1000);
        ESP.restart(); });

  httpServer.begin();

  DEBUG_PRINTF("[INFO] Web servers started - HTTP: %d, WS: %d\n", HTTP_PORT, WEBSOCKET_PORT);
}

void setupOTA()
{
  DEBUG_PRINTLN("[SETUP] Configuring OTA updates...");

  ArduinoOTA.onStart([]()
                     {
        otaActive = true;
        hardware.setBlinkLED(Colors::WHITE, LED_BLINK_FAST);
        DEBUG_PRINTLN("[OTA] Update started"); });

  ArduinoOTA.onEnd([]()
                   {
        otaActive = false;
        hardware.setLED(Colors::GREEN);
        DEBUG_PRINTLN("[OTA] Update completed"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
                          // Optional: Add progress indication
                        });

  ArduinoOTA.onError([](ota_error_t error)
                     {
        hardware.setLED(Colors::RED);
        otaActive = false;
        DEBUG_PRINTF("[OTA] Error: %d\n", error); });

  ArduinoOTA.begin();
  DEBUG_PRINTLN("[INFO] OTA initialized");
}

void setupDiscovery()
{
  DEBUG_PRINTLN("[SETUP] Starting UDP discovery...");

  // Setup time synchronization
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // Start UDP for discovery
  udpDiscovery.begin(UDP_DISCOVERY_PORT);

  DEBUG_PRINTF("[INFO] UDP discovery started on port %d\n", UDP_DISCOVERY_PORT);
}

// =========================================================================
// WEBSOCKET HANDLERS
// =========================================================================

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    DEBUG_PRINTF("[WS] Client %u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    sendDashboardUpdate(client);
    break;

  case WS_EVT_DISCONNECT:
    DEBUG_PRINTF("[WS] Client %u disconnected\n", client->id());
    break;

  case WS_EVT_DATA:
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
    {
      data[len] = 0;
      String message = String((char *)data);
      DEBUG_PRINTF("[WS] Message from client %u: %s\n", client->id(), message.c_str());

      // Handle button presses (but avoid ANT/AUTO buttons which use latch format)
      if (message.startsWith("button:"))
      {
        String buttonName = message.substring(7);
        // Skip ANT and AUTO buttons - they should use latch format
        if (buttonName != "button-ant" && buttonName != "button-auto")
        {
          buttons.pressButton(buttonName);
        }
        else
        {
          DEBUG_PRINTF("[WS] Ignoring button: message for %s (should use latch format)\n", buttonName.c_str());
        }
      }
    }
    break;
  }

  case WS_EVT_ERROR:
    DEBUG_PRINTF("[WS] Error from client %u\n", client->id());
    break;
  }
}

void onDashboardWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                        AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    DEBUG_PRINTF("[DASH] Dashboard client %u connected\n", client->id());
    sendDashboardUpdate(client);
    break;

  case WS_EVT_DISCONNECT:
    DEBUG_PRINTF("[DASH] Dashboard client %u disconnected\n", client->id());
    break;

  case WS_EVT_DATA:
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
    {
      data[len] = 0;
      String message = String((char *)data);
      DEBUG_PRINTF("[DASH] Message: %s\n", message.c_str());

      // Handle simple text commands
      if (message == "request_update")
      {
        DEBUG_PRINTLN("[DASH] Request update command received");
        sendDashboardUpdate(client);
        return;
      }

      // Handle test commands
      if (message == "test_mcp")
      {
        DEBUG_PRINTLN("[DASH] MCP test command received");
        if (hardware.getMCP())
        {
          DEBUG_PRINTLN("[TEST] Testing MCP23017 ANT pin...");
          try
          {
            hardware.getMCP()->pinMode(BUTTON_ANT_PIN, OUTPUT);
            hardware.getMCP()->digitalWrite(BUTTON_ANT_PIN, LOW);
            DEBUG_PRINTF("[TEST] ANT pin %d set to LOW\n", BUTTON_ANT_PIN);
            delay(500);
            hardware.getMCP()->digitalWrite(BUTTON_ANT_PIN, HIGH);
            DEBUG_PRINTF("[TEST] ANT pin %d set to HIGH\n", BUTTON_ANT_PIN);
          }
          catch (...)
          {
            DEBUG_PRINTLN("[TEST] MCP operation failed!");
          }
        }
        else
        {
          DEBUG_PRINTLN("[TEST] MCP instance is null!");
        }
        return;
      }

      if (message == "debug_state")
      {
        DEBUG_PRINTLN("[DASH] Debug state command received");
        DEBUG_PRINTF("[DEBUG] Current ANT state: %s\n", config.getAntState() ? "true (ANT 2)" : "false (ANT 1)");
        DEBUG_PRINTF("[DEBUG] Current AUTO state: %s\n", config.getAutoState() ? "true (AUTO)" : "false (SEMI)");
        DEBUG_PRINTF("[DEBUG] MCP instance: %s\n", hardware.getMCP() ? "EXISTS" : "NULL");
        if (hardware.getMCP())
        {
          DEBUG_PRINTF("[DEBUG] MCP address: 0x%02X\n", MCP23017_ADDRESS);
        }
        return;
      }

      if (message == "force_restart")
      {
        DEBUG_PRINTLN("[DASH] Force restart command received");
        DEBUG_PRINTLN("=== FORCED RESTART ===");
        delay(100);
        ESP.restart();
        return;
      }

      // Handle momentary format messages (momentary:button-id:on/off) for Model 998
      if (message.startsWith("momentary:"))
      {
        DEBUG_PRINTF("[DASH] Processing momentary message: %s\n", message.c_str());
        int firstColon = message.indexOf(':', 0);               // Find first colon (after "momentary")
        int secondColon = message.indexOf(':', firstColon + 1); // Find second colon

        if (firstColon > 0 && secondColon > firstColon)
        {
          String buttonId = message.substring(firstColon + 1, secondColon); // Between first and second colon
          String actionStr = message.substring(secondColon + 1);            // After second colon
          bool isPress = (actionStr == "on");

          DEBUG_PRINTF("[DASH] Momentary - buttonId: '%s', action: '%s', isPress: %s\n",
                       buttonId.c_str(), actionStr.c_str(), isPress ? "true" : "false");

          // Handle ANT button momentary actions for Model 998
          if (buttonId == "button-ant")
          {
            DEBUG_PRINTF("[DASH] ANT momentary action: %s\n", isPress ? "PRESS (activate output)" : "RELEASE (deactivate output)");
            if (isPress)
            {
              bool success = buttons.startMomentaryAction("button-ant");
              DEBUG_PRINTF("[DASH] ANT momentary press started, success: %s\n", success ? "true" : "false");
            }
            else
            {
              bool success = buttons.stopMomentaryAction("button-ant");
              DEBUG_PRINTF("[DASH] ANT momentary press stopped, success: %s\n", success ? "true" : "false");
            }
          }
          // Handle other momentary buttons
          else if (buttonId.startsWith("button-"))
          {
            if (isPress)
            {
              buttons.startMomentaryAction(buttonId);
            }
            else
            {
              buttons.stopMomentaryAction(buttonId);
            }
          }
        }
        else
        {
          DEBUG_PRINTF("[DASH] Failed to parse momentary message: '%s'\n", message.c_str());
        }
        return;
      }

      // Handle latch format messages (latch:button-id:state)
      if (message.startsWith("latch:"))
      {
        DEBUG_PRINTF("[DASH] Processing latch message: %s\n", message.c_str());
        int firstColon = message.indexOf(':', 0);               // Find first colon (after "latch")
        int secondColon = message.indexOf(':', firstColon + 1); // Find second colon

        DEBUG_PRINTF("[DASH] Colon positions - first: %d, second: %d\n", firstColon, secondColon);

        if (firstColon > 0 && secondColon > firstColon)
        {
          String buttonId = message.substring(firstColon + 1, secondColon); // Between first and second colon
          String stateStr = message.substring(secondColon + 1);             // After second colon
          bool state = (stateStr == "true");

          DEBUG_PRINTF("[DASH] Parsed - buttonId: '%s', stateStr: '%s', state: %s\n",
                       buttonId.c_str(), stateStr.c_str(), state ? "true" : "false");

          DEBUG_PRINTF("[DASH] Latch command: %s -> %s\n", buttonId.c_str(), state ? "ON" : "OFF");

          // Handle ANT button state changes
          if (buttonId == "button-ant")
          {
            DEBUG_PRINTF("[DASH] Setting ANT state to: %s\n", state ? "true (ANT 2)" : "false (ANT 1)");
            DEBUG_PRINTF("[DASH] Current ANT state before change: %s\n", config.getAntState() ? "true" : "false");
            config.setAntState(state);
            DEBUG_PRINTF("[DASH] ANT state after change: %s\n", config.getAntState() ? "true" : "false");
            DEBUG_PRINTF("[DASH] ANT state saved, now calling setButtonOutput with state\n");
            bool success = buttons.setButtonOutput("button-ant", state);
            DEBUG_PRINTF("[DASH] ANT button output set, success: %s\n", success ? "true" : "false");
          }
          // Handle AUTO button state changes
          else if (buttonId == "button-auto")
          {
            DEBUG_PRINTF("[DASH] Setting AUTO state to: %s\n", state ? "true (AUTO)" : "false (SEMI)");
            config.setAutoState(state);
            bool success = buttons.setButtonOutput("button-auto", state);
            DEBUG_PRINTF("[DASH] AUTO button output set, success: %s\n", success ? "true" : "false");
          }

          // Send updated state to all clients
          sendDashboardUpdate(nullptr);
        }
        else
        {
          DEBUG_PRINTF("[DASH] Failed to parse latch message: '%s' (firstColon: %d, secondColon: %d)\n",
                       message.c_str(), firstColon, secondColon);
        }
        return;
      }

      // Parse JSON message
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, message);

      if (error)
      {
        DEBUG_PRINTF("[DASH] JSON parse error: %s\n", error.c_str());
        return;
      }

      // Handle different message types
      if (doc["type"] == "button")
      {
        String buttonName = doc["button"];
        // Skip ANT and AUTO buttons - they should use latch format
        if (buttonName != "button-ant" && buttonName != "button-auto")
        {
          buttons.pressButton(buttonName);
        }
        else
        {
          DEBUG_PRINTF("[DASH] Ignoring button type message for %s (should use latch format)\n", buttonName.c_str());
        }
      }
      else if (doc.containsKey("set_device_number"))
      {
        int newDeviceNumber = doc["set_device_number"];
        DEBUG_PRINTF("[DASH] Device number change request: %d\n", newDeviceNumber);
        config.setDeviceNumber(newDeviceNumber);
        // Send updated state to all clients
        sendDashboardUpdate(nullptr);
      }
      else if (doc["type"] == "deviceNumber")
      {
        int newDeviceNumber = doc["value"];
        DEBUG_PRINTF("[DASH] Device number change request: %d\n", newDeviceNumber);
        config.setDeviceNumber(newDeviceNumber);
        // Send updated state to all clients
        sendDashboardUpdate(nullptr);
      }
      else if (doc["type"] == "civModel")
      {
        String newModel = doc["value"];
        config.setCivModel(newModel);
        // Update button behavior based on new model
        buttons.setButtonOutput("button-ant");
        buttons.setButtonOutput("button-auto");
        // Send updated state to all clients
        sendDashboardUpdate(nullptr);
      }
      else if (doc.containsKey("set_civ_model"))
      {
        String newModel = doc["set_civ_model"];
        DEBUG_PRINTF("[DASH] CI-V model change request: %s\n", newModel.c_str());
        bool success = config.setCivModel(newModel);
        if (success)
        {
          // Update button behavior based on new model
          buttons.setButtonOutput("button-ant");
          buttons.setButtonOutput("button-auto");
          // Send updated state to all clients
          sendDashboardUpdate(nullptr);
        }
      }
      else if (doc["type"] == "requestState")
      {
        sendDashboardUpdate(client);
      }
    }
    break;
  }

  case WS_EVT_ERROR:
    DEBUG_PRINTF("[DASH] Error from client %u\n", client->id());
    break;
  }
}

void onRemoteWsEvent(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    remoteWSConnected = false;
    DEBUG_PRINTLN("[REMOTE] Disconnected from remote WebSocket");
    break;

  case WStype_CONNECTED:
    remoteWSConnected = true;
    DEBUG_PRINTF("[REMOTE] Connected to: %s\n", (char *)payload);
    break;

  case WStype_TEXT:
    DEBUG_PRINTF("[REMOTE] Received: %s\n", (char *)payload);
    // Forward messages to dashboard clients
    dashboardWs.textAll(String((char *)payload));
    break;

  case WStype_ERROR:
    DEBUG_PRINTF("[REMOTE] Error: %s\n", (char *)payload);
    break;

  default:
    break;
  }
}

// =========================================================================
// HTTP HANDLERS
// =========================================================================

void handleRoot(AsyncWebServerRequest *request)
{
  String html = loadFile("/index.html");
  if (html.length() == 0)
  {
    request->send(404, "text/plain", "Dashboard not found");
    return;
  }

  // Process template variables
  html = processTemplate(html);

  request->send(200, "text/html", html);
}

void handleUpdateLatch(AsyncWebServerRequest *request)
{
  if (request->hasParam("button"))
  {
    String buttonName = request->getParam("button")->value();
    buttons.setButtonOutput(buttonName);
  }
  request->send(200, "text/plain", "OK");
}

// =========================================================================
// UTILITY FUNCTIONS
// =========================================================================

String processTemplate(String tmpl)
{
  // Replace basic project variables
  tmpl.replace("%DEVICE_IP%", deviceIP);
  tmpl.replace("%PROJECT_NAME%", PROJECT_NAME);
  tmpl.replace("%PROJECT_VERSION%", PROJECT_VERSION);
  tmpl.replace("%VERSION%", PROJECT_VERSION);
  tmpl.replace("%DEVICE_NUMBER%", String(config.getDeviceNumber()));
  tmpl.replace("%CIV_MODEL%", config.getCurrentCivModel());
  tmpl.replace("%CIV_ADDRESS%", String(config.getCivAddress()));

  // Network information
  tmpl.replace("%IP%", deviceIP);
  tmpl.replace("%UDP_PORT%", String(UDP_DISCOVERY_PORT));
  tmpl.replace("%WEBSOCKET_PORT%", String(WEBSOCKET_PORT));

  // System information
  tmpl.replace("%CHIP_ID%", String((uint32_t)ESP.getEfuseMac(), HEX));
  tmpl.replace("%CPU_FREQ%", String(ESP.getCpuFreqMHz()));
  tmpl.replace("%FREE_HEAP%", String(ESP.getFreeHeap()));
  tmpl.replace("%UPTIME%", String(millis() / 1000) + " seconds");

  // Memory information
  tmpl.replace("%FLASH_TOTAL%", String(ESP.getFlashChipSize()));
  tmpl.replace("%FLASH_USED%", String(ESP.getSketchSize()));
  tmpl.replace("%FLASH_FREE%", String(ESP.getFreeSketchSpace()));
  tmpl.replace("%PSRAM_SIZE%", String(ESP.getPsramSize()));

  // Current time/timestamp
  tmpl.replace("%TIME%", String(millis()));

  return tmpl;
}

String loadFile(const char *path)
{
  File file = LittleFS.open(path, "r");
  if (!file)
  {
    DEBUG_PRINTF("[ERROR] Failed to open file: %s\n", path);
    return "";
  }

  String content = file.readString();
  file.close();

  return content;
}

String extractTimestamp(const String &json)
{
  int start = json.indexOf("\"timestamp\":\"") + 13;
  int end = json.indexOf("\"", start);
  if (start > 12 && end > start)
  {
    return json.substring(start, end);
  }
  return "";
}

String toHexUpper(const String &data)
{
  String hex = "";
  for (size_t i = 0; i < data.length(); i++)
  {
    char buf[3];
    sprintf(buf, "%02X", (unsigned char)data[i]);
    hex += buf;
  }
  return hex;
}

// =========================================================================
// SYSTEM TASKS
// =========================================================================

void processUDPDiscovery()
{
  static unsigned long lastDiscoveryTime = 0;
  unsigned long currentTime = millis();

  // Send discovery broadcast every 30 seconds
  if (currentTime - lastDiscoveryTime >= DISCOVERY_INTERVAL)
  {
    StaticJsonDocument<256> discoveryDoc;
    discoveryDoc["type"] = "shackmate-discovery";
    discoveryDoc["name"] = PROJECT_NAME;
    discoveryDoc["version"] = PROJECT_VERSION;
    discoveryDoc["ip"] = deviceIP;
    discoveryDoc["port"] = WEBSOCKET_PORT;
    discoveryDoc["device"] = config.getDeviceNumber();
    discoveryDoc["model"] = config.getCurrentCivModel();
    discoveryDoc["timestamp"] = String(currentTime);

    String discoveryMessage;
    serializeJson(discoveryDoc, discoveryMessage);

    // Broadcast to subnet
    IPAddress broadcastIP = WiFi.localIP() | (~WiFi.subnetMask());
    udpDiscovery.beginPacket(broadcastIP, UDP_DISCOVERY_PORT);
    udpDiscovery.print(discoveryMessage);
    udpDiscovery.endPacket();

    lastDiscoveryTime = currentTime;
  }

  // Check for incoming discovery messages
  int packetSize = udpDiscovery.parsePacket();
  if (packetSize)
  {
    char packetBuffer[512];
    int len = udpDiscovery.read(packetBuffer, sizeof(packetBuffer) - 1);
    packetBuffer[len] = '\0';

    // DEBUG_PRINTF("[DISCOVERY] Received UDP packet: %s\n", packetBuffer);

    // Parse discovery response
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, packetBuffer);

    if (!error)
    {
      String msgType = doc["type"];
      DEBUG_PRINTF("[DISCOVERY] Message type: %s\n", msgType.c_str());

      // Accept both controller and discovery responses
      if (msgType == "shackmate-controller" || msgType == "shackmate-discovery")
      {
        String serverIP = doc["ip"];
        int serverPort = doc["port"];

        if (serverIP != deviceIP)
        { // Don't connect to ourselves
          discoveredWsServer = "ws://" + serverIP + ":" + String(serverPort) + "/";
          DEBUG_PRINTF("[DISCOVERY] Found server: %s\n", discoveredWsServer.c_str());

          // Connect to remote WebSocket if not already connected
          if (!remoteWSConnected && discoveredWsServer != lastRemoteWsServer)
          {
            DEBUG_PRINTF("[DISCOVERY] Attempting to connect to: %s:%d\n", serverIP.c_str(), serverPort);
            remoteWS.begin(serverIP, serverPort, "/");
            remoteWS.onEvent(onRemoteWsEvent);
            lastRemoteWsServer = discoveredWsServer;
          }
        }
      }
    }
    else
    {
      // Try to parse as comma-separated values (ShackMate,IP,Port)
      String packet = String(packetBuffer);
      int firstComma = packet.indexOf(',');
      int secondComma = packet.indexOf(',', firstComma + 1);

      if (firstComma > 0 && secondComma > firstComma)
      {
        String deviceName = packet.substring(0, firstComma);
        String serverIP = packet.substring(firstComma + 1, secondComma);
        int serverPort = packet.substring(secondComma + 1).toInt();

        // DEBUG_PRINTF("[DISCOVERY] Parsed CSV: %s at %s:%d\n", deviceName.c_str(), serverIP.c_str(), serverPort);

        if (deviceName == "ShackMate" && serverIP != deviceIP)
        {
          discoveredWsServer = "ws://" + serverIP + ":" + String(serverPort) + "/";
          // DEBUG_PRINTF("[DISCOVERY] Found CSV server: %s\n", discoveredWsServer.c_str());

          // Connect to remote WebSocket if not already connected
          if (!remoteWSConnected && discoveredWsServer != lastRemoteWsServer)
          {
            DEBUG_PRINTF("[DISCOVERY] Attempting CSV connection to: %s:%d\n", serverIP.c_str(), serverPort);
            remoteWS.begin(serverIP, serverPort, "/");
            remoteWS.onEvent(onRemoteWsEvent);
            lastRemoteWsServer = discoveredWsServer;
          }
        }
      }
      else
      {
        DEBUG_PRINTF("[DISCOVERY] Unable to parse packet: %s\n", packetBuffer);
      }
    }
  }
}

void processWebSocketMessages()
{
  // Clean up disconnected clients
  ws.cleanupClients();
  dashboardWs.cleanupClients();
}

void processSystemTasks()
{
  static unsigned long lastStateUpdate = 0;
  unsigned long currentTime = millis();

  // Send state updates every 5 seconds
  if (currentTime - lastStateUpdate >= STATE_UPDATE_INTERVAL)
  {
    // DEBUG_PRINTLN("[INFO] Sending periodic dashboard update");
    sendDashboardUpdate(nullptr);
    lastStateUpdate = currentTime;
  }

  // Feed watchdog
  esp_task_wdt_reset();
}

void updateStatusLED()
{
  if (otaActive)
  {
    hardware.setBlinkLED(Colors::WHITE, LED_BLINK_FAST);
  }
  else if (captivePortalActive)
  {
    hardware.setBlinkLED(Colors::PURPLE, LED_BLINK_SLOW);
  }
  else if (!WiFi.isConnected())
  {
    hardware.setBlinkLED(Colors::RED, LED_BLINK_SLOW);
  }
  else if (remoteWSConnected)
  {
    hardware.setLED(Colors::BLUE);
  }
  else
  {
    hardware.setLED(Colors::GREEN);
  }
}

void sendDashboardUpdate(AsyncWebSocketClient *client)
{
  StaticJsonDocument<1024> doc;

  // System info
  doc["type"] = "dashboard_update";
  doc["device_number"] = config.getDeviceNumber();
  doc["civ_model"] = config.getCurrentCivModel();
  doc["civ_address"] = config.getCivAddress();
  doc["ip"] = deviceIP;
  doc["remote_ws_server"] = lastRemoteWsServer.length() > 0 ? lastRemoteWsServer : "Not connected";
  doc["version"] = PROJECT_VERSION;
  doc["time"] = String(millis());

  // Button states (match JavaScript field names)
  bool currentAntState = config.getAntState();
  doc["ant_state"] = currentAntState ? "ANT 2" : "ANT 1";
  doc["auto_state"] = config.getAutoState() ? "AUTO" : "SEMI";
  doc["ant_button_momentary"] = config.isModelMomentary();

  // DEBUG_PRINTF("[DASH] Sending ant_state: %s (config value: %s)\n",
  //              currentAntState ? "ANT 2" : "ANT 1", currentAntState ? "true" : "false");

  // Hardware indicators (match JavaScript field names)
  doc["tuning_active"] = hardware.getTuningStatus() ? 1 : 0;
  doc["swr_ok"] = hardware.getSWRStatus() ? 1 : 0;
  doc["remote_ws_connected"] = remoteWSConnected;

  // System information (match JavaScript field names)
  doc["chip_id"] = String((uint32_t)ESP.getEfuseMac(), HEX);
  doc["cpu_freq"] = ESP.getCpuFreqMHz();
  doc["mem_free"] = ESP.getFreeHeap() / 1024;          // Convert to KB
  doc["flash_total"] = ESP.getFlashChipSize() / 1024;  // Convert to KB
  doc["flash_used"] = ESP.getSketchSize() / 1024;      // Convert to KB
  doc["flash_free"] = ESP.getFreeSketchSpace() / 1024; // Convert to KB
  doc["psram_size"] = ESP.getPsramSize() / 1024;       // Convert to KB

  // Uptime breakdown
  unsigned long uptimeSeconds = millis() / 1000;
  doc["uptime_seconds"] = uptimeSeconds % 60;
  doc["uptime_minutes"] = (uptimeSeconds / 60) % 60;
  doc["uptime_hours"] = (uptimeSeconds / 3600) % 24;
  doc["uptime_days"] = uptimeSeconds / 86400;

  String message;
  serializeJson(doc, message);

  if (client)
  {
    client->text(message);
  }
  else
  {
    ws.textAll(message);
    dashboardWs.textAll(message);
  }

  // DEBUG_PRINTF("[STATE] Update sent - ANT:%s AUTO:%s\n",
  //              config.getAntState() ? "ON" : "OFF",
  //              config.getAutoState() ? "ON" : "OFF");
}

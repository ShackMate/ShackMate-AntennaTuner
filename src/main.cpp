#include <WebSocketsClient.h> // For remote WebSocket client
// ...existing includes...
#include <WebSocketsClient.h> // For remote WebSocket client
// Required includes (WebSocketsClient.h removed)
#include <stdint.h>
#include <ArduinoJson.h> // For StaticJsonDocument and DeserializationError
#include <WiFi.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <time.h>                     // For NTP and time functions
#include <LittleFS.h>                 // For serving HTML files from filesystem
#include <ArduinoOTA.h>               // OTA update support
#include "esp_task_wdt.h"             // Watchdog Timer
#include "esp_heap_caps.h"            // For heap_caps_get_total_size.h"
#include <Adafruit_NeoPixel.h>        // RGB LED support
#include "../lib/MCP23017/MCP23017.h" // MCP23017 I2C GPIO expander
#include <stdint.h>
// ...existing code...
// -------------------------------------------------------------------------
// MCP23017 Instance
// -------------------------------------------------------------------------
MCP23017 mcp(0x20); // Default I2C address 0x20

// -------------------------------------------------------------------------
// Forward declarations
// -------------------------------------------------------------------------
String toHexUpper(const String &data);
void sendDashboardUpdate(AsyncWebSocketClient *client = nullptr);
void sendStateUpdate();
void setDeviceNumber(int newDeviceNumber);
void onRemoteWsEvent(WStype_t type, uint8_t *payload, size_t length);
void setupButtonOutputs();
void setAtomLed(uint8_t r, uint8_t g, uint8_t b);
void loadLatchedStates();
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void onDashboardWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void handleRoot(AsyncWebServerRequest *request);
void handleUpdateLatch(AsyncWebServerRequest *request);

// (Adjust these definitions as appropriate for your hardware.)
#define BUTTON_ANT_PIN 0
#define BUTTON_CUP_PIN 0
#define BUTTON_LUP_PIN 0
#define BUTTON_AUTO_PIN 0
#define BUTTON_CDN_PIN 0
#define BUTTON_LDN_PIN 0
#define BUTTON_TUNE_PIN 0

// Project Constants
#define NAME "ShackMate - Tuner Control"
#define VERSION "2.0.0 "
#define AUTHOR "Half Baked Circuits"

// mDNS Name
#define MDNS_NAME "shackmate-tuner"

// UDP Port definition
#define UDP_PORT 4210

// Pin Definitions
#define LED_GREEN 2

// --- M5 Atom RGB LED Configuration ---
#define ATOM_LED_PIN 27 // M5 Atom Lite RGB LED pin
#define ATOM_NUM_LEDS 1
Adafruit_NeoPixel atom_led(ATOM_NUM_LEDS, ATOM_LED_PIN, NEO_GRB + NEO_KHZ800);

// -------------------------------------------------------------------------
// Global Preferences Instances
// -------------------------------------------------------------------------
// Preferences for WiFi credentials (set via BLE) using namespace "wifi"
Preferences wifiPrefs;
// Preferences for latched state
Preferences configPrefs;
// Preferences for device number (dedicated instance)
Preferences devicePrefs;

// -------------------------------------------------------------------------
// Global Variables and Objects
// -------------------------------------------------------------------------
String deviceIP = "";
String tcpPort = "";
String discoveredWsServer = ""; // Holds discovered IP:port for dashboard/remoteWS

bool antState = false;  // false = unlatched ("ANT 1")
bool autoState = false; // false = unlatched ("SEMI")
bool otaActive = false;
bool captivePortalActive = false;

AsyncWebServer httpServer(80);
AsyncWebServer *wsServer = nullptr;
AsyncWebSocket ws("/ws");

AsyncWebSocket dashboardWs("/dashboard-ws"); // WebSocket for dashboard on port 80
WiFiUDP udp;
WiFiUDP udpDiscovery;
unsigned long lastUdpBroadcast = 0;
const unsigned long broadcastInterval = 15000; // 15 seconds

// CI-V Address (for dashboard display and control)
uint8_t civAddr = 0xB4; // Default CI-V address (can be set dynamically)

// --- Remote WebSocket Client (remoteWS) ---
WebSocketsClient remoteWS;
String lastRemoteWsServer = "";

bool remoteWSConnected = false;

// -------------------------------------------------------------------------
// Setup Function
// -------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  Serial.println("[DEBUG] Setup started");
  WiFiManager wifiManager;
  // Initialize MCP23017
  mcp.begin();
  // Example: Set GPA0 as output and turn it on
  mcp.pinMode(0, OUTPUT); // GPA0
  mcp.digitalWrite(0, HIGH);
  Serial.begin(115200);
  delay(1000);
  pinMode(LED_GREEN, OUTPUT);
  setupButtonOutputs();

  // Initialize RGB LED
  atom_led.begin();
  atom_led.setBrightness(50);
  setAtomLed(0, 0, 0); // LED OFF initially

  if (!LittleFS.begin())
  {
    Serial.println("LittleFS mount failed");
    setAtomLed(255, 0, 0); // Red for error
  }
  else
  {
    // LittleFS mounted successfully
  }

  // Load latched button states from NVS
  loadLatchedStates();
  Serial.println("[DEBUG] Called loadLatchedStates() in setup()");

  wifiPrefs.begin("wifi", false);
  wifiPrefs.end();

  // Enable dual-mode (AP+STA) for WiFiManager captive portal.
  WiFi.mode(WIFI_AP_STA);

  // WiFiManager captive portal for configuration.
  Serial.println("Starting WiFiManager for configuration.");
  captivePortalActive = true;
  setAtomLed(128, 0, 128); // Purple during WiFi setup

  wifiManager.setAPCallback([](WiFiManager *wm)
                            {
    Serial.println("Entered configuration mode (AP mode)");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP().toString()); });
  if (!wifiManager.autoConnect("shackmate-tuner"))
  {
    Serial.println("Failed to connect using WiFiManager, restarting...");
    setAtomLed(255, 0, 0); // Red for failure
    delay(3000);
    ESP.restart();
    delay(5000);
  }
  deviceIP = WiFi.localIP().toString();

  captivePortalActive = false;
  setAtomLed(0, 255, 0); // Green after WiFi connects

  // --- End WiFi connection section ---

  Serial.println("Connected, IP address: " + deviceIP);

  // Load device number from NVS and set CI-V address
  devicePrefs.begin("device", false);
  int storedDeviceNumber = 1;
  bool hasDeviceNumber = devicePrefs.isKey("deviceNumber");
  if (hasDeviceNumber)
  {
    storedDeviceNumber = devicePrefs.getInt("deviceNumber", 1);
    Serial.printf("[NVS] Read deviceNumber from NVS: %d\n", storedDeviceNumber);
  }
  else
  {
    devicePrefs.putInt("deviceNumber", 1);
    Serial.println("[NVS] deviceNumber not found, initializing to 1");
  }
  storedDeviceNumber = constrain(storedDeviceNumber, 1, 4);
  civAddr = 0xB3 + storedDeviceNumber;
  devicePrefs.end();

  wifiPrefs.begin("wifi", false);
  wifiPrefs.end();

  // Time setup
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
    Serial.println("Failed to obtain time");
  else
    Serial.println("Time synchronized");

  // mDNS setup
  if (!MDNS.begin(MDNS_NAME))
    Serial.println("Error setting up mDNS responder!");
  else
  {
    Serial.print("mDNS responder started: http://");
    Serial.print(MDNS_NAME);
    Serial.println(".local");
  }

  // Start UDP
  udp.begin(UDP_PORT);
  udpDiscovery.begin(UDP_PORT);
  Serial.printf("UDP discovery listener started on port %d\n", UDP_PORT);

  // Set up WebSocket server on default port 4000.
  tcpPort = "4000";
  ws.onEvent(onWsEvent);
  wsServer = new AsyncWebServer(4000);
  wsServer->addHandler(&ws);
  wsServer->begin();
  Serial.printf("WebSocket server started on port 4000\n");

  // Set up dashboard WebSocket on port 80
  dashboardWs.onEvent(onDashboardWsEvent);
  httpServer.addHandler(&dashboardWs);

  // HTTP Routes
  httpServer.on("/", HTTP_GET, handleRoot);
  // /remote HTTP route removed
  httpServer.on("/updateLatch", HTTP_GET, handleUpdateLatch);
  httpServer.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
                {
    // Serve a 204 No Content for favicon.ico to avoid 500 errors
    request->send(204); });
  httpServer.begin();
  Serial.println("HTTP server started on port 80");

  // OTA Setup
  ArduinoOTA.onStart([]()
                     {
                       Serial.println("OTA update starting...");
                       otaActive = true;
                       setAtomLed(255, 255, 255); // White during OTA
                     });
  ArduinoOTA.onEnd([]()
                   {
                     Serial.println("\nOTA update complete");
                     otaActive = false;
                     setAtomLed(0, 255, 0); // Green when complete
                   });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("OTA Progress: %u%%\r", (progress * 100) / total); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("OTA Error[%u]: ", error);
    setAtomLed(255, 0, 0); // Red for error
    otaActive = false;
    if (error == OTA_AUTH_ERROR)
      Serial.println("Authentication Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed"); });
  ArduinoOTA.begin();
  Serial.println("OTA update service started");

  setAtomLed(0, 255, 0); // Green solid at end of setup
}

// -------------------------------------------------------------------------
// Helper: Update CI-V address and notify dashboards
// -------------------------------------------------------------------------
void updateCivAddressAndNotify(uint8_t newCivAddr)
{
  civAddr = newCivAddr;
  Serial.printf("[CI-V] Updated CI-V address to 0x%02X\n", civAddr);
  // Optionally, persist to NVS or perform other actions here
  // Notify all dashboards of the change
  sendDashboardUpdate(nullptr);
}

// -------------------------------------------------------------------------
// Helper Functions
// -------------------------------------------------------------------------

void setAtomLed(uint8_t r, uint8_t g, uint8_t b)
{
  atom_led.setPixelColor(0, atom_led.Color(r, g, b));
  atom_led.show();
}

void setupButtonOutputs()
{
  pinMode(BUTTON_ANT_PIN, OUTPUT);
  pinMode(BUTTON_CUP_PIN, OUTPUT);
  pinMode(BUTTON_LUP_PIN, OUTPUT);
  pinMode(BUTTON_AUTO_PIN, OUTPUT);
  pinMode(BUTTON_CDN_PIN, OUTPUT);
  pinMode(BUTTON_LDN_PIN, OUTPUT);
  pinMode(BUTTON_TUNE_PIN, OUTPUT);

  digitalWrite(BUTTON_ANT_PIN, LOW);
  digitalWrite(BUTTON_CUP_PIN, LOW);
  digitalWrite(BUTTON_LUP_PIN, LOW);
  digitalWrite(BUTTON_AUTO_PIN, LOW);
  digitalWrite(BUTTON_CDN_PIN, LOW);
  digitalWrite(BUTTON_LDN_PIN, LOW);
  digitalWrite(BUTTON_TUNE_PIN, LOW);
}

void setButtonOutput(const String &button)
{
  if (button == "button-ant")
    digitalWrite(BUTTON_ANT_PIN, antState ? HIGH : LOW);
  else if (button == "button-auto")
    digitalWrite(BUTTON_AUTO_PIN, autoState ? HIGH : LOW);
}

bool initLittleFS()
{
  if (!LittleFS.begin())
  {
    Serial.println("Error: LittleFS mount failed");
    return false;
  }
  Serial.println("LittleFS mounted successfully");
  return true;
}

void loadLatchedStates()
{
  configPrefs.begin("config", false);
  antState = configPrefs.getBool("ant", false);
  autoState = configPrefs.getBool("auto", false);
  configPrefs.end();
  Serial.printf("[NVS] loadLatchedStates: antState=%d, autoState=%d\n", antState, autoState);
}

String processTemplate(String tmpl)
{
  struct tm timeinfo;
  char timeStr[64];
  if (getLocalTime(&timeinfo))
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  else
    strcpy(timeStr, "TIME_NOT_SET");

  tmpl.replace("%PROJECT_NAME%", String(NAME));
  tmpl.replace("%TIME%", String(timeStr));
  tmpl.replace("%IP%", deviceIP);
  tmpl.replace("%WEBSOCKET_PORT%", tcpPort);
  tmpl.replace("%UDP_PORT%", String(UDP_PORT));
  tmpl.replace("%VERSION%", VERSION);

  String antLabel = antState ? "ANT 2" : "ANT 1";
  tmpl.replace("%ANT_STATE%", antLabel);
  String autoLabel = autoState ? "AUTO" : "SEMI";
  tmpl.replace("%AUTO_STATE%", autoLabel);

  uint32_t totalMem = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  uint32_t freeMem = ESP.getFreeHeap();
  uint32_t usedMem = totalMem - freeMem;
  tmpl.replace("%MEM_TOTAL%", String(totalMem / 1024) + " KB");
  tmpl.replace("%MEM_USED%", String(usedMem / 1024) + " KB");
  tmpl.replace("%MEM_FREE%", String(freeMem / 1024) + " KB");

  uint32_t flashTotal = ESP.getFlashChipSize();
  uint32_t sketchSize = ESP.getSketchSize();
  uint32_t flashFree = flashTotal - sketchSize;
  tmpl.replace("%FLASH_TOTAL%", String(flashTotal / 1024) + " KB");
  tmpl.replace("%FLASH_USED%", String(sketchSize / 1024) + " KB");
  tmpl.replace("%FLASH_FREE%", String(flashFree / 1024) + " KB");

  unsigned long secs = millis() / 1000;
  unsigned long mins = secs / 60;
  unsigned long hrs = mins / 60;
  unsigned long days = hrs / 24;
  secs = secs % 60;
  mins = mins % 60;
  hrs = hrs % 24;
  char uptimeStr[64];
  snprintf(uptimeStr, sizeof(uptimeStr), "%lu days %02lu:%02lu:%02lu", days, hrs, mins, secs);
  tmpl.replace("%UPTIME%", String(uptimeStr));

  uint64_t chipid = ESP.getEfuseMac();
  String chipIdStr = String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
  chipIdStr.toUpperCase();
  tmpl.replace("%CHIP_ID%", chipIdStr);

  int chipRev = ESP.getChipRevision();
  tmpl.replace("%CHIP_REV%", String(chipRev));

  uint32_t flashSizeKB = ESP.getFlashChipSize() / 1024;
  tmpl.replace("%FLASH_SIZE%", String(flashSizeKB));

  uint32_t psramSize = ESP.getPsramSize();
  tmpl.replace("%PSRAM_SIZE%", String(psramSize / 1024));

  tmpl.replace("%CPU_FREQ%", String(ESP.getCpuFreqMHz()));
  tmpl.replace("%FREE_HEAP%", String(ESP.getFreeHeap()));

  char memUsedTotalStr[32];
  snprintf(memUsedTotalStr, sizeof(memUsedTotalStr), "%u KB / %u KB", usedMem / 1024, totalMem / 1024);
  tmpl.replace("%MEM_USED_TOTAL%", String(memUsedTotalStr));

  return tmpl;
}

String loadFile(const char *path)
{
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory())
  {
    Serial.printf("Failed to open file: %s\n", path);
    return "";
  }
  String content;
  while (file.available())
  {
    content += char(file.read());
  }
  file.close();
  return content;
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  Serial.println("[DEBUG] onWsEvent called");
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
  {
    String msg;
    for (size_t i = 0; i < len; i++)
    {
      msg += (char)data[i];
    }
    if (msg.startsWith("momentary:"))
    {
      int firstColon = msg.indexOf(':');
      int secondColon = msg.indexOf(':', firstColon + 1);
      if (firstColon >= 0 && secondColon >= 0)
      {
        String button = msg.substring(firstColon + 1, secondColon);
        String stateStr = msg.substring(secondColon + 1);
        int pin = -1;
        if (button == "button-cup")
          pin = BUTTON_CUP_PIN;
        else if (button == "button-lup")
          pin = BUTTON_LUP_PIN;
        else if (button == "button-cdn")
          pin = BUTTON_CDN_PIN;
        else if (button == "button-ldn")
          pin = BUTTON_LDN_PIN;
        else if (button == "button-tune")
          pin = BUTTON_TUNE_PIN;
        if (pin != -1)
        {
          digitalWrite(pin, (stateStr == "on") ? HIGH : LOW);
          Serial.printf("Momentary command: %s set to %s\n", button.c_str(), stateStr.c_str());
          client->text("Momentary command processed");
          sendDashboardUpdate(nullptr); // Broadcast update to all dashboards
        }
        else
        {
          client->text("Invalid momentary button");
        }
      }
    }
    else if (msg.startsWith("latch:"))
    {
      int firstColon = msg.indexOf(':');
      int secondColon = msg.indexOf(':', firstColon + 1);
      if (firstColon >= 0 && secondColon >= 0)
      {
        String button = msg.substring(firstColon + 1, secondColon);
        String stateStr = msg.substring(secondColon + 1);
        bool state = (stateStr == "true");

        configPrefs.begin("config", false);
        if (button == "button-ant")
        {
          configPrefs.putBool("ant", state);
          Serial.printf("[NVS] putBool: ant=%d\n", state);
        }
        else if (button == "button-auto")
        {
          configPrefs.putBool("auto", state);
          Serial.printf("[NVS] putBool: auto=%d\n", state);
        }
        configPrefs.end();
        // Always reload from NVS after write
        loadLatchedStates();
        if (button == "button-ant" || button == "button-auto")
        {
          setButtonOutput(button);
        }

        Serial.printf("Latch command: %s set to %s\n", button.c_str(), stateStr.c_str());

        sendDashboardUpdate(nullptr); // Broadcast update to all dashboards
        client->text("Latch command processed");
      }
    }
    else
    {
      Serial.printf("Received via WebSocket: %s\n", msg.c_str());
      client->text("Message received: " + msg);
    }
    break;
  }
  default:
    break;
  }
}

void onDashboardWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  Serial.println("[DEBUG] onDashboardWsEvent called");
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("Dashboard WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    // Send initial dashboard data
    sendDashboardUpdate(client);
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("Dashboard WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
  {
    String msg;
    for (size_t i = 0; i < len; i++)
    {
      msg += (char)data[i];
    }
    Serial.printf("Dashboard WebSocket received: %s\n", msg.c_str());
    if (msg == "request_update")
    {
      sendDashboardUpdate(client);
    }
    else if (msg.startsWith("{"))
    {
      Serial.println("[DEBUG] Attempting to parse JSON for device number change...");
      StaticJsonDocument<128> doc;
      DeserializationError err = deserializeJson(doc, msg);
      if (err)
      {
        Serial.printf("[DEBUG] JSON parse error: %s\n", err.c_str());
      }
      else if (!doc.containsKey("set_device_number"))
      {
        Serial.println("[DEBUG] JSON does not contain 'set_device_number' key.");
      }
      else
      {
        int newDeviceNumber = doc["set_device_number"];
        Serial.printf("[DEBUG] Calling setDeviceNumber(%d)\n", newDeviceNumber);
        setDeviceNumber(newDeviceNumber);
        // Only send JSON response, not plain text
        sendDashboardUpdate(client);
      }
    }
    else if (msg.startsWith("latch:"))
    {
      int firstColon = msg.indexOf(':');
      int secondColon = msg.indexOf(':', firstColon + 1);
      if (firstColon >= 0 && secondColon >= 0)
      {
        String button = msg.substring(firstColon + 1, secondColon);
        String stateStr = msg.substring(secondColon + 1);
        bool state = (stateStr == "true");
        configPrefs.begin("config", false);
        if (button == "button-ant")
        {
          configPrefs.putBool("ant", state);
          Serial.printf("[NVS] putBool: ant=%d\n", state);
        }
        else if (button == "button-auto")
        {
          configPrefs.putBool("auto", state);
          Serial.printf("[NVS] putBool: auto=%d\n", state);
        }
        configPrefs.end();
        loadLatchedStates();
        if (button == "button-ant" || button == "button-auto")
        {
          setButtonOutput(button);
        }
        Serial.printf("Latch command: %s set to %s\n", button.c_str(), stateStr.c_str());
        sendDashboardUpdate(nullptr);
        client->text("Latch command processed");
      }
    }
    // Do not send any plain text or non-JSON responses to dashboard clients
    break;
  }
  default:
    break;
  }
}

void sendDashboardUpdate(AsyncWebSocketClient *client)
{
  struct tm timeinfo;
  char timeStr[64];
  if (getLocalTime(&timeinfo))
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  else
    strcpy(timeStr, "TIME_NOT_SET");

  uint32_t totalMem = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  uint32_t freeMem = ESP.getFreeHeap();
  uint32_t usedMem = totalMem - freeMem;

  uint32_t flashTotal = ESP.getFlashChipSize();
  uint32_t sketchSize = ESP.getSketchSize();
  uint32_t flashFree = flashTotal - sketchSize;

  unsigned long secs = millis() / 1000;
  unsigned long mins = secs / 60;
  unsigned long hrs = mins / 60;
  unsigned long days = hrs / 24;
  secs = secs % 60;
  mins = mins % 60;
  hrs = hrs % 24;
  char uptimeStr[64];
  snprintf(uptimeStr, sizeof(uptimeStr), "%lu days %02lu:%02lu:%02lu", days, hrs, mins, secs);

  uint64_t chipid = ESP.getEfuseMac();
  String chipIdStr = String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
  chipIdStr.toUpperCase();

  String antLabel = antState ? "ANT 2" : "ANT 1";
  String autoLabel = autoState ? "AUTO" : "SEMI";

  String dashboardData = "{";
  dashboardData += "\"type\":\"dashboard_update\",";
  dashboardData += "\"time\":\"" + String(timeStr) + "\",";
  dashboardData += "\"ip\":\"" + deviceIP + "\",";
  // Use discoveredWsServer if available, else deviceIP:tcpPort
  String wsServerDisplay = discoveredWsServer.length() > 0 ? discoveredWsServer : (deviceIP + ":" + tcpPort);
  dashboardData += "\"websocket_port\":\"" + wsServerDisplay + "\",";
  dashboardData += "\"udp_port\":" + String(UDP_PORT) + ",";
  dashboardData += "\"mem_total\":" + String(totalMem / 1024) + ",";
  dashboardData += "\"mem_used\":" + String(usedMem / 1024) + ",";
  dashboardData += "\"mem_free\":" + String(freeMem / 1024) + ",";
  dashboardData += "\"flash_total\":" + String(flashTotal / 1024) + ",";
  dashboardData += "\"flash_used\":" + String(sketchSize / 1024) + ",";
  dashboardData += "\"flash_free\":" + String(flashFree / 1024) + ",";
  dashboardData += "\"uptime\":\"" + String(uptimeStr) + "\",";
  dashboardData += "\"chip_id\":\"" + chipIdStr + "\",";
  dashboardData += "\"chip_rev\":" + String(ESP.getChipRevision()) + ",";
  dashboardData += "\"flash_size\":" + String(ESP.getFlashChipSize() / 1024) + ",";
  dashboardData += "\"psram_size\":" + String(ESP.getPsramSize() / 1024) + ",";
  dashboardData += "\"cpu_freq\":" + String(ESP.getCpuFreqMHz()) + ",";
  dashboardData += "\"ant_state\":\"" + antLabel + "\",";
  dashboardData += "\"auto_state\":\"" + autoLabel + "\",";

  // Add CI-V address and device number to dashboard JSON (baudrate removed)
  char civAddrStr[8];
  snprintf(civAddrStr, sizeof(civAddrStr), "0x%02X", civAddr);
  dashboardData += "\"civ_address\":\"" + String(civAddrStr) + "\",";
  // Always read device number from NVS for dashboard
  devicePrefs.begin("device", false);
  int deviceNumber = devicePrefs.getInt("deviceNumber", 1);
  // Serial.printf("[NVS] sendDashboardUpdate: deviceNumber from NVS: %d\n", deviceNumber);
  devicePrefs.end();
  deviceNumber = constrain(deviceNumber, 1, 4);
  dashboardData += "\"device_number\":" + String(deviceNumber) + ",";
  dashboardData += "\"ota_active\":" + String(otaActive ? "true" : "false") + ",";
  dashboardData += "\"captive_portal_active\":" + String(captivePortalActive ? "true" : "false") + ",";
  dashboardData += "\"remote_ws_connected\":" + String(remoteWSConnected ? "true" : "false");
  dashboardData += "}";

  if (client)
  {
    client->text(dashboardData);
  }
  else
  {
    dashboardWs.textAll(dashboardData);
  }
}

void sendStateUpdate()
{
  String antLabel = antState ? "ANT 2" : "ANT 1";
  String autoLabel = autoState ? "AUTO" : "SEMI";

  String stateData = "{";
  stateData += "\"type\":\"state_update\",";
  stateData += "\"ant_state\":\"" + antLabel + "\",";
  stateData += "\"auto_state\":\"" + autoLabel + "\"";
  stateData += "}";
}

void handleRoot(AsyncWebServerRequest *request)
{
  String page = loadFile("/index.html");
  if (page == "")
  {
    request->send(500, "text/plain", "Error loading page");
    return;
  }
  page = processTemplate(page);
  request->send(200, "text/html", page);
}

void handleUpdateLatch(AsyncWebServerRequest *request)
{
  if (!request->hasArg("button") || !request->hasArg("state"))
  {
    request->send(400, "text/plain", "Missing parameters");
    return;
  }
  String button = request->arg("button");
  bool state = (request->arg("state") == "true");
  configPrefs.begin("config", false);
  if (button == "button-ant")
  {
    configPrefs.putBool("ant", state);
    Serial.printf("[NVS] putBool: ant=%d\n", state);
  }
  else if (button == "button-auto")
  {
    configPrefs.putBool("auto", state);
    Serial.printf("[NVS] putBool: auto=%d\n", state);
  }
  configPrefs.end();
  // Always reload from NVS after write
  loadLatchedStates();
  if (button == "button-ant" || button == "button-auto")
  {
    setButtonOutput(button);
  }
  sendDashboardUpdate(nullptr); // Broadcast update to all dashboards
  request->send(200, "text/plain", "OK");
}

String extractTimestamp(const String &json)
{
  String key = "\"timestamp\":\"";
  int start = json.indexOf(key);
  if (start == -1)
    return "";
  start += key.length();
  int end = json.indexOf("\"", start);
  if (end == -1)
    return "";
  return json.substring(start, end);
}

void setDeviceNumber(int newDeviceNumber)
{
  newDeviceNumber = constrain(newDeviceNumber, 1, 4);
  devicePrefs.begin("device", false);
  devicePrefs.putInt("deviceNumber", newDeviceNumber);
  int verifyDeviceNumber = devicePrefs.getInt("deviceNumber", -1);
  Serial.printf("[NVS] Verified deviceNumber in NVS: %d\n", verifyDeviceNumber);
  devicePrefs.end();
  civAddr = 0xB3 + newDeviceNumber;
  sendDashboardUpdate(nullptr);
}

void loop()
{
  ArduinoOTA.handle();

  unsigned long currentMillis = millis();

  // RGB LED status indication (OTA/captivePortal override remoteWS color)
  static unsigned long ledLastToggle = 0;
  static bool ledOn = false;
  unsigned long ledBlinkInterval = 0;
  static uint8_t curR = 0, curG = 255, curB = 0; // Default: green

  if (otaActive)
  {
    ledBlinkInterval = 100;
    curR = 255;
    curG = 255;
    curB = 255; // WHITE fast blink during OTA
  }
  else if (captivePortalActive)
  {
    ledBlinkInterval = 500;
    curR = 128;
    curG = 0;
    curB = 128; // Purple
  }
  else if (remoteWSConnected)
  {
    ledBlinkInterval = 0;
    curR = 0;
    curG = 0;
    curB = 255; // Blue when remoteWS connected
  }
  else
  {
    ledBlinkInterval = 0;
    curR = 0;
    curG = 255;
    curB = 0; // Green for remoteWS disconnected
  }

  if (otaActive || captivePortalActive)
  {
    if (currentMillis - ledLastToggle >= ledBlinkInterval)
    {
      ledLastToggle = currentMillis;
      ledOn = !ledOn;
      setAtomLed(ledOn ? curR : 0, ledOn ? curG : 0, ledOn ? curB : 0);
    }
  }
  else
  {
    setAtomLed(curR, curG, curB);
  }

  // --- UDP ShackMate Discovery Listener (responds to "ShackMate" requests) ---
  int discLen = udpDiscovery.parsePacket();
  if (discLen > 0)
  {
    char discBuf[discLen + 1];
    int discRead = udpDiscovery.read(discBuf, discLen);
    discBuf[discRead] = '\0';
    String discMsg(discBuf);
    if (discMsg.startsWith("ShackMate"))
    {
      // Parse message: ShackMate,<ip>,<port>
      int firstComma = discMsg.indexOf(',');
      int secondComma = discMsg.indexOf(',', firstComma + 1);
      if (firstComma > 0 && secondComma > firstComma)
      {
        String foundIp = discMsg.substring(firstComma + 1, secondComma);
        String foundPort = discMsg.substring(secondComma + 1);
        String newDiscovered = foundIp + ":" + foundPort;
        if (discoveredWsServer != newDiscovered)
        {
          discoveredWsServer = newDiscovered;
          Serial.printf("[UDP DISCOVERY] Set discoveredWsServer to %s\n", discoveredWsServer.c_str());
          // Immediately push dashboard update to all clients
          if (dashboardWs.count() > 0)
          {
            sendDashboardUpdate(nullptr);
          }

          // --- Remote WebSocket client logic: connect/reconnect if needed ---
          if (discoveredWsServer != lastRemoteWsServer && discoveredWsServer.length() > 0)
          {
            // Parse IP and port
            int colonIdx = discoveredWsServer.indexOf(":");
            if (colonIdx > 0)
            {
              String ip = discoveredWsServer.substring(0, colonIdx);
              int port = discoveredWsServer.substring(colonIdx + 1).toInt();
              Serial.printf("[remoteWS] Connecting to ws://%s:%d/remote-ws\n", ip.c_str(), port);
              remoteWS.disconnect();
              remoteWS.begin(ip.c_str(), port, "/remote-ws");
              remoteWS.onEvent(onRemoteWsEvent); // Register event handler so connection state is tracked
              remoteWS.setReconnectInterval(5000);
              lastRemoteWsServer = discoveredWsServer;
            }
          }
        }
      }
      // Respond to discovery as before
      String resp = "ShackMate," + deviceIP + "," + String(tcpPort);
      udpDiscovery.beginPacket(udpDiscovery.remoteIP(), udpDiscovery.remotePort());
      udpDiscovery.print(resp);
      udpDiscovery.endPacket();
    }
  }

  // Periodically update dashboard (for uptime and live metrics)
  static unsigned long lastDashboardUpdate = 0;
  const unsigned long dashboardUpdateInterval = 1000; // 1 second
  if (currentMillis - lastDashboardUpdate >= dashboardUpdateInterval)
  {
    lastDashboardUpdate = currentMillis;
    if (dashboardWs.count() > 0)
    {
      sendDashboardUpdate(nullptr);
    }
  }

  // --- Remote WebSocket client loop ---
  remoteWS.loop();

  delay(1);
}

// --- Remote WebSocket event handler (file scope) ---
void onRemoteWsEvent(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_CONNECTED:
    remoteWSConnected = true;
    Serial.printf("[remoteWS] Connected to remote WebSocket server\n");
    break;
  case WStype_DISCONNECTED:
    remoteWSConnected = false;
    Serial.printf("[remoteWS] Disconnected from remote WebSocket server\n");
    break;
  case WStype_TEXT:
    Serial.printf("[remoteWS] Received: %s\n", (const char *)payload);
    // Optionally, handle remote commands here
    break;
  case WStype_ERROR:
    Serial.printf("[remoteWS] Error\n");
    break;
  default:
    break;
  }
}

String toHexUpper(const String &data)
{
  String hexStr = "";
  for (int i = 0; i < data.length(); i++)
  {
    uint8_t b = (uint8_t)data.charAt(i);
    char buffer[3];
    sprintf(buffer, "%02X", b);
    hexStr += buffer;
    hexStr += " ";
  }
  return hexStr;
}
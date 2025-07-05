/*
  Digital Water Curtain - ESP32 Controller Firmware
  Developed by JA3Jou3 & Ehsen
  Firmware Version: 4.4 (Auto IP, Favicon Fix, Pattern Storage)
*/

// LIBRARIES
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <EEPROM.h>

// CONFIGURATION
#define LATCH_PIN    12
#define CLOCK_PIN    14
#define DATA_PIN     13
#define LED_PIN      19

#define EEPROM_SIZE 256
#define CONFIG_MAGIC 0x44574346 // "DWCF"
struct Configuration {
  uint32_t magic;
  char ssid[64];
  char password[64];
  int numValves;
  int numLeds;
  bool configured;
};

const char* ap_ssid = "DigitalWaterCurtain-Setup";

// GLOBAL VARIABLES
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Configuration config;

int BYTES_PER_ROW = 2;
#define PATTERN_BUFFER_SIZE 8192
byte patternBuffer[PATTERN_BUFFER_SIZE];
CRGB* leds = nullptr;

int numPatternRows = 0;
int currentPatternRow = 0;
unsigned long lastUpdateTime = 0;
int animationSpeed = 100;
bool isPlaying = false;
const int BITS_PER_BYTE = 8;

void saveConfiguration() {
  config.magic = CONFIG_MAGIC;
  EEPROM.put(0, config);
  EEPROM.commit();
}

void clearConfiguration() {
  Configuration blankConfig;
  memset(&blankConfig, 0, sizeof(Configuration));
  blankConfig.configured = false;
  blankConfig.magic = 0;
  EEPROM.put(0, blankConfig);
  EEPROM.commit();
  // Also clear the in-memory config
  memcpy(&config, &blankConfig, sizeof(Configuration));
}

void loadConfiguration() {
  EEPROM.get(0, config);
  if (config.magic != CONFIG_MAGIC || config.numValves <= 0 || config.numLeds <= 0) {
    Serial.println("Magic number mismatch or invalid config data found. Resetting to defaults.");
    clearConfiguration();
  }
}

void writeShiftRegisters(byte rowData[]) {
  digitalWrite(LATCH_PIN, LOW);
  for (int i = BYTES_PER_ROW - 1; i >= 0; i--) {
    shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, rowData[i]);
  }
  digitalWrite(LATCH_PIN, HIGH);
}

void setupHardware() {
    if (leds != nullptr) {
        delete[] leds;
        leds = nullptr;
    }
    if (config.numValves > 0 && config.numLeds > 0) {
        BYTES_PER_ROW = (config.numValves + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
        leds = new CRGB[config.numLeds];
        if (leds) {
            FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, config.numLeds);
            FastLED.setBrightness(50);
            for(int i = 0; i < config.numLeds; i++) { leds[i] = CRGB::Black; }
            FastLED.show();
        } else {
            Serial.println("Error: Failed to allocate memory for LEDs!");
        }
    } else {
        Serial.println("Warning: Invalid number of valves or LEDs. Hardware not initialized.");
    }
}

// Function to save patterns to SPIFFS
void savePatternToSPIFFS(const String& patternName, const String& patternData) {
    String filename = "/patterns/" + patternName + ".json";
    
    // Create patterns directory if it doesn't exist
    if (!SPIFFS.exists("/patterns")) {
        // SPIFFS doesn't have mkdir, so we create a dummy file to establish the path
        File dummy = SPIFFS.open("/patterns/.dummy", "w");
        if (dummy) {
            dummy.close();
        }
    }
    
    File file = SPIFFS.open(filename, "w");
    if (file) {
        file.print(patternData);
        file.close();
        Serial.println("Pattern saved: " + filename);
    } else {
        Serial.println("Failed to save pattern: " + filename);
    }
}

// Function to load patterns from SPIFFS
String loadPatternsFromSPIFFS() {
    DynamicJsonDocument doc(8192);
    JsonArray patterns = doc.createNestedArray("patterns");
    
    File root = SPIFFS.open("/patterns");
    if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory() && String(file.name()).endsWith(".json")) {
                DynamicJsonDocument patternDoc(4096);
                DeserializationError error = deserializeJson(patternDoc, file);
                if (!error) {
                    patterns.add(patternDoc.as<JsonObject>());
                }
            }
            file = root.openNextFile();
        }
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

// Function to get MIME type based on file extension
String getContentType(String filename) {
    if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".png")) return "image/png";
    else if (filename.endsWith(".jpg")) return "image/jpeg";
    else if (filename.endsWith(".jpeg")) return "image/jpeg";
    else if (filename.endsWith(".gif")) return "image/gif";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    else if (filename.endsWith(".svg")) return "image/svg+xml";
    else if (filename.endsWith(".json")) return "application/json";
    else if (filename.endsWith(".woff")) return "font/woff";
    else if (filename.endsWith(".woff2")) return "font/woff2";
    else if (filename.endsWith(".ttf")) return "font/ttf";
    else if (filename.endsWith(".eot")) return "application/vnd.ms-fontobject";
    return "text/plain";
}

// Function to handle file serving from SPIFFS
void handleFileRequest(AsyncWebServerRequest *request) {
    String path = request->url();
    
    // If requesting root, serve index.html
    if (path == "/") {
        path = "/index.html";
    }
    
    // Handle favicon.ico specifically
    if (path == "/favicon.ico") {
        if (SPIFFS.exists("/favicon.ico")) {
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/favicon.ico", "image/x-icon");
            response->addHeader("Cache-Control", "public, max-age=31536000");
            response->addHeader("Access-Control-Allow-Origin", "*");
            request->send(response);
            return;
        } else {
            // Return a minimal 1x1 transparent ICO if favicon doesn't exist
            const uint8_t favicon[] = {
                0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x30, 0x00,
                0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
                0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00
            };
            AsyncWebServerResponse *response = request->beginResponse_P(200, "image/x-icon", favicon, sizeof(favicon));
            response->addHeader("Cache-Control", "public, max-age=31536000");
            response->addHeader("Access-Control-Allow-Origin", "*");
            request->send(response);
            return;
        }
    }
    
    Serial.println("Requested file: " + path);
    
    // Check if file exists in SPIFFS
    if (SPIFFS.exists(path)) {
        String contentType = getContentType(path);
        Serial.println("Serving file: " + path + " with content type: " + contentType);
        
        // Add cache control headers for static assets
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, path, contentType);
        if (path.endsWith(".js") || path.endsWith(".css")) {
            response->addHeader("Cache-Control", "public, max-age=31536000");
        }
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    } else {
        // If file doesn't exist, try to serve index.html for SPA routing
        Serial.println("File not found: " + path + ", serving index.html");
        if (SPIFFS.exists("/index.html")) {
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/index.html", "text/html");
            response->addHeader("Access-Control-Allow-Origin", "*");
            request->send(response);
        } else {
            Serial.println("index.html not found in SPIFFS!");
            request->send(404, "text/plain", "File not found");
        }
    }
}

const char* setupPage = R"rawliteral(
<!DOCTYPE html><html><head><title>Digital Water Curtain Setup</title><meta name="viewport" content="width=device-width, initial-scale=1"><style>body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background-color:#121212;color:#E0E0E0;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}.container{background-color:#1E1E1E;padding:2rem;border-radius:12px;box-shadow:0 8px 24px rgba(0,0,0,0.5);width:100%;max-width:400px;border:1px solid #333}h1{color:#BB86FC;text-align:center;margin-bottom:2rem}label{display:block;margin-bottom:.5rem;color:#B0B0B0}input{width:calc(100% - 20px);padding:10px;margin-bottom:1rem;border-radius:6px;border:1px solid #444;background-color:#2C2C2C;color:#E0E0E0;font-size:1rem}input:focus{outline:none;border-color:#BB86FC}button{background-color:#03DAC6;color:#000;border:none;padding:12px 20px;text-align:center;font-size:1rem;margin-top:1rem;cursor:pointer;border-radius:6px;width:100%;font-weight:bold}button:hover{background-color:#35fbe8}</style></head><body><div class="container"><h1>Device Configuration</h1><form action="/save" method="POST"><label for="ssid">WiFi SSID:</label><input type="text" id="ssid" name="ssid" required><label for="password">WiFi Password:</label><input type="password" id="password" name="password"><label for="valves">Number of Valves (multiple of 8):</label><input type="number" id="valves" name="valves" value="16" step="8" min="8" required><label for="leds">Number of LEDs:</label><input type="number" id="leds" name="leds" value="16" min="1" required><button type="submit">Save and Reboot</button></form></div></body></html>
)rawliteral";

void handleRoot(AsyncWebServerRequest *request){
    request->send_P(200, "text/html", setupPage);
}

void handleSave(AsyncWebServerRequest *request) {
    bool success = false;
    if(request->hasParam("ssid", true) && request->hasParam("valves", true) && request->hasParam("leds", true)) {
        String ssid = request->getParam("ssid", true)->value();
        String valves = request->getParam("valves", true)->value();
        String leds = request->getParam("leds", true)->value();

        if (ssid.length() > 0 && valves.toInt() > 0 && leds.toInt() > 0) {
            strncpy(config.ssid, ssid.c_str(), sizeof(config.ssid) - 1);
            
            // Safely handle optional password
            if (request->hasParam("password", true)) {
                strncpy(config.password, request->getParam("password", true)->value().c_str(), sizeof(config.password) - 1);
            } else {
                config.password[0] = '\0';
            }
            
            config.numValves = valves.toInt();
            config.numLeds = leds.toInt();
            config.configured = true;
            
            saveConfiguration();
            success = true;
        }
    }
    
    if (success) {
        request->send(200, "text/plain", "Configuration saved. Rebooting...");
        delay(1000);
        ESP.restart();
    } else {
        request->send(400, "text/plain", "Bad Request. Please provide all required fields.");
    }
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.println("WebSocket client connected");
        
        // Send current IP address to client
        DynamicJsonDocument doc(256);
        doc["action"] = "ip_address";
        doc["ip"] = WiFi.localIP().toString();
        String response;
        serializeJson(doc, response);
        client->text(response);
        
        // Send saved patterns to client
        String patterns = loadPatternsFromSPIFFS();
        if (patterns.length() > 10) { // Check if we have actual patterns
            client->text(patterns);
        }
        
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.println("WebSocket client disconnected");
    } else if (type == WS_EVT_DATA) {
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, (char*)data, len);
        
        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
            return;
        }

        const char* action = doc["action"];
        if (!action) return;

        if (strcmp(action, "reboot_to_ap") == 0) {
            clearConfiguration();
            Serial.println("Configuration cleared. Rebooting to AP mode.");
            delay(500);
            ESP.restart();
        } else if (strcmp(action, "config") == 0) {
            int newNumValves = doc["valves"];
            int newNumLeds = doc["leds"];
            if (newNumValves > 0 && newNumLeds > 0 && (newNumValves != config.numValves || newNumLeds != config.numLeds)) {
                config.numValves = newNumValves;
                config.numLeds = newNumLeds;
                setupHardware();
                saveConfiguration();
                Serial.printf("Reconfigured for %d valves and %d LEDs.\n", config.numValves, config.numLeds);
            }
        } else if (strcmp(action, "save_pattern") == 0) {
            // Save individual pattern to SPIFFS
            const char* patternName = doc["name"];
            if (patternName) {
                String patternData;
                serializeJson(doc, patternData);
                savePatternToSPIFFS(String(patternName), patternData);
                
                // Send confirmation back to client
                DynamicJsonDocument response(256);
                response["action"] = "pattern_saved";
                response["name"] = patternName;
                String responseStr;
                serializeJson(response, responseStr);
                client->text(responseStr);
            }
        } else if (strcmp(action, "get_patterns") == 0) {
            // Send all saved patterns to client
            String patterns = loadPatternsFromSPIFFS();
            client->text(patterns);
        } else if (strcmp(action, "play") == 0) {
            isPlaying = true;
            currentPatternRow = 0;
        } else if (strcmp(action, "pause") == 0) {
            isPlaying = false;
            byte* clearByte = new byte[BYTES_PER_ROW]();
            if (clearByte) {
                writeShiftRegisters(clearByte);
                delete[] clearByte;
            }
        } else if (strcmp(action, "speed") == 0) {
            animationSpeed = doc["value"];
        } else if (strcmp(action, "color") == 0) {
            if (!leds) return;
            const char* colorStr = doc["value"];
            long number = strtol( &colorStr[1], NULL, 16);
            for(int i = 0; i < config.numLeds; i++) {
                leds[i] = CRGB((number >> 16) & 0xFF, (number >> 8) & 0xFF, number & 0xFF);
            }
            FastLED.show();
        } else if (strcmp(action, "load_pattern") == 0) {
            JsonArray pattern = doc["pattern"].as<JsonArray>();
            numPatternRows = pattern.size();
            if(numPatternRows * BYTES_PER_ROW > PATTERN_BUFFER_SIZE) {
                Serial.println("Error: Pattern too large for buffer!");
                numPatternRows = 0;
                return;
            }
            
            int bufferIndex = 0;
            for (JsonArray row : pattern) {
                byte rowData[BYTES_PER_ROW] = {0};
                int valveIndex = 0;
                for (bool valveOn : row) {
                    if (valveOn) {
                        int bytePos = valveIndex / BITS_PER_BYTE;
                        int bitPos = valveIndex % BITS_PER_BYTE;
                        if(bytePos < BYTES_PER_ROW) {
                            bitSet(rowData[bytePos], bitPos); 
                        }
                    }
                    valveIndex++;
                }
                memcpy(&patternBuffer[bufferIndex], rowData, BYTES_PER_ROW);
                bufferIndex += BYTES_PER_ROW;
            }
            Serial.printf("Loaded pattern with %d rows.\n", numPatternRows);
        }
    }
}

void listSPIFFSFiles() {
    Serial.println("SPIFFS Files:");
    File root = SPIFFS.open("/");
    if (!root) {
        Serial.println("Failed to open SPIFFS root directory");
        return;
    }
    
    File file = root.openNextFile();
    while(file) {
        Serial.print("  ");
        Serial.print(file.name());
        Serial.print(" (");
        Serial.print(file.size());
        Serial.println(" bytes)");
        file = root.openNextFile();
    }
    file.close();
    root.close();
}

void setupSTA() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.ssid, config.password);
    Serial.print("Connecting to WiFi...");
    int retries = 20;
    while (WiFi.status() != WL_CONNECTED && retries > 0) {
        delay(500);
        Serial.print(".");
        retries--;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFailed to connect to WiFi. Rebooting into AP mode.");
        clearConfiguration();
        delay(1000);
        ESP.restart();
    }

    Serial.println("");
    Serial.print("Connected! IP Address: ");
    Serial.println(WiFi.localIP());

    // List SPIFFS files for debugging
    listSPIFFSFiles();

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    
    // Handle OPTIONS requests for CORS
    server.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        request->send(response);
    });
    
    // API endpoint to get current IP
    server.on("/api/ip", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(128);
        doc["ip"] = WiFi.localIP().toString();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Handle root specifically
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        handleFileRequest(request);
    });

    // Handle index.html specifically
    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        handleFileRequest(request);
    });
    
    // Handle all other requests with our custom handler
    server.onNotFound(handleFileRequest);

    // Enable CORS for all responses
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

    server.begin();
    Serial.println("Web server started");
    Serial.println("You can now access the web interface at: http://" + WiFi.localIP().toString());
}

void setupAP() {
    Serial.println("Starting in AP Mode for configuration.");
    WiFi.softAP(ap_ssid);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", setupPage);
    });
    server.begin();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Digital Water Curtain Controller...");
  
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  EEPROM.begin(EEPROM_SIZE);
  loadConfiguration();

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  Serial.println("SPIFFS mounted successfully");
  Serial.printf("SPIFFS Total: %d bytes\n", SPIFFS.totalBytes());
  Serial.printf("SPIFFS Used: %d bytes\n", SPIFFS.usedBytes());

  if (config.configured) {
    Serial.println("Device is configured. Starting in STA mode.");
    setupHardware();
    setupSTA();
  } else {
    Serial.println("Device not configured. Starting in AP mode.");
    setupAP();
  }
}

void loop() {
    if (config.configured) {
        ws.cleanupClients();
        if (isPlaying && numPatternRows > 0 && millis() - lastUpdateTime > animationSpeed) {
            lastUpdateTime = millis();
            int currentRowOffset = currentPatternRow * BYTES_PER_ROW;
            if ((currentRowOffset + BYTES_PER_ROW) <= PATTERN_BUFFER_SIZE) {
                writeShiftRegisters(&patternBuffer[currentRowOffset]);
            }
            currentPatternRow = (currentPatternRow + 1) % numPatternRows;
        }
    }
}
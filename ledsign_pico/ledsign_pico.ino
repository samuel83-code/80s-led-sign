
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <string.h>
#include "secrets.h"
#include "hardware/watchdog.h"

// ============================================================
// CONFIGURATION
// ============================================================

constexpr uint8_t DISPLAY_WIDTH = 90;
constexpr uint8_t DISPLAY_HEIGHT = 7;

constexpr unsigned long SCROLL_INTERVAL_MS = 75;
unsigned long scrollIntervalMs = SCROLL_INTERVAL_MS;
constexpr unsigned long ACK_TIMEOUT_MS = 300;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;


constexpr unsigned long WIFI_CHECK_INTERVAL_MS = 30000;
constexpr uint8_t WIFI_MAX_FAILURES = 3;

// Ändra porten om routerns webbgränssnitt använder HTTPS (443).
constexpr uint16_t ROUTER_TEST_PORT = 80;

unsigned long lastWiFiCheck = 0;
unsigned long lastWiFiRecovery = 0;

uint32_t wifiCheckSuccesses = 0;
uint32_t wifiCheckFailures = 0;
uint32_t wifiForcedReconnects = 0;
uint8_t consecutiveWiFiFailures = 0;

bool lastWiFiCheckOK = true;



constexpr uint32_t UART_BAUD = 57600;
constexpr size_t MAX_MESSAGE_BYTES = 256;

constexpr uint8_t FRAME_START_1 = 0xAA;
constexpr uint8_t FRAME_START_2 = 0x55;
constexpr uint8_t FRAME_ACK = 0x06;
constexpr uint8_t FRAME_NAK = 0x15;

// Piezo connected between GP28 and GND
constexpr uint8_t PIEZO_PIN = 28;
constexpr unsigned int BEEP_FREQUENCY = 1200;
constexpr unsigned int BEEP_DURATION_MS = 150;

const char WIFI_MESSAGE[] = "Connecting to WiFi...";

char message[MAX_MESSAGE_BYTES + 1] =
    "Gå ut och var glad din jävel";

WebServer server(80);

bool webServerStarted = false;
bool previousWiFiConnected = false;

unsigned long lastWiFiRetry = 0;
unsigned long lastFrameTime = 0;

int16_t scrollOffset = 0;
uint8_t frame[DISPLAY_WIDTH];

bool lastRebootWasWatchdog = false;

// ============================================================
// 5x7 FONT
// One blank column between characters
// ============================================================

struct Glyph {
  uint16_t codepoint;
  uint8_t rows[7];
};


const Glyph FONT[] = {
  // ============================================================
  // ASCII: SPACE, PUNCTUATION AND SYMBOLS
  // ============================================================
  {' ', {0,0,0,0,0,0,0}},
  {'!', {4,4,4,4,4,0,4}},
  {'"', {10,10,10,0,0,0,0}},
  {'#', {10,31,10,10,31,10,0}},
  {'$', {4,15,20,14,5,30,4}},
  {'%', {25,26,2,4,8,11,19}},
  {'&', {12,18,20,8,21,18,13}},
  {'\'',{4,4,8,0,0,0,0}},
  {'(', {2,4,8,8,8,4,2}},
  {')', {8,4,2,2,2,4,8}},
  {'*', {0,21,14,31,14,21,0}},
  {'+', {0,4,4,31,4,4,0}},
  {',', {0,0,0,0,4,4,8}},
  {'-', {0,0,0,31,0,0,0}},
  {'.', {0,0,0,0,0,12,12}},
  {'/', {1,1,2,4,8,16,16}},
  {':', {0,4,4,0,4,4,0}},
  {';', {0,4,4,0,4,4,8}},
  {'<', {2,4,8,16,8,4,2}},
  {'=', {0,0,31,0,31,0,0}},
  {'>', {8,4,2,1,2,4,8}},
  {'?', {14,17,1,2,4,0,4}},
  {'@', {14,17,23,21,23,16,15}},
  {'[', {14,8,8,8,8,8,14}},
  {'\\',{16,16,8,4,2,1,1}},
  {']', {14,2,2,2,2,2,14}},
  {'^', {4,10,17,0,0,0,0}},
  {'_', {0,0,0,0,0,0,31}},
  {'`', {8,4,2,0,0,0,0}},
  {'{', {6,8,8,16,8,8,6}},
  {'|', {4,4,4,4,4,4,4}},
  {'}', {12,2,2,1,2,2,12}},
  {'~', {0,0,9,22,0,0,0}},

  // ============================================================
  // DIGITS 0-9
  // ============================================================
  {'0', {14,17,19,21,25,17,14}},
  {'1', {4,12,4,4,4,4,14}},
  {'2', {14,17,1,2,4,8,31}},
  {'3', {30,1,1,14,1,1,30}},
  {'4', {2,6,10,18,31,2,2}},
  {'5', {31,16,16,30,1,1,30}},
  {'6', {14,16,16,30,17,17,14}},
  {'7', {31,1,2,4,8,8,8}},
  {'8', {14,17,17,14,17,17,14}},
  {'9', {14,17,17,15,1,1,14}},

  // ============================================================
  // UPPERCASE A-Z
  // ============================================================
  {'A', {14,17,17,31,17,17,17}},
  {'B', {30,17,17,30,17,17,30}},
  {'C', {15,16,16,16,16,16,15}},
  {'D', {30,17,17,17,17,17,30}},
  {'E', {31,16,16,30,16,16,31}},
  {'F', {31,16,16,30,16,16,16}},
  {'G', {15,16,16,23,17,17,15}},
  {'H', {17,17,17,31,17,17,17}},
  {'I', {31,4,4,4,4,4,31}},
  {'J', {7,2,2,2,18,18,12}},
  {'K', {17,18,20,24,20,18,17}},
  {'L', {16,16,16,16,16,16,31}},
  {'M', {17,27,21,21,17,17,17}},
  {'N', {17,25,21,19,17,17,17}},
  {'O', {14,17,17,17,17,17,14}},
  {'P', {30,17,17,30,16,16,16}},
  {'Q', {14,17,17,17,21,18,13}},
  {'R', {30,17,17,30,20,18,17}},
  {'S', {15,16,16,14,1,1,30}},
  {'T', {31,4,4,4,4,4,4}},
  {'U', {17,17,17,17,17,17,14}},
  {'V', {17,17,17,17,17,10,4}},
  {'W', {17,17,17,21,21,21,10}},
  {'X', {17,17,10,4,10,17,17}},
  {'Y', {17,17,10,4,4,4,4}},
  {'Z', {31,1,2,4,8,16,31}},

  // ============================================================
  // LOWERCASE a-z
  // ============================================================
  {'a', {0,0,14,1,15,17,15}},
  {'b', {16,16,30,17,17,17,30}},
  {'c', {0,0,15,16,16,16,15}},
  {'d', {1,1,15,17,17,17,15}},
  {'e', {0,0,14,17,31,16,14}},
  {'f', {6,8,8,30,8,8,8}},
  {'g', {0,14,17,17,15,1,14}},
  {'h', {16,16,30,17,17,17,17}},
  {'i', {4,0,12,4,4,4,14}},
  {'j', {2,0,6,2,2,18,12}},
  {'k', {16,16,18,20,24,20,18}},
  {'l', {12,4,4,4,4,4,14}},
  {'m', {0,0,26,21,21,21,21}},
  {'n', {0,0,30,17,17,17,17}},
  {'o', {0,0,14,17,17,17,14}},
  {'p', {0,30,17,17,30,16,16}},
  {'q', {0,15,17,17,15,1,1}},
  {'r', {0,0,22,24,16,16,16}},
  {'s', {0,0,15,16,14,1,30}},
  {'t', {8,8,30,8,8,8,6}},
  {'u', {0,0,17,17,17,19,13}},
  {'v', {0,0,17,17,17,10,4}},
  {'w', {0,0,17,17,21,21,10}},
  {'x', {0,0,17,10,4,10,17}},
  {'y', {0,17,17,17,15,1,14}},
  {'z', {0,0,31,2,4,8,31}},

  // ============================================================
  // SWEDISH CHARACTERS
  // ============================================================
  {0x00C5, {4,10,14,17,31,17,17}}, // Å
  {0x00C4, {10,0,14,17,31,17,17}}, // Ä
  {0x00D6, {10,0,14,17,17,17,14}}, // Ö
  {0x00E5, {4,10,14,1,15,17,15}},  // å
  {0x00E4, {10,0,14,1,15,17,15}},  // ä
  {0x00F6, {10,0,14,17,17,17,14}}, // ö

  // ============================================================
  // EUROPEAN AND MATHEMATICAL SYMBOLS
  // ============================================================
  {0x20AC, {7,8,30,8,30,8,7}},      // €
  {0x00A3, {6,9,8,28,8,8,31}},      // £
  {0x00B0, {6,9,9,6,0,0,0}},        // °
  {0x00A7, {14,16,12,18,6,1,14}},  // §
  {0x00B1, {4,4,31,4,4,0,31}},      // ±
  {0x00D7, {0,17,10,4,10,17,0}},   // ×
  {0x00F7, {0,4,0,31,0,4,0}},       // ÷
  {0x00A9, {14,17,23,20,23,17,14}},// ©
  {0x00AE, {14,17,29,27,29,17,14}},// ®
  {0x00B5, {0,0,17,17,19,29,16}},  // µ

  // ============================================================
  // ARROWS
  // ============================================================
  {0x2192, {0,4,2,31,2,4,0}},      // →
  {0x2190, {0,4,8,31,8,4,0}},      // ←
  {0x2191, {4,14,21,4,4,4,0}},     // ↑
  {0x2193, {0,4,4,4,21,14,4}},     // ↓

  // ============================================================
  // DECORATIVE SYMBOLS
  // ============================================================
  {0x2665, {0,10,21,31,31,14,4}},  // ♥
  {0x2605, {4,21,14,31,14,21,4}},  // ★
  {0x2606, {4,21,10,17,10,21,4}},  // ☆
  {0x263A, {14,17,21,17,21,17,14}},// ☺
  {0x2639, {14,17,21,17,14,17,14}},// ☹
  {0x266A, {6,5,5,5,29,28,0}},    // ♪
  {0x266B, {15,9,9,27,27,0,0}},   // ♫
  {0x2713, {0,1,2,20,8,0,0}},     // ✓
  {0x2717, {0,17,10,4,10,17,0}},  // ✗
  {0x2022, {0,0,0,4,0,0,0}},      // •
  {0x2026, {0,0,0,0,0,21,0}}      // …
};


constexpr size_t FONT_COUNT = sizeof(FONT) / sizeof(FONT[0]);



void checkWiFiConnection() {
  const unsigned long now = millis();

  if (now - lastWiFiCheck < WIFI_CHECK_INTERVAL_MS) {
    return;
  }

  lastWiFiCheck = now;

  if (WiFi.status() != WL_CONNECTED) {
    consecutiveWiFiFailures = 0;
    return; // maintainWiFi() handles normal disconnections.
  }

  WiFiClient client;
  IPAddress gateway = WiFi.gatewayIP();

  // Actual TCP connection test, not just WiFi.status().
  bool success = client.connect(gateway, ROUTER_TEST_PORT);
  client.stop();

  lastWiFiCheckOK = success;

  if (success) {
    wifiCheckSuccesses++;
    consecutiveWiFiFailures = 0;
    return;
  }

  wifiCheckFailures++;
  consecutiveWiFiFailures++;

  Serial.printf(
    "WiFi connectivity test failed (%u/%u)\n",
    consecutiveWiFiFailures,
    WIFI_MAX_FAILURES
  );

  if (consecutiveWiFiFailures >= WIFI_MAX_FAILURES) {
    Serial.println("WiFi appears stuck. Forcing reconnect...");

    wifiForcedReconnects++;
    lastWiFiRecovery = millis();
    consecutiveWiFiFailures = 0;

    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Let maintainWiFi() handle subsequent retries.
    lastWiFiRetry = millis();
  }
}


// ============================================================
// PIEZO
// ============================================================

void beep() {
  // Non-blocking: does not delay display updates.
  tone(PIEZO_PIN, BEEP_FREQUENCY, BEEP_DURATION_MS);
}

// ============================================================
// UTF-8 AND TEXT RENDERING
// ============================================================

uint16_t nextCodepoint(const char*& text) {
  const uint8_t first = static_cast<uint8_t>(*text++);

  // ASCII
  if (first < 0x80) {
    return first;
  }

  // Two-byte UTF-8: å, ä, ö etc.
  if ((first & 0xE0) == 0xC0 && text[0]) {
    const uint8_t second =
        static_cast<uint8_t>(*text++);

    return ((uint16_t)(first & 0x1F) << 6) |
           (second & 0x3F);
  }

  // Three-byte UTF-8: ♥ etc.
  if ((first & 0xF0) == 0xE0 && text[0] && text[1]) {
    const uint8_t second =
        static_cast<uint8_t>(*text++);
    const uint8_t third =
        static_cast<uint8_t>(*text++);

    return ((uint16_t)(first & 0x0F) << 12) |
           ((uint16_t)(second & 0x3F) << 6) |
           (third & 0x3F);
  }

  return '?';
}

const Glyph* findGlyph(uint16_t codepoint) {
  for (size_t i = 0; i < FONT_COUNT; ++i) {
    if (FONT[i].codepoint == codepoint) {
      return &FONT[i];
    }
  }
  return nullptr;
}

uint16_t countCharacters(const char* text) {
  uint16_t count = 0;

  while (*text) {
    nextCodepoint(text);
    ++count;
  }

  return count;
}

const char* getDisplayText() {
  return WiFi.status() == WL_CONNECTED
      ? message
      : WIFI_MESSAGE;
}

void setPixel(int16_t x, int8_t y) {
  if (x < 0 || x >= DISPLAY_WIDTH ||
      y < 0 || y >= DISPLAY_HEIGHT) {
    return;
  }

  frame[x] |= (1 << y);
}

void drawChar(int16_t x, uint16_t codepoint) {
  const Glyph* glyph = findGlyph(codepoint);

  if (!glyph) {
    return;
  }

  for (uint8_t row = 0; row < DISPLAY_HEIGHT; ++row) {
    const uint8_t bits = glyph->rows[row];

    for (uint8_t col = 0; col < 5; ++col) {
      if (bits & (1 << (4 - col))) {
        setPixel(x + col, row);
      }
    }
  }
}

void renderFrame(const char* text) {
  memset(frame, 0, sizeof(frame));

  int16_t x = DISPLAY_WIDTH - scrollOffset;

  while (*text) {
    drawChar(x, nextCodepoint(text));
    x += 6;
  }
}

// ============================================================
// UART FRAME TRANSMISSION
// Protocol unchanged: AA 55 + 90 bytes + XOR checksum
// ============================================================

bool sendFrame() {
  uint8_t checksum = 0;

  while (Serial1.available()) {
    Serial1.read();
  }

  Serial1.write(FRAME_START_1);
  Serial1.write(FRAME_START_2);

  for (uint8_t i = 0; i < DISPLAY_WIDTH; ++i) {
    Serial1.write(frame[i]);
    checksum ^= frame[i];
  }

  Serial1.write(checksum);
  Serial1.flush();

  const unsigned long start = millis();

  while (millis() - start < ACK_TIMEOUT_MS) {
    if (Serial1.available()) {
      const uint8_t response = Serial1.read();

      if (response == FRAME_ACK) return true;
      if (response == FRAME_NAK) return false;
    }

    yield();
  }

  return false;
}

// ============================================================
// REST API
// ============================================================

// Read a JSON string field, respecting escaped quotes.
// This is a lightweight parser for our simple API, not a
// complete JSON implementation.
bool extractJsonString(
    const String& body,
    const char* field,
    String& result
) {
  const String key = String("\"") + field + "\"";
  const int keyPos = body.indexOf(key);

  if (keyPos < 0) return false;

  const int colon = body.indexOf(':', keyPos + key.length());
  if (colon < 0) return false;

  int pos = colon + 1;

  while (pos < (int)body.length() &&
         (body[pos] == ' ' || body[pos] == '\t')) {
    ++pos;
  }

  if (pos >= (int)body.length() || body[pos] != '"') {
    return false;
  }

  ++pos;
  result = "";

  while (pos < (int)body.length()) {
    const char c = body[pos++];

    if (c == '"') {
      return true;
    }

    if (c == '\\') {
      if (pos >= (int)body.length()) return false;

      const char escaped = body[pos++];

      switch (escaped) {
        case '"':  result += '"'; break;
        case '\\': result += '\\'; break;
        case '/':  result += '/'; break;
        case 'n':  result += '\n'; break;
        case 'r':  result += '\r'; break;
        case 't':  result += '\t'; break;
        default:
          // Unsupported JSON escape (including \uXXXX).
          return false;
      }
    } else {
      result += c;
    }

    if (result.length() > MAX_MESSAGE_BYTES) {
      return false;
    }
  }

  return false;
}

// Read optional JSON boolean field, e.g. "beep": true.
bool jsonBooleanTrue(const String& body, const char* field) {
  const String key = String("\"") + field + "\"";
  const int keyPos = body.indexOf(key);

  if (keyPos < 0) return false;

  const int colon = body.indexOf(':', keyPos + key.length());
  if (colon < 0) return false;

  int pos = colon + 1;

  while (pos < (int)body.length() &&
         (body[pos] == ' ' || body[pos] == '\t')) {
    ++pos;
  }

  if (!body.startsWith("true", pos)) return false;

  const int end = pos + 4;
  return end == (int)body.length() ||
         body[end] == ',' ||
         body[end] == '}' ||
         body[end] == ' ' ||
         body[end] == '\n';
}

void handleMessage() {
  if (!server.hasArg("plain")) {
    server.send(
      400,
      "application/json",
      "{\"error\":\"Missing JSON body\"}"
    );
    return;
  }

  const String body = server.arg("plain");
  String newText;

  if (!extractJsonString(body, "text", newText)) {
    server.send(
      400,
      "application/json",
      "{\"error\":\"Invalid or missing text field\"}"
    );
    return;
  }

  if (newText.length() == 0 ||
      newText.length() > MAX_MESSAGE_BYTES) {
    server.send(
      400,
      "application/json",
      "{\"error\":\"Invalid text length\"}"
    );
    return;
  }

  // Update displayed message
  newText.toCharArray(message, sizeof(message));
  scrollOffset = 0;

  // Optional sound notification
  if (jsonBooleanTrue(body, "beep")) {
    beep();
  }

  server.send(
    200,
    "application/json",
    "{\"status\":\"ok\"}"
  );
}

void setupWebServer() {
  server.on("/api/message", HTTP_POST, handleMessage);

  
  server.on("/api/health", HTTP_GET, []() {
    String json = "{";
    json += "\"status\":\"ok\",";
    json += "\"uptime_seconds\":" + String(millis() / 1000) + ",";
    json += "\"watchdog_reboot\":";
    json += lastRebootWasWatchdog ? "true," : "false,";
    json += "\"wifi_status\":" + String(WiFi.status()) + ",";
    json += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"wifi_ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"wifi_check_ok\":";
    json += lastWiFiCheckOK ? "true," : "false,";
    json += "\"wifi_check_successes\":" + String(wifiCheckSuccesses) + ",";
    json += "\"wifi_check_failures\":" + String(wifiCheckFailures) + ",";
    json += "\"wifi_forced_reconnects\":" + String(wifiForcedReconnects) + ",";
    json += "\"last_wifi_recovery_seconds\":" + String(lastWiFiRecovery / 1000);
    json += "}";

    server.send(200, "application/json", json);
  });


  server.on("/api/config", HTTP_GET, []() {
    String json = "{\"scroll_interval_ms\":";
    json += String(scrollIntervalMs);
    json += "}";

    server.send(200, "application/json", json);
  });

  server.on("/api/config", HTTP_PATCH, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json",
                  "{\"error\":\"Missing JSON body\"}");
      return;
    }

    String body = server.arg("plain");

    // Accept only a JSON object containing scroll_interval_ms.
    int keyPos = body.indexOf("\"scroll_interval_ms\"");
    if (keyPos < 0) {
      server.send(400, "application/json",
                  "{\"error\":\"Missing scroll_interval_ms\"}");
      return;
    }

    int colon = body.indexOf(':', keyPos);
    if (colon < 0) {
      server.send(400, "application/json",
                  "{\"error\":\"Invalid JSON\"}");
      return;
    }

    String value = body.substring(colon + 1);
    value.trim();

    // Remove the closing brace and surrounding whitespace.
    int end = value.indexOf('}');
    if (end < 0) {
      server.send(400, "application/json",
                  "{\"error\":\"Invalid JSON\"}");
      return;
    }

    value = value.substring(0, end);
    value.trim();

    // Require a positive integer.
    if (value.length() == 0) {
      server.send(400, "application/json",
                  "{\"error\":\"Invalid interval\"}");
      return;
    }

    for (unsigned int i = 0; i < value.length(); i++) {
      if (value[i] < '0' || value[i] > '9') {
        server.send(400, "application/json",
                    "{\"error\":\"Interval must be an integer\"}");
        return;
      }
    }

    unsigned long interval = value.toInt();

    if (interval < 20 || interval > 1000) {
      server.send(400, "application/json",
                  "{\"error\":\"Interval must be 20-1000 ms\"}");
      return;
    }

    scrollIntervalMs = interval;

    String json = "{\"scroll_interval_ms\":";
    json += String(scrollIntervalMs);
    json += "}";

    server.send(200, "application/json", json);
  });



  server.begin();
}

// ============================================================
// WIFI CONNECTION MANAGEMENT
// ============================================================

void maintainWiFi() {
  const bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected != previousWiFiConnected) {
    previousWiFiConnected = connected;
    scrollOffset = 0;

    if (connected) {
      Serial.println("WiFi connected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      Serial.print("Signal strength: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
    } else {
      Serial.println(
        "WiFi disconnected. Showing connection message."
      );
    }
  }

  if (connected) {
    if (!webServerStarted) {
      setupWebServer();
      webServerStarted = true;
      Serial.println("REST API ready on port 80");
    }
    return;
  }

  if (millis() - lastWiFiRetry >= WIFI_RETRY_INTERVAL_MS) {
    lastWiFiRetry = millis();

    Serial.println("Retrying WiFi connection...");

    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// ============================================================
// MAIN
// ============================================================

void setup() {
  lastRebootWasWatchdog = watchdog_caused_reboot();
  Serial.begin(115200);

  // Piezo
  pinMode(PIEZO_PIN, OUTPUT);
  digitalWrite(PIEZO_PIN, LOW);

  // UART to Arduino Micro
  Serial1.setTX(0);  // GP0 -> Micro D0 RX
  Serial1.setRX(1);  // GP1 <- Micro D1 TX
  Serial1.begin(UART_BAUD);

  Serial.println();
  Serial.println("Pico W LED Sign starting...");

  WiFi.mode(WIFI_STA);
  WiFi.noLowPowerMode();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lastWiFiRetry = millis();

  Serial.println("Connecting to WiFi in the background...");
}

void loop() {
  maintainWiFi();
   checkWiFiConnection();

  if (webServerStarted &&
      WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  const unsigned long now = millis();

  if (now - lastFrameTime < scrollIntervalMs) {
    return;
  }

  lastFrameTime = now;

  const char* displayText = getDisplayText();
  renderFrame(displayText);

  if (!sendFrame()) {
    Serial.println("Frame failed - retrying");
    return;
  }

  ++scrollOffset;

  const int16_t textWidth =
      countCharacters(displayText) * 6;

  if (scrollOffset >= DISPLAY_WIDTH + textWidth) {
    scrollOffset = 0;
  }
}

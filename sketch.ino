/**
 * Conserve Naija recycling machine firmware (ESP32 / Wokwi).
 *
 * Same HTTP protocol as `cn-simulator`:
 *   Authorization: Device <key>
 *   POST /iot/devices/me/heartbeat
 *   POST /iot/devices/me/telemetry
 *   POST /iot/devices/me/sessions/claim            { "code": "482731" }
 *   POST /iot/devices/me/sessions/{id}/progress    { "stage": "sorting" }
 *   POST /iot/devices/me/sessions/{id}/measurement { "fractions": [...] }
 *
 * State machine:
 *   IDLE → ENTER_CODE → AUTHENTICATING → READY
 *        → MEASURING → SORTING → PROCESSING → SUCCESS → RESET → IDLE
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
// Laptop (VS Code + Private IoT Gateway). Public viewer / wokwi.com: Railway origin.
#ifndef CN_API_HOST
//#define CN_API_HOST "http://host.wokwi.internal:8080"
#define CN_API_HOST "https://conserve-naija-production.up.railway.app"
#endif
const char* API_HOST = CN_API_HOST;
const char* DEVICE_KEY = "cn-dev-yaba-device-key";
const char* FIRMWARE = "wokwi-0.4.2";
const uint32_t WIFI_RETRY_MS = 15000;
// Generous enough for a cold-started server. A tight timeout showed up as a
// misleading "NO REPLY" while the host was still booting.
const uint32_t HTTP_TIMEOUT_MS = 12000;
const uint32_t TLS_HANDSHAKE_S = 15;
const uint32_t HEARTBEAT_MS = 30000;
// How long a result stays on screen before returning to idle.
const uint32_t ERROR_HOLD_MS = 4000;
const uint32_t SUCCESS_HOLD_MS = 4000;

const uint8_t PIN_WEIGHT = 34;
const uint8_t PIN_WEIGH_BTN = 18;

enum MachineState {
  ST_IDLE,
  ST_ENTER_CODE,
  ST_AUTHENTICATING,
  ST_READY,
  ST_MEASURING,
  ST_SORTING,
  ST_PROCESSING,
  ST_SUCCESS,
  ST_ERROR,
  ST_RESET
};

LiquidCrystal_I2C lcd(0x27, 20, 4);

const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'C', '0', '#'}
};
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

byte enterGlyph[8] = {
  0b00001,
  0b00001,
  0b00001,
  0b00101,
  0b01101,
  0b11111,
  0b01100,
  0b00100
};

MachineState state = ST_IDLE;
String code;
String sessionId;
String lastError;
float lastWeightKg = 0;
int lastConservePoints = 0;
unsigned long stateEntered = 0;
unsigned long lastHeartbeat = 0;

void showCodeScreen(const String& shown) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("YOUR OTP");
  lcd.setCursor(0, 1);
  lcd.print(shown);
  lcd.setCursor(0, 2);
  lcd.print("C cancel ");
  lcd.write((uint8_t)0);
  lcd.print(" enter");
  Serial.printf("[ENTER_CODE] YOUR OTP | %s | C cancel  ⏎ enter\n", shown.c_str());
}

const char* stateName();

void show(const char* l0, const char* l1, const char* l2 = "", const char* l3 = "") {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(l0);
  lcd.setCursor(0, 1); lcd.print(l1);
  lcd.setCursor(0, 2); lcd.print(l2);
  lcd.setCursor(0, 3); lcd.print(l3);
  Serial.printf("[%s] %s | %s | %s | %s\n", stateName(), l0, l1, l2, l3);
}

void showWelcome() {
  show("Welcome to", "Conserve Site Yaba", "CN-MACHINE-001", "Input Conserve OTP");
}

const char* stateName() {
  switch (state) {
    case ST_IDLE: return "IDLE";
    case ST_ENTER_CODE: return "ENTER_CODE";
    case ST_AUTHENTICATING: return "AUTHENTICATING";
    case ST_READY: return "READY";
    case ST_MEASURING: return "MEASURING";
    case ST_SORTING: return "SORTING";
    case ST_PROCESSING: return "PROCESSING";
    case ST_SUCCESS: return "SUCCESS";
    case ST_ERROR: return "ERROR";
    case ST_RESET: return "RESET";
  }
  return "?";
}

void enter(MachineState next) {
  state = next;
  stateEntered = millis();
  switch (state) {
    case ST_IDLE:
      code = "";
      sessionId = "";
      showWelcome();
      break;
    case ST_ENTER_CODE:
      showCodeScreen(code);
      break;
    case ST_AUTHENTICATING:
      show("CONNECTING...", "Please wait.", "", "");
      break;
    case ST_READY:
      show("YOU'RE IN", "Conserve Site Yaba", "Dump mixed waste", "Press WEIGH");
      break;
    case ST_MEASURING:
      show("WEIGHING...", "", "Hold still", "");
      break;
    case ST_SORTING:
      show("SORTING...", "Mixed waste", "Plastic Glass", "Paper Metal");
      break;
    case ST_PROCESSING:
      show("COUNTING IT UP", "Hang on.", "", "");
      break;
    case ST_SUCCESS:
      show("THAT'S IN", "", "Thank you", "");
      break;
    case ST_ERROR:
      show("CANNOT CONTINUE", lastError.c_str(), "Resetting...", "");
      break;
    case ST_RESET:
      showWelcome();
      break;
  }
}

bool jsonKeyAt(const String& body, int at) {
  if (at <= 0) return true;
  char prev = body[at - 1];
  return prev == '{' || prev == ',' || prev == ' ' || prev == '\n' || prev == '\r' || prev == '\t';
}

String jsonGet(const String& body, const char* key) {
  String needle = String("\"") + key + "\":";
  int at = 0;
  while (true) {
    at = body.indexOf(needle, at);
    if (at < 0) return "";
    if (jsonKeyAt(body, at)) break;
    at += 1;
  }
  at += needle.length();
  while (at < (int)body.length() && (body[at] == ' ' || body[at] == '"')) {
    if (body[at] == '"') {
      int end = body.indexOf('"', at + 1);
      return end > at ? body.substring(at + 1, end) : "";
    }
    at++;
  }
  int end = at;
  while (end < (int)body.length() && (isDigit(body[end]) || body[end] == '.' || body[end] == '-')) {
    end++;
  }
  return body.substring(at, end);
}

int postJson(const String& path, const String& body, String* response) {
  HTTPClient http;
  String url = String(API_HOST) + path;
  WiFiClientSecure tls;
  WiFiClient plain;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (url.startsWith("https://")) {
    tls.setInsecure();
    tls.setHandshakeTimeout(TLS_HANDSHAKE_S);
    http.begin(tls, url);
  } else {
    http.begin(plain, url);
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Device ") + DEVICE_KEY);
  int codeHttp = http.POST(body);
  String payload = http.getString();
  Serial.printf("%s -> %d %s\n", path.c_str(), codeHttp, payload.c_str());
  if (response) *response = payload;
  http.end();
  return codeHttp;
}

bool httpOk(int status) {
  return status >= 200 && status < 300;
}

void setLastError(const String& response, int status, const char* fallback) {
  // The API reports problems as `detail`; older builds used `error`. A request
  // validation failure carries an array instead of a sentence, which parses to
  // an empty string, so the caller's fallback still applies.
  lastError = jsonGet(response, "error");
  if (lastError.length() == 0) lastError = jsonGet(response, "detail");
  if (lastError.length() > 20) lastError = lastError.substring(0, 20);
  if (lastError.length() == 0) {
    lastError = status < 0 ? "NO REPLY" : fallback;
  }
}

void heartbeat() {
  postJson("/iot/devices/me/heartbeat",
           String("{\"latitude\":6.5095,\"longitude\":3.3711,\"firmwareVersion\":\"") + FIRMWARE + "\"}",
           nullptr);
}

void telemetry(float binKg, int fill) {
  String body = "{\"location\":{\"latitude\":6.5095,\"longitude\":3.3711},\"bins\":[";
  body += "{\"material\":\"plastic\",\"weightKg\":";
  body += String(binKg * 0.5f, 1);
  body += ",\"fillPercent\":";
  body += String(fill);
  body += "},{\"material\":\"glass\",\"weightKg\":";
  body += String(binKg * 0.25f, 1);
  body += ",\"fillPercent\":";
  body += String(fill);
  body += "},{\"material\":\"paper\",\"weightKg\":";
  body += String(binKg * 0.15f, 1);
  body += ",\"fillPercent\":";
  body += String(fill);
  body += "},{\"material\":\"metal\",\"weightKg\":";
  body += String(binKg * 0.10f, 1);
  body += ",\"fillPercent\":";
  body += String(fill);
  body += "}]}";
  postJson("/iot/devices/me/telemetry", body, nullptr);
}

float readWeightKg() {
  int raw = analogRead(PIN_WEIGHT);
  if (raw < 0) raw = 0;
  if (raw > 4095) raw = 4095;
  return (raw / 4095.0f) * 5.0f;
}

void claimSession() {
  String response;
  String body = String("{\"code\":\"") + code + "\"}";
  int status = postJson("/iot/devices/me/sessions/claim", body, &response);
  if (!httpOk(status)) {
    setLastError(response, status, "BAD CODE");
    enter(ST_ERROR);
    return;
  }
  sessionId = jsonGet(response, "sessionId");
  if (sessionId.length() == 0) sessionId = jsonGet(response, "id");
  if (sessionId.length() == 0) {
    lastError = "NO CODE";
    enter(ST_ERROR);
    return;
  }
  enter(ST_READY);
}

void appendFraction(String& body, bool& first, const char* material, float kg) {
  if (kg < 0.05f) return;
  if (!first) body += ",";
  first = false;
  body += "{\"material\":\"";
  body += material;
  body += "\",\"weightKg\":";
  body += String(kg, 2);
  body += "}";
}

void submitFractions(float kg) {
  lastWeightKg = kg;
  String progressPath = String("/iot/devices/me/sessions/") + sessionId + "/progress";
  postJson(progressPath, "{\"stage\":\"sorting\"}", nullptr);

  float plastic = kg * 0.50f;
  float glass = kg * 0.25f;
  float paper = kg * 0.15f;
  float metal = kg * 0.10f;

  String body = "{\"fractions\":[";
  bool first = true;
  appendFraction(body, first, "plastic", plastic);
  appendFraction(body, first, "glass", glass);
  appendFraction(body, first, "paper", paper);
  appendFraction(body, first, "metal", metal);
  body += "]}";
  if (first) {
    lastError = "TOO LIGHT";
    enter(ST_ERROR);
    return;
  }

  String path = String("/iot/devices/me/sessions/") + sessionId + "/measurement";
  String response;
  enter(ST_PROCESSING);
  int status = postJson(path, body, &response);
  if (!httpOk(status)) {
    setLastError(response, status, "DEPOSIT FAILED");
    enter(ST_ERROR);
    return;
  }
  String cp = jsonGet(response, "conservePoints");
  if (cp.length() == 0) cp = jsonGet(response, "greenPoints");
  lastConservePoints = cp.toInt();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("THAT'S IN");
  lcd.setCursor(0, 1); lcd.print(String(lastWeightKg, 2) + " KG MIXED");
  lcd.setCursor(0, 2); lcd.print(String("+") + lastConservePoints + " CP");
  lcd.setCursor(0, 3); lcd.print("Conserve Site Yaba");
  state = ST_SUCCESS;
  stateEntered = millis();
}

void setup() {
  Serial.begin(115200);
  Serial.println("boot");
  pinMode(PIN_WEIGH_BTN, INPUT_PULLUP);
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, enterGlyph);
  Serial.printf("api %s\n", API_HOST);
  show("Welcome to", "Connecting WiFi", "", "");
  WiFi.mode(WIFI_STA);
  // Scan for Wokwi-GUEST. Pinning channel 6 skips the scan and misses the
  // AP when the viewer/gateway is not on 6 (public viewer and VS Code).
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - wifiStart > WIFI_RETRY_MS) {
      show("NO WIFI", "Can't get online", "Trying again", "");
      WiFi.disconnect();
      delay(400);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      wifiStart = millis();
    }
  }
  Serial.println("\nwifi up");
  lastHeartbeat = millis();
  enter(ST_IDLE);
}

void loop() {
  unsigned long now = millis();
  if (state == ST_IDLE && now - lastHeartbeat > HEARTBEAT_MS) {
    lastHeartbeat = now;
    heartbeat();
    telemetry(40.0 + (now / 1000 % 40), 40);
  }

  char key = keypad.getKey();
  if (state == ST_IDLE && key) {
    if (key >= '0' && key <= '9') {
      code = String(key);
      enter(ST_ENTER_CODE);
    }
  } else if (state == ST_ENTER_CODE && key) {
    if (key == 'C' || key == '*') {
      code = "";
      enter(ST_ENTER_CODE);
    } else if (key == '#') {
      if (code.length() == 6) {
        enter(ST_AUTHENTICATING);
        claimSession();
      }
    } else if (key >= '0' && key <= '9' && code.length() < 6) {
      code += key;
      String pretty = code;
      while (pretty.length() < 6) pretty += "_";
      showCodeScreen(pretty);
    }
  }

  if (state == ST_READY) {
    float kg = readWeightKg();
    lcd.setCursor(0, 1);
    lcd.print(String(kg, 2) + " KG mixed     ");
    lcd.setCursor(0, 2);
    lcd.print(kg < 0.1f ? "Dump mixed waste " : "Turn the scale   ");
    if (digitalRead(PIN_WEIGH_BTN) == LOW && kg >= 0.1f) {
      lastWeightKg = kg;
      enter(ST_MEASURING);
    }
  }

  if (state == ST_MEASURING) {
    lcd.setCursor(0, 1);
    lcd.print(String(lastWeightKg, 2) + " KG mixed  ");
    if (now - stateEntered > 800) {
      enter(ST_SORTING);
    }
  }

  if (state == ST_SORTING && now - stateEntered > 1600) {
    submitFractions(lastWeightKg);
  }

  if (state == ST_SUCCESS && now - stateEntered > SUCCESS_HOLD_MS) {
    enter(ST_RESET);
  }
  if (state == ST_ERROR && now - stateEntered > ERROR_HOLD_MS) {
    enter(ST_RESET);
  }
  if (state == ST_RESET && now - stateEntered > 1500) {
    enter(ST_IDLE);
  }
}

/*
 * ⬡ PADEL SCORE — ESP32 Firmware
 * 
 * Hardware: ESP32 (any variant with WiFi)
 * Dependencies (install via Arduino Library Manager):
 *   - ESPAsyncWebServer (by me-no-dev)
 *   - AsyncTCP (by me-no-dev)
 *   - ArduinoJson (by Benoit Blanchon) v7+
 *   - LittleFS (built-in with ESP32 Arduino core 2.x+)
 *
 * File upload:
 *   Upload the /data folder to LittleFS using:
 *   - Arduino IDE: Tools → ESP32 Sketch Data Upload
 *   - PlatformIO: pio run --target uploadfs
 *
 * Endpoints:
 *   http://192.168.4.1/display  → TV court display
 *   http://192.168.4.1/input    → Phone scoring input
 *   ws://192.168.4.1/ws         → WebSocket (state sync)
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ===== CONFIGURATION =====
const char* WIFI_SSID     = "padtrac-demo";
const char* WIFI_PASSWORD = "";  // Open network (no password)
const int   WIFI_CHANNEL  = 6;
const int   MAX_CLIENTS   = 8;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ===== GAME STATE =====
struct SideInfo {
  String type;       // "singles" or "doubles"
  String names[2];
  String teamName;
  bool   registered;
};

struct GameState {
  SideInfo sides[2];          // 0=left, 1=right
  int      points[2];
  int      games[2];
  int      sets[2][3];        // sets[side][setIndex]
  int      currentSet;
  bool     tiebreak;
  int      tiebreakPoints[2];
  bool     matchOver;
  int      winner;            // -1, 0, or 1
  int      serving;           // 0 or 1
  String   lastScoredBy;
  bool     matchStarted;
  unsigned long lastActivity;
};

GameState state;
GameState historyStack[200]; // undo stack
int historyTop = -1;

// ===== POINT NAMES =====
const char* POINT_NAMES[] = {"0", "15", "30", "40"};

// ===== FUNCTION DECLARATIONS =====
void resetState();
void pushHistory();
bool popHistory();
void scoreRegularPoint(int side);
void scoreTiebreakPoint(int side);
void winGame(int side);
void winSet(int side);
String getPointDisplay(int side);
String getSideName(int side);
String buildStateJson();
void broadcastState();
void handleWebSocketMessage(AsyncWebSocketClient* client, const String& msg);

// ===== STATE MANAGEMENT =====

void resetState() {
  for (int s = 0; s < 2; s++) {
    state.sides[s].type = "singles";
    state.sides[s].names[0] = "";
    state.sides[s].names[1] = "";
    state.sides[s].teamName = "";
    state.sides[s].registered = false;
    state.points[s] = 0;
    state.games[s] = 0;
    state.tiebreakPoints[s] = 0;
    for (int i = 0; i < 3; i++) state.sets[s][i] = 0;
  }
  state.currentSet = 0;
  state.tiebreak = false;
  state.matchOver = false;
  state.winner = -1;
  state.serving = 0;
  state.lastScoredBy = "";
  state.matchStarted = false;
  state.lastActivity = millis();
  historyTop = -1;
}

void pushHistory() {
  if (historyTop < 199) {
    historyTop++;
    historyStack[historyTop] = state;
  }
}

bool popHistory() {
  if (historyTop < 0) return false;
  state = historyStack[historyTop];
  historyTop--;
  state.lastActivity = millis();
  return true;
}

// ===== SCORING LOGIC =====

void scorePoint(int side, const String& scoredBy) {
  if (state.matchOver) return;
  pushHistory();
  state.lastScoredBy = scoredBy;
  state.lastActivity = millis();

  if (state.tiebreak) {
    scoreTiebreakPoint(side);
  } else {
    scoreRegularPoint(side);
  }
}

void scoreRegularPoint(int side) {
  int other = 1 - side;
  state.points[side]++;

  if (state.points[side] >= 4 && state.points[side] - state.points[other] >= 2) {
    winGame(side);
  }
}

void scoreTiebreakPoint(int side) {
  int other = 1 - side;
  state.tiebreakPoints[side]++;
  int total = state.tiebreakPoints[0] + state.tiebreakPoints[1];

  if (total == 1 || (total > 1 && (total - 1) % 2 == 0)) {
    state.serving = 1 - state.serving;
  }

  if (state.tiebreakPoints[side] >= 7 && state.tiebreakPoints[side] - state.tiebreakPoints[other] >= 2) {
    winGame(side);
  }
}

void winGame(int side) {
  state.games[side]++;
  state.points[0] = 0;
  state.points[1] = 0;

  int cs = state.currentSet;
  state.sets[side][cs] = state.games[side];

  if (state.tiebreak) {
    state.tiebreak = false;
    state.tiebreakPoints[0] = 0;
    state.tiebreakPoints[1] = 0;
    winSet(side);
    return;
  }

  state.serving = 1 - state.serving;

  if (state.games[side] >= 6 && state.games[side] - state.games[1 - side] >= 2) {
    winSet(side);
  } else if (state.games[0] == 6 && state.games[1] == 6) {
    state.tiebreak = true;
    state.tiebreakPoints[0] = 0;
    state.tiebreakPoints[1] = 0;
  }
}

void winSet(int side) {
  int cs = state.currentSet;
  state.sets[0][cs] = state.games[0];
  state.sets[1][cs] = state.games[1];

  int setsWon[2] = {0, 0};
  for (int i = 0; i <= cs; i++) {
    if (state.sets[0][i] > state.sets[1][i]) setsWon[0]++;
    else if (state.sets[1][i] > state.sets[0][i]) setsWon[1]++;
  }

  if (setsWon[side] >= 2) {
    state.matchOver = true;
    state.winner = side;
  } else {
    state.currentSet++;
    state.games[0] = 0;
    state.games[1] = 0;
  }
}

String getPointDisplay(int side) {
  if (state.tiebreak) return String(state.tiebreakPoints[side]);

  int p0 = state.points[0];
  int p1 = state.points[1];

  if (p0 >= 3 && p1 >= 3) {
    if (p0 == p1) return "40";
    if (state.points[side] > state.points[1 - side]) return "AD";
    return "40";
  }
  int idx = state.points[side];
  if (idx > 3) idx = 3;
  return String(POINT_NAMES[idx]);
}

String getSideName(int side) {
  if (state.sides[side].teamName.length() > 0) return state.sides[side].teamName;
  if (state.sides[side].registered) {
    String result = state.sides[side].names[0];
    if (state.sides[side].names[1].length() > 0) {
      result += " & " + state.sides[side].names[1];
    }
    return result;
  }
  return side == 0 ? "TEAM 1" : "TEAM 2";
}

// ===== JSON SERIALIZATION =====

String buildStateJson() {
  JsonDocument doc;

  // sides
  JsonObject sides = doc["sides"].to<JsonObject>();
  for (int s = 0; s < 2; s++) {
    const char* key = s == 0 ? "left" : "right";
    JsonObject side = sides[key].to<JsonObject>();
    side["type"] = state.sides[s].type;
    JsonArray names = side["names"].to<JsonArray>();
    names.add(state.sides[s].names[0]);
    if (state.sides[s].names[1].length() > 0) names.add(state.sides[s].names[1]);
    side["teamName"] = state.sides[s].teamName;
    side["registered"] = state.sides[s].registered;
  }

  // scores
  JsonArray pts = doc["points"].to<JsonArray>();
  pts.add(state.points[0]); pts.add(state.points[1]);

  JsonArray gms = doc["games"].to<JsonArray>();
  gms.add(state.games[0]); gms.add(state.games[1]);

  JsonArray setsArr = doc["sets"].to<JsonArray>();
  for (int s = 0; s < 2; s++) {
    JsonArray sideSet = setsArr.add<JsonArray>();
    for (int i = 0; i < 3; i++) sideSet.add(state.sets[s][i]);
  }

  doc["currentSet"] = state.currentSet;
  doc["tiebreak"] = state.tiebreak;

  JsonArray tbp = doc["tiebreakPoints"].to<JsonArray>();
  tbp.add(state.tiebreakPoints[0]); tbp.add(state.tiebreakPoints[1]);

  doc["matchOver"] = state.matchOver;
  doc["winner"] = state.winner;
  doc["serving"] = state.serving;
  doc["lastScoredBy"] = state.lastScoredBy;
  doc["matchStarted"] = state.matchStarted;

  // Display-ready point strings
  doc["pointsDisplay"][0] = getPointDisplay(0);
  doc["pointsDisplay"][1] = getPointDisplay(1);
  doc["nameLeft"] = getSideName(0);
  doc["nameRight"] = getSideName(1);

  String output;
  serializeJson(doc, output);
  return output;
}

void broadcastState() {
  String json = buildStateJson();
  ws.textAll(json);
}

// ===== WEBSOCKET HANDLER =====

void handleWebSocketMessage(AsyncWebSocketClient* client, const String& msg) {
  state.lastActivity = millis();

  if (msg.startsWith("score:")) {
    int side = msg.substring(6, 7).toInt();
    String scoredBy = msg.length() > 8 ? msg.substring(8) : "Scorer";
    scorePoint(side, scoredBy);
    broadcastState();
  }
  else if (msg == "undo") {
    if (popHistory()) broadcastState();
  }
  else if (msg == "reset") {
    historyTop = -1;
    state.points[0] = 0; state.points[1] = 0;
    state.games[0] = 0; state.games[1] = 0;
    for (int s = 0; s < 2; s++)
      for (int i = 0; i < 3; i++) state.sets[s][i] = 0;
    state.currentSet = 0;
    state.tiebreak = false;
    state.tiebreakPoints[0] = 0; state.tiebreakPoints[1] = 0;
    state.matchOver = false;
    state.winner = -1;
    state.serving = 0;
    state.lastScoredBy = "";
    state.lastActivity = millis();
    broadcastState();
  }
  else if (msg == "finish") {
    resetState();
    broadcastState();
  }
  else if (msg == "getState") {
    // New client requesting current state
    client->text(buildStateJson());
  }
  else if (msg.startsWith("startMatch:")) {
    // JSON payload with team info
    String payload = msg.substring(11);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return;

    String mode = doc["mode"] | "singles";

    state.sides[0].type = mode;
    state.sides[0].names[0] = doc["leftName1"] | "Player 1";
    state.sides[0].names[1] = (mode == "doubles") ? (doc["leftName2"] | String("Player 2")) : String("");
    state.sides[0].teamName = doc["leftTeam"] | "";
    state.sides[0].registered = true;

    state.sides[1].type = mode;
    state.sides[1].names[0] = doc["rightName1"] | "Player 2";
    state.sides[1].names[1] = (mode == "doubles") ? (doc["rightName2"] | String("Player 2")) : String("");
    state.sides[1].teamName = doc["rightTeam"] | "";
    state.sides[1].registered = true;

    state.matchStarted = true;
    state.lastActivity = millis();
    broadcastState();
  }
  else if (msg.startsWith("editTeams:")) {
    String payload = msg.substring(10);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return;

    String mode = doc["mode"] | "singles";

    state.sides[0].type = mode;
    state.sides[0].names[0] = doc["leftName1"] | "Player 1";
    state.sides[0].names[1] = (mode == "doubles") ? (doc["leftName2"] | String("Player 2")) : String("");
    state.sides[0].teamName = doc["leftTeam"] | "";

    state.sides[1].type = mode;
    state.sides[1].names[0] = doc["rightName1"] | "Player 2";
    state.sides[1].names[1] = (mode == "doubles") ? (doc["rightName2"] | String("Player 2")) : String("");
    state.sides[1].teamName = doc["rightTeam"] | "";

    state.lastActivity = millis();
    broadcastState();
  }
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WS] Client #%u connected from %s\n", client->id(),
                    client->remoteIP().toString().c_str());
      // Send current state to new client
      client->text(buildStateJson());
      break;

    case WS_EVT_DISCONNECT:
      Serial.printf("[WS] Client #%u disconnected\n", client->id());
      break;

    case WS_EVT_DATA: {
      AwsFrameInfo* info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0; // null-terminate
        String msg = String((char*)data);
        handleWebSocketMessage(client, msg);
      }
      break;
    }

    case WS_EVT_ERROR:
      Serial.printf("[WS] Client #%u error\n", client->id());
      break;

    case WS_EVT_PONG:
      break;
  }
}

// ===== SETUP =====

void setup() {
  Serial.begin(115200);
  Serial.println("\n⬡ PADEL SCORE — Starting...");

  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("ERROR: LittleFS mount failed!");
    return;
  }
  Serial.println("LittleFS mounted.");

  // List files
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf("  FILE: %s (%d bytes)\n", file.name(), file.size());
    file = root.openNextFile();
  }

  // Initialize game state
  resetState();

  // Start WiFi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL, 0, MAX_CLIENTS);
  delay(100);

  IPAddress ip = WiFi.softAPIP();
  Serial.printf("WiFi AP '%s' started on %s\n", WIFI_SSID, ip.toString().c_str());

  // WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->redirect("/input");
  });

  server.on("/display", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/display.html", "text/html");
  });

  server.on("/input", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/input.html", "text/html");
  });

  // Serve any static files from LittleFS (CSS, JS, etc. if needed)
  server.serveStatic("/", LittleFS, "/");

  server.begin();
  Serial.println("HTTP server started.");
  Serial.printf("  Display: http://%s/display\n", ip.toString().c_str());
  Serial.printf("  Input:   http://%s/input\n", ip.toString().c_str());
  Serial.println("Ready!");
}

// ===== LOOP =====

void loop() {
  ws.cleanupClients();
  delay(10);
}

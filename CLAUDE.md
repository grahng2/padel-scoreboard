# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Upload Commands

```bash
# Flash firmware to ESP32
pio run --target upload

# Upload HTML files to LittleFS filesystem
pio run --target uploadfs

# Monitor serial output (115200 baud)
pio device monitor
```

## Architecture

This is an ESP32 Arduino firmware project for a self-contained padel tennis scoreboard. No external server — the ESP32 itself is a WiFi access point and web server.

**Stack:**
- `padel_esp32.ino` — All firmware logic: WiFi AP, HTTP server, WebSocket hub, scoring engine
- `data/display.html` — TV court visualization page (read-only, receives state via WebSocket)
- `data/input.html` — Phone scoring input page (sends commands, receives state updates via WebSocket)

**Data flow:** Phone/TV connects to ESP32 WiFi AP → opens HTTP page from LittleFS → establishes WebSocket to `ws://192.168.4.1/ws` → phone sends text commands, server broadcasts JSON state to all clients.

**State management in firmware:**
- Single `GameState` struct holds all match state
- `historyStack[200]` array acts as an undo stack (push before every scored point)
- All state lives in RAM; no persistence across reboots
- `broadcastState()` serializes state to JSON and sends to all WebSocket clients

**WebSocket protocol:** Clients send plain text commands (`score:0:Name`, `undo`, `reset`, `finish`, `getState`, `startMatch:{json}`, `editTeams:{json}`); server responds with full JSON state broadcast.

**Scoring logic:** Standard padel/tennis rules — 0/15/30/40/Deuce/Advantage, games, best-of-3 sets, tiebreak at 6-6 (first to 7 with 2-point lead). All in `scoreRegularPoint()`, `scoreTiebreakPoint()`, `winGame()`, `winSet()`.

## Key Configuration (top of `padel_esp32.ino`)

```cpp
const char* WIFI_SSID     = "Padel-Court-1";
const char* WIFI_PASSWORD = "";   // empty = open network
const int   WIFI_CHANNEL  = 6;
const int   MAX_CLIENTS   = 8;
```

## Dependencies

- Platform: `espressif32 @ 6.9.0`, `esp32dev` board, Arduino framework
- Libraries: `ESPAsyncWebServer v1.2.4`, `AsyncTCP v1.1.4`, `ArduinoJson ^7.0.0`
- Filesystem: LittleFS (HTML files in `data/` are uploaded separately with `uploadfs`)

## External Resources

Google Fonts and QR code JS library load from CDN — requires phone/TV to have internet access in addition to ESP32 WiFi. System gracefully falls back to system fonts and a text QR placeholder on isolated networks.
